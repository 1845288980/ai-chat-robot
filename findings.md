# Findings

## Project/SDK inspection

- Target: ESP32-S3 (`CONFIG_IDF_TARGET="esp32s3"`).
- SDK: ESP-IDF v5.4 at `D:/Espressif/frameworks/esp-idf-v5.4`.
- The existing `components/bsp` component already compiles its `iic`, `led`, and `exti` source directories. `iic.c` and `iic.h` are empty, so they are an appropriate home for the OLED driver.
- GPIO1 is occupied by the existing LED test and GPIO2 by its interrupt input. The initial OLED pin assignment will be SDA=GPIO4 and SCL=GPIO5.
- The ESP-IDF I2C OLED example confirms the new hardware-I2C master API (`i2c_new_master_bus`, `i2c_master_bus_add_device`, and `i2c_master_transmit`) works with SSD1306.

## Build diagnosis

- The existing `build/CMakeCache.txt` was created from `D:/espidf_code_myself/1_led`, not this project. A normal build cannot safely reconfigure it; the generated build directory must be cleaned and recreated.
- After regeneration, `idf.py build` completed successfully with the actual `iic/iic.c` object compiled and linked. The final `build/main.bin` is 237,056 bytes and has 88% free space in the application partition.

## External content

ESP-IDF's local `i2c_oled` example and SSD1306 panel implementation were inspected as API and initialization references. No third-party library will be copied into the project.

## Wi-Fi scan reference

- The reference project uses the standard ESP-IDF blocking scan sequence: initialize NVS/netif/event loop, create the default STA netif, initialize/start Wi-Fi in `WIFI_MODE_STA`, call `esp_wifi_scan_start(NULL, true)`, then retrieve AP count and records.
- The port keeps this logic in `components/bsp/wifi_scan`, limiting `main.c` to OLED initialization plus one scan/display call.
- OLED text is ASCII-only; SSIDs are truncated to 18 characters so each numbered line fits the 128x64 display. Hidden SSIDs are shown as `<hidden>`.
- Periodic refresh uses one FreeRTOS task with `xTaskCreate()` and `vTaskDelay(pdMS_TO_TICKS(5000))`; Wi-Fi stack initialization is guarded so it runs only once.
- Serial logs now include the raw SSID, BSSID, RSSI, and channel, which distinguishes two AP records that may have similar display names.
