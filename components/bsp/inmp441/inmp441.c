#include "inmp441.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iic.h"
#include "tcp_client.h"

#define INMP441_SAMPLE_RATE_HZ 16000
#define INMP441_MAX_RECORD_MS 10000
#define INMP441_MAX_PCM_BYTES (INMP441_SAMPLE_RATE_HZ * 2 * INMP441_MAX_RECORD_MS / 1000)
#define INMP441_BUTTON_GPIO GPIO_NUM_17
#define INMP441_DEBOUNCE_MS 30
#define INMP441_READ_BUFFER_BYTES 1024

static const char *TAG = "inmp441";
static i2s_chan_handle_t s_rx_channel;
static TaskHandle_t s_ptt_task;
static uint8_t *s_pcm_buffer;

static int64_t absolute_value(int32_t value)
{
    return value < 0 ? -(int64_t)value : value;
}

static esp_err_t inmp441_i2s_init(void)
{
    const i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&channel_config, NULL, &s_rx_channel);
    if (ret != ESP_OK) return ret;

    const i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(INMP441_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = INMP441_I2S_BCLK_GPIO,
            .ws = INMP441_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = INMP441_I2S_DOUT_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ret = i2s_channel_init_std_mode(s_rx_channel, &standard_config);
    if (ret != ESP_OK) {
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        return ret;
    }
    return i2s_channel_enable(s_rx_channel);
}

static void set_audio_status(const char *status)
{
    esp_err_t ret = iic_oled_set_text_row(3, status);
    if (ret != ESP_OK) ESP_LOGW(TAG, "OLED audio status update failed: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "audio status: %s", status);
}

static void ptt_task(void *arg)
{
    (void)arg;
    int32_t raw_samples[INMP441_READ_BUFFER_BYTES / sizeof(int32_t)];
    size_t pcm_length = 0;
    uint64_t pcm_absolute_sum = 0;
    int32_t pcm_peak = 0;
    uint64_t left_absolute_sum = 0;
    uint64_t right_absolute_sum = 0;
    uint32_t slot_sample_count = 0;
    bool recording = false;
    bool armed = true;
    int stable_button_level = 1;
    int candidate_button_level = 1;
    TickType_t candidate_since = xTaskGetTickCount();
    set_audio_status("FREE");

    while (true) {
        int button_level = gpio_get_level(INMP441_BUTTON_GPIO);
        TickType_t now = xTaskGetTickCount();
        if (button_level != candidate_button_level) {
            candidate_button_level = button_level;
            candidate_since = now;
        } else if (button_level != stable_button_level &&
                   (now - candidate_since) >= pdMS_TO_TICKS(INMP441_DEBOUNCE_MS)) {
            stable_button_level = button_level;
            if (stable_button_level == 0 && armed && !recording) {
                recording = true;
                armed = false;
                pcm_length = 0;
                pcm_absolute_sum = 0;
                pcm_peak = 0;
                left_absolute_sum = 0;
                right_absolute_sum = 0;
                slot_sample_count = 0;
                set_audio_status("RECORDING");
            } else if (stable_button_level != 0) {
                armed = true;
                if (recording) recording = false;
            }
        }

        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_channel, raw_samples, sizeof(raw_samples),
                                         &bytes_read, 20);
        if (ret == ESP_OK && recording) {
            size_t words = bytes_read / sizeof(raw_samples[0]);
            for (size_t i = 0; i + 1 < words && pcm_length + 1 < INMP441_MAX_PCM_BYTES; i += 2) {
                // INMP441 emits 24-bit samples left-aligned in each 32-bit I2S
                // slot. L/R is tied low, and diagnostics show the mic on left.
                int32_t left_sample = raw_samples[i] >> 8;
                int32_t right_sample = raw_samples[i + 1] >> 8;
                int32_t sample = raw_samples[i] >> 16;
                left_absolute_sum += (uint64_t)absolute_value(left_sample);
                right_absolute_sum += (uint64_t)absolute_value(right_sample);
                ++slot_sample_count;
                if (sample > INT16_MAX) sample = INT16_MAX;
                if (sample < INT16_MIN) sample = INT16_MIN;
                int16_t pcm_sample = (int16_t)sample;
                int32_t magnitude = pcm_sample < 0 ? -(int32_t)pcm_sample : pcm_sample;
                pcm_absolute_sum += (uint32_t)magnitude;
                if (magnitude > pcm_peak) pcm_peak = magnitude;
                s_pcm_buffer[pcm_length++] = (uint8_t)(pcm_sample & 0xFF);
                s_pcm_buffer[pcm_length++] = (uint8_t)(((uint16_t)pcm_sample >> 8) & 0xFF);
            }
            if (pcm_length >= INMP441_MAX_PCM_BYTES) {
                pcm_length = INMP441_MAX_PCM_BYTES;
                recording = false;
                armed = false;
                ESP_LOGW(TAG, "maximum recording duration reached");
            }
        } else if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        }

        if (!recording && pcm_length > 0) {
            ESP_LOGI(TAG, "recorded %u PCM bytes: output avg=%" PRIu32 ", peak=%" PRId32
                     ", raw L/R avg=%" PRIu32 "/%" PRIu32,
                     (unsigned)pcm_length, (uint32_t)(pcm_absolute_sum / (pcm_length / 2)), pcm_peak,
                     (uint32_t)(left_absolute_sum / slot_sample_count),
                     (uint32_t)(right_absolute_sum / slot_sample_count));
            set_audio_status("REC DONE");
            vTaskDelay(pdMS_TO_TICKS(300));
            set_audio_status("UPLOADING");
            i2s_channel_disable(s_rx_channel);
            ret = tcp_client_upload_pcm(s_pcm_buffer, pcm_length);
            i2s_channel_enable(s_rx_channel);
            if (ret == ESP_OK) {
                set_audio_status("SAVED");
            } else {
                ESP_LOGE(TAG, "PCM upload failed: %s", esp_err_to_name(ret));
                set_audio_status("UPLOAD FAIL");
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            set_audio_status("FREE");
            pcm_length = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t inmp441_audio_test_start(void)
{
    if (s_ptt_task != NULL || s_rx_channel != NULL) return ESP_ERR_INVALID_STATE;
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << INMP441_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&button_config);
    if (ret != ESP_OK) return ret;

    s_pcm_buffer = heap_caps_malloc(INMP441_MAX_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pcm_buffer == NULL) return ESP_ERR_NO_MEM;
    ret = inmp441_i2s_init();
    if (ret != ESP_OK) {
        free(s_pcm_buffer);
        s_pcm_buffer = NULL;
        ESP_LOGE(TAG, "failed to initialize I2S: %s", esp_err_to_name(ret));
        return ret;
    }
    if (xTaskCreate(ptt_task, "inmp441_ptt", 4096, NULL, 5, &s_ptt_task) != pdPASS) {
        i2s_channel_disable(s_rx_channel);
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        free(s_pcm_buffer);
        s_pcm_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "push-to-talk ready: button=GPIO%d, max=%d ms, pcm=%d bytes",
             INMP441_BUTTON_GPIO, INMP441_MAX_RECORD_MS, INMP441_MAX_PCM_BYTES);
    return ESP_OK;
}
