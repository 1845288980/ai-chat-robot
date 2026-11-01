#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the Wi-Fi STA stack used by the scanner. Call once. */
esp_err_t wifi_scan_init(void);

/** Connect to the fixed test Wi-Fi network and show the result on the OLED. */
esp_err_t wifi_connect_to_ap(void);

/** Scan nearby access points once and show their SSIDs on the initialized OLED. */
esp_err_t wifi_scan_show_on_oled(void);

/** Create the FreeRTOS task that scans and refreshes the OLED every 5 seconds. */
esp_err_t wifi_scan_start_periodic_task(void);

#ifdef __cplusplus
}
#endif
