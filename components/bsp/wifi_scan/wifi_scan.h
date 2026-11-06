#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the Wi-Fi STA stack used by the scanner. Call once. */
esp_err_t wifi_scan_init(void);

/** Start connecting to the fixed test Wi-Fi network; reconnection continues in the background. */
esp_err_t wifi_connect_to_ap(void);

/** Return whether the station currently has a valid IP connection. */
bool wifi_scan_is_connected(void);

/** Format the station's current IPv4 address into the provided buffer. */
esp_err_t wifi_scan_get_local_ip(char *buffer, size_t capacity);

/** Scan nearby access points once and show their SSIDs on the initialized OLED. */
esp_err_t wifi_scan_show_on_oled(void);

/** Create the FreeRTOS task that scans and refreshes the OLED every 5 seconds. */
esp_err_t wifi_scan_start_periodic_task(void);

#ifdef __cplusplus
}
#endif
