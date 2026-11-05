#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INMP441_I2S_BCLK_GPIO GPIO_NUM_6
#define INMP441_I2S_WS_GPIO   GPIO_NUM_7
#define INMP441_I2S_DOUT_GPIO GPIO_NUM_15

/** Start the INMP441 capture and GPIO17 push-to-talk task. */
esp_err_t inmp441_audio_test_start(void);

#ifdef __cplusplus
}
#endif
