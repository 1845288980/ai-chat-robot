# SSD1306 I2C OLED integration plan

## Goal
Add a reusable ESP32-S3 hardware-I2C SSD1306 OLED driver in the existing `iic` component and display `HelloWorld` on the first line at startup.

## Phases

1. **Inspect project and SDK** — complete
2. **Implement reusable SSD1306 component API and rendering** — complete
3. **Integrate startup demo and pin configuration** — complete
4. **Build, correct issues, and document use** — complete
4. **Build, correct issues, and document use** — pending

## Constraints and decisions

- Prefer ESP-IDF hardware I2C.
- Keep OLED functionality in the pre-created `components/iic` component unless the project structure shows a clear reason to split it.
- Default pins will be explicitly configurable and reported to the user.
- GPIO1 and GPIO2 are already used by the LED/EXTI sample, so the OLED defaults will use GPIO4 (SDA) and GPIO5 (SCL).
- ESP-IDF v5.4's new `driver/i2c_master.h` API will be used; this avoids the deprecated I2C driver.
