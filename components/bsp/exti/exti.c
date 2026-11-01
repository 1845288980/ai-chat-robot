#include "exti.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define EXTI_GPIO GPIO_NUM_2
#define EXTI_EVENT_QUEUE_LENGTH 10

// 队列用于把“中断发生时的电平”从 ISR 传给普通任务。
// ISR 中只能做非常短的、不会阻塞的工作，不能直接 printf。
static QueueHandle_t s_exti_event_queue;

/**
 * @brief GPIO2 的中断服务函数（ISR）。
 *
 * GPIO_INTR_ANYEDGE 表示上升沿和下降沿都进入这里。
 * 此时读取 GPIO2 的当前电平：1 表示刚发生上升沿，0 表示刚发生下降沿。
 */
static void IRAM_ATTR gpio2_isr_handler(void *arg)
{
    gpio_num_t gpio_num = (gpio_num_t)(uintptr_t)arg;
    int level = gpio_get_level(gpio_num);
    BaseType_t higher_priority_task_woken = pdFALSE;

    // xQueueSendFromISR 是专门给中断函数使用的队列 API，且不会等待。
    xQueueSendFromISR(s_exti_event_queue, &level, &higher_priority_task_woken);

    // 如果刚才唤醒了更高优先级的任务，让系统在退出中断时立刻切换过去。
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief 普通任务：从队列取出 ISR 保存的电平，再安全地输出到终端。
 */
static void exti_log_task(void *arg)
{
    int level;

    while (true) {
        // portMAX_DELAY 表示没有中断事件时一直等待，不会占用 CPU 轮询。
        if (xQueueReceive(s_exti_event_queue, &level, portMAX_DELAY) == pdTRUE) {
            printf("GPIO2 interrupt: %s\n", level ? "HIGH1" : "LOW1");
        }
    }
}

void exti_init(void)
{
    // 1. 配置 GPIO2 为输入，并开启双边沿中断。
    // 下拉电阻让未连接或 GPIO1 输出低电平时保持稳定的低电平。
    gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << EXTI_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&input_config));

    // 2. 创建事件队列。每个元素只保存一个 int 类型的电平值。
    s_exti_event_queue = xQueueCreate(EXTI_EVENT_QUEUE_LENGTH, sizeof(int));
    configASSERT(s_exti_event_queue != NULL);

    // 3. 创建打印任务。printf 放在这里，而不是放在中断服务函数中。
    BaseType_t task_created = xTaskCreate(exti_log_task, "exti_log", 2048, NULL, 10, NULL);
    configASSERT(task_created == pdPASS);

    // 4. 安装 GPIO 的全局中断服务；ESP_INTR_FLAG_IRAM 允许 ISR 放在 IRAM 中运行。
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    // 5. 将 GPIO2 和前面定义的 ISR 函数绑定在一起。
    // 参数 EXTI_GPIO 会作为 arg 传给 gpio2_isr_handler。
    ESP_ERROR_CHECK(gpio_isr_handler_add(EXTI_GPIO, gpio2_isr_handler,
                                        (void *)(uintptr_t)EXTI_GPIO));

    // 6. 显式启用 GPIO2 中断；之后 GPIO2 的每个上升沿/下降沿都会触发 ISR。
    ESP_ERROR_CHECK(gpio_intr_enable(EXTI_GPIO));
}
