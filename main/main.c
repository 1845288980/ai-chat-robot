#include "esp_err.h"
#include "iic.h"
#include "wifi_scan.h"

void app_main(void)
{
    // 先准备 GPIO2 的中断，再开始翻转 GPIO1，防止漏掉第一条边沿。
    ESP_ERROR_CHECK(iic_oled_init(IIC_OLED_SDA_GPIO_DEFAULT,
                                  IIC_OLED_SCL_GPIO_DEFAULT,
                                  IIC_OLED_I2C_ADDRESS_DEFAULT));
    ESP_ERROR_CHECK(wifi_scan_start_periodic_task());
}
