#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the TCP client task that talks to the configured PC server. */
esp_err_t tcp_client_start(void);

/** Queue PCM for transmission and wait for the PC save result. */
esp_err_t tcp_client_upload_pcm(const uint8_t *pcm, size_t length);

#ifdef __cplusplus
}
#endif
