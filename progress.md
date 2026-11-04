# Progress log

- 2026-10-31: Started SSD1306 hardware-I2C OLED integration. Created tracking files; project inspection is next.
- 2026-10-31: Inspected the project, target, existing GPIO allocation, and bundled ESP-IDF OLED/I2C examples. Selected the existing `components/bsp/iic` directory and hardware I2C on GPIO4/GPIO5.
- 2026-10-31: Implemented the reusable SSD1306 driver in the existing `iic` directory. It uses hardware I2C0 at 400 kHz, a 128x64 frame buffer, ASCII 5x7 text, pixel rendering, display on/off, and inversion. Updated startup code to display `HelloWorld` at text row 0.
- 2026-10-31: First build attempt identified a stale CMake cache copied from the `1_led` project. Next action: regenerate only this project's build directory and compile.
- 2026-10-31: Regenerated the stale build cache and completed `idf.py build` successfully. The resulting `build/main.bin` is 0x3a000 bytes and fits the firmware partition with 88% free space.
- 2026-10-31: Corrected character rendering so drawing text in the `off` color only changes foreground glyph pixels. Rebuilt successfully; `iic/iic.c` was compiled and linked into the final firmware.
- 2026-11-01: Inspected the reference Wi-Fi scan project; added reusable `components/bsp/wifi_scan` module using the NVS/netif/STA/blocking-scan/AP-record flow. `main.c` now initializes OLED and calls `wifi_scan_show_on_oled()`.
- 2026-11-01: First build exposed a missing `esp_check.h` include for `ESP_RETURN_ON_ERROR`; added it. Rebuilt successfully with ESP-IDF v5.4 (`main.bin` generated, 62% app partition free).
- 2026-11-01: Converted scanning to a periodic FreeRTOS task. `wifi_scan_init()` runs once, `wifi_scan_task()` scans/updates OLED then delays 5 seconds, and logs SSID/BSSID/RSSI/channel. Build passed; app partition still has 62% free.
- 2026-11-01: Added fixed-credential STA connection experiment using SSID `Gang`. OLED shows connecting, connected with IP, or failed after 5 retries. Compared with reference `14_WiFi_STA`; build passed and generated `build/main.bin`.
- 2026-11-01: Added TCP client component targeting PC `192.168.1.19:5000`, plus `tools/tcp_server.py`. ESP32 build passed (`main.bin`, 61% app partition free); Python syntax and loopback ACK test passed.
- 2026-11-02: Diagnosed TCP failure: PC DHCP address changed from `192.168.1.19` to `192.168.1.142` while firmware still targeted the old address. PC is now Private, port 5000 is listening, and local Test-NetConnection succeeds. Updated TCP target to `192.168.1.142`; rebuild and reflash required.
- 2026-11-02: Rebuilt after updating the TCP target to `192.168.1.142`; build passed and generated `build/main.bin` (61% app partition free). Awaiting board flash/runtime verification.
