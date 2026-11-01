#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IIC_OLED_WIDTH               128
#define IIC_OLED_HEIGHT              64
#define IIC_OLED_I2C_ADDRESS_DEFAULT 0x3C
#define IIC_OLED_SDA_GPIO_DEFAULT    GPIO_NUM_4
#define IIC_OLED_SCL_GPIO_DEFAULT    GPIO_NUM_5

/** Configure hardware I2C and initialize a 128x64 SSD1306 OLED. */
esp_err_t iic_oled_init(gpio_num_t sda_gpio, gpio_num_t scl_gpio, uint8_t i2c_address);

/** Release the OLED device and its hardware I2C bus. */
esp_err_t iic_oled_deinit(void);

/** Clear the local 128x64 frame buffer. Call iic_oled_update() to show it. */
void iic_oled_clear(void);

/** Send the complete local frame buffer to the display. */
esp_err_t iic_oled_update(void);

/** Turn one pixel on or off in the local frame buffer. */
void iic_oled_draw_pixel(uint8_t x, uint8_t y, bool on);

/** Draw one 5x7 ASCII character at a pixel location in the local frame buffer. */
void iic_oled_draw_char(uint8_t x, uint8_t y, char character, bool on);

/** Draw an ASCII string at a pixel location in the local frame buffer. */
void iic_oled_draw_string(uint8_t x, uint8_t y, const char *text, bool on);

/** Draw an ASCII string using 6x8 text cells; column 0..20 and row 0..7. */
void iic_oled_write_string(uint8_t column, uint8_t row, const char *text, bool on);

/** Set normal (false) or inverse (true) display mode. */
esp_err_t iic_oled_set_invert(bool invert);

/** Turn the panel on or off. */
esp_err_t iic_oled_set_display_on(bool on);

#ifdef __cplusplus
}
#endif
