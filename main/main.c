#include "esp_err.h"
#include "esp_log.h"
#include "iic.h"
#include "wifi_scan.h"
#include "tcp_client.h"
#include "inmp441.h"

void app_main(void)
{
    // 先准备 GPIO2 的中断，再开始翻转 GPIO1，防止漏掉第一条边沿。
    ESP_ERROR_CHECK(iic_oled_init(IIC_OLED_SDA_GPIO_DEFAULT,
                                  IIC_OLED_SCL_GPIO_DEFAULT,
                                  IIC_OLED_I2C_ADDRESS_DEFAULT));
    ESP_ERROR_CHECK(inmp441_audio_test_start());
    esp_err_t ret = wifi_connect_to_ap();
    if (ret != ESP_OK) {
        ESP_LOGE("main", "Wi-Fi connection did not succeed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_ERROR_CHECK(tcp_client_start());
}
