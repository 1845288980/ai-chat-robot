#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the TCP client task that talks to the configured PC server. */
esp_err_t tcp_client_start(void);

#ifdef __cplusplus
}
#endif
