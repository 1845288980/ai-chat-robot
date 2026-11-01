#include "wifi_scan.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "iic.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_SCAN_LIST_SIZE 12
#define WIFI_SCAN_DISPLAY_ROWS 8
#define WIFI_SCAN_REFRESH_PERIOD_MS 5000

static const char *TAG = "wifi_scan";
static bool s_wifi_initialized;
static TaskHandle_t s_wifi_scan_task;

esp_err_t wifi_scan_init(void)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "failed to erase NVS");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to initialize NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create event loop");

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_scan_show_on_oled(void)
{
    wifi_ap_record_t ap_info[WIFI_SCAN_LIST_SIZE] = {0};
    uint16_t ap_count = 0;
    uint16_t record_count = WIFI_SCAN_LIST_SIZE;
    esp_err_t ret = wifi_scan_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(NULL, true), TAG, "Wi-Fi scan failed");
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "failed to get AP count");
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&record_count, ap_info), TAG,
                        "failed to get AP records");

    ESP_LOGI(TAG, "found %u access points", ap_count);
    iic_oled_clear();
    iic_oled_write_string(0, 0, "WiFi networks:", true);

    uint16_t display_count = record_count;
    if (display_count > WIFI_SCAN_DISPLAY_ROWS - 1) {
        display_count = WIFI_SCAN_DISPLAY_ROWS - 1;
    }
    for (uint16_t i = 0; i < display_count; ++i) {
        char line[IIC_OLED_WIDTH / 6 + 1];
        if (ap_info[i].ssid[0] == '\0') {
            snprintf(line, sizeof(line), "%u: <hidden>", (unsigned)(i + 1));
        } else {
            snprintf(line, sizeof(line), "%u: %.18s", (unsigned)(i + 1),
                     (const char *)ap_info[i].ssid);
        }
        ESP_LOGI(TAG, "SSID='%.*s' BSSID=%02x:%02x:%02x:%02x:%02x:%02x RSSI=%d channel=%u",
                 (int)sizeof(ap_info[i].ssid), (const char *)ap_info[i].ssid,
                 ap_info[i].bssid[0], ap_info[i].bssid[1], ap_info[i].bssid[2],
                 ap_info[i].bssid[3], ap_info[i].bssid[4], ap_info[i].bssid[5],
                 ap_info[i].rssi, ap_info[i].primary);
        iic_oled_write_string(0, (uint8_t)(i + 1), line, true);
    }
    if (display_count == 0) {
        iic_oled_write_string(0, 1, "No networks", true);
    }
    return iic_oled_update();
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    while (true) {
        esp_err_t ret = wifi_scan_show_on_oled();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "periodic scan failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_REFRESH_PERIOD_MS));
    }
}

esp_err_t wifi_scan_start_periodic_task(void)
{
    ESP_RETURN_ON_ERROR(wifi_scan_init(), TAG, "Wi-Fi initialization failed");
    if (s_wifi_scan_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 5, &s_wifi_scan_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
