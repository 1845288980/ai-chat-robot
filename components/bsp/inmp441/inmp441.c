#include "inmp441.h"

#include <inttypes.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INMP441_SAMPLE_RATE_HZ 16000
#define INMP441_READ_BUFFER_BYTES 1024
#define INMP441_LOG_PERIOD_MS 2000

static const char *TAG = "inmp441";
static i2s_chan_handle_t s_rx_channel;
static TaskHandle_t s_audio_test_task;

static esp_err_t inmp441_i2s_init(void)
{
    const i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&channel_config, NULL, &s_rx_channel);
    if (ret != ESP_OK) {
        return ret;
    }

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
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_channel, &standard_config);
    if (ret != ESP_OK) {
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_rx_channel);
    if (ret != ESP_OK) {
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
    }
    return ret;
}

static void inmp441_audio_test_task(void *arg)
{
    (void)arg;
    int32_t raw_samples[INMP441_READ_BUFFER_BYTES / sizeof(int32_t)];
    uint64_t absolute_sum = 0;
    uint32_t sample_count = 0;
    int32_t peak = 0;
    TickType_t report_at = xTaskGetTickCount() + pdMS_TO_TICKS(INMP441_LOG_PERIOD_MS);

    while (true) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_channel, raw_samples, sizeof(raw_samples),
                                         &bytes_read, 1000);
        if (ret == ESP_OK) {
            size_t word_count = bytes_read / sizeof(raw_samples[0]);
            // The L/R pin is tied low, so the microphone sends audio in the left slot.
            for (size_t i = 0; i + 1 < word_count; i += 2) {
                int32_t sample = raw_samples[i] >> 8;
                int64_t magnitude = sample < 0 ? -(int64_t)sample : sample;
                absolute_sum += (uint64_t)magnitude;
                if (magnitude > peak) {
                    peak = (int32_t)magnitude;
                }
                ++sample_count;
            }
        } else {
            ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        }

        if ((int32_t)(xTaskGetTickCount() - report_at) >= 0) {
            uint32_t average = sample_count == 0 ? 0 :
                (uint32_t)(absolute_sum / sample_count);
            ESP_LOGI(TAG, "audio level: avg_abs=%" PRIu32 ", peak=%" PRId32
                     " (16 kHz, left channel)", average, peak);
            absolute_sum = 0;
            sample_count = 0;
            peak = 0;
            report_at = xTaskGetTickCount() + pdMS_TO_TICKS(INMP441_LOG_PERIOD_MS);
        }
    }
}

esp_err_t inmp441_audio_test_start(void)
{
    if (s_audio_test_task != NULL || s_rx_channel != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = inmp441_i2s_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    if (xTaskCreate(inmp441_audio_test_task, "inmp441_test", 4096, NULL, 5,
                    &s_audio_test_task) != pdPASS) {
        i2s_channel_disable(s_rx_channel);
        i2s_del_channel(s_rx_channel);
        s_rx_channel = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "I2S RX started: BCLK=GPIO%d WS=GPIO%d SD=GPIO%d, %d Hz",
             INMP441_I2S_BCLK_GPIO, INMP441_I2S_WS_GPIO, INMP441_I2S_DOUT_GPIO,
             INMP441_SAMPLE_RATE_HZ);
    return ESP_OK;
}
