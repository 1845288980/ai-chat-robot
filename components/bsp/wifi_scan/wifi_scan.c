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
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define WIFI_SCAN_LIST_SIZE 12
#define WIFI_SCAN_DISPLAY_ROWS 8
#define WIFI_SCAN_REFRESH_PERIOD_MS 5000
#define WIFI_TEST_SSID "Gang"
#define WIFI_TEST_PASSWORD "18203728650"
#define WIFI_CONNECT_MAX_RETRIES 5
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi_scan";
static bool s_wifi_initialized;
static bool s_wifi_started;
static TaskHandle_t s_wifi_scan_task;
static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif;
static uint8_t s_connect_retries;

static void wifi_connection_event_handler(void *arg, esp_event_base_t event_base,
                                          int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "connecting to SSID: %s", WIFI_TEST_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_event_group != NULL) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        if (s_connect_retries < WIFI_CONNECT_MAX_RETRIES) ++s_connect_retries;
        else s_connect_retries = 1;
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi reconnect request failed: %s", esp_err_to_name(ret));
        }
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnect attempt %u/%u, reason=%d",
                 s_connect_retries, WIFI_CONNECT_MAX_RETRIES, event->reason);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        s_connect_retries = 0;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_event_group != NULL) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

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

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set STA mode");
    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_connect_to_ap(void)
{
    ESP_RETURN_ON_ERROR(wifi_scan_init(), TAG, "Wi-Fi initialization failed");
    if (s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_connection_event_handler, NULL),
                        TAG, "failed to register Wi-Fi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_connection_event_handler, NULL),
                        TAG, "failed to register IP handler");

    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", WIFI_TEST_SSID);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", WIFI_TEST_PASSWORD);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                        "failed to set Wi-Fi credentials");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    s_wifi_started = true;

    ESP_LOGI(TAG, "Wi-Fi started; connection and reconnection run asynchronously");
    return ESP_OK;
}

bool wifi_scan_is_connected(void)
{
    return s_wifi_event_group != NULL &&
           (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_scan_get_local_ip(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0 || s_sta_netif == NULL || !wifi_scan_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret != ESP_OK) return ret;
    int written = snprintf(buffer, capacity, IPSTR, IP2STR(&ip_info.ip));
    return written < 0 || (size_t)written >= capacity ? ESP_ERR_INVALID_SIZE : ESP_OK;
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

    if (!s_wifi_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
        s_wifi_started = true;
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
