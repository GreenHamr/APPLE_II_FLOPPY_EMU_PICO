/*
 * SSD1306 OLED Display Driver Implementation
 */

#include "ssd1306.h"
#include "font_5x7.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static i2c_inst_t *i2c_instance = NULL;
static uint8_t framebuffer[SSD1306_WIDTH * SSD1306_PAGES];

static void ssd1306_write_command(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};  // Control byte + command
    i2c_write_blocking(i2c_instance, SSD1306_I2C_ADDR, buf, 2, false);
}

static void ssd1306_write_data(uint8_t *data, size_t len) {
    uint8_t *buf = (uint8_t *)malloc(len + 1);
    buf[0] = 0x40;  // Data mode
    memcpy(buf + 1, data, len);
    i2c_write_blocking(i2c_instance, SSD1306_I2C_ADDR, buf, len + 1, false);
    free(buf);
}

void ssd1306_init(i2c_inst_t *i2c, uint8_t sda, uint8_t scl) {
    i2c_instance = i2c;
    
    i2c_init(i2c, 400000);  // 400 kHz
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
    
    // Инициализация на дисплея
    ssd1306_write_command(SSD1306_DISPLAYOFF);
    ssd1306_write_command(SSD1306_SETDISPLAYCLOCKDIV);
    ssd1306_write_command(0x80);
    ssd1306_write_command(SSD1306_SETMULTIPLEX);
    ssd1306_write_command(SSD1306_HEIGHT - 1);
    ssd1306_write_command(SSD1306_SETDISPLAYOFFSET);
    ssd1306_write_command(0x0);
    ssd1306_write_command(SSD1306_SETSTARTLINE | 0x0);
    ssd1306_write_command(SSD1306_CHARGEPUMP);
    ssd1306_write_command(0x14);
    ssd1306_write_command(SSD1306_MEMORYMODE);
    ssd1306_write_command(0x00);
    ssd1306_write_command(SSD1306_SEGREMAP | 0x1);
    ssd1306_write_command(SSD1306_COMSCANDEC);
    ssd1306_write_command(SSD1306_SETCOMPINS);
    ssd1306_write_command(0x12);
    ssd1306_write_command(SSD1306_SETCONTRAST);
    ssd1306_write_command(0xCF);
    ssd1306_write_command(SSD1306_SETPRECHARGE);
    ssd1306_write_command(0xF1);
    ssd1306_write_command(SSD1306_SETVCOMDETECT);
    ssd1306_write_command(0x40);
    ssd1306_write_command(SSD1306_DISPLAYALLON_RESUME);
    ssd1306_write_command(SSD1306_NORMALDISPLAY);
    ssd1306_write_command(SSD1306_DISPLAYON);
    
    ssd1306_clear();
    ssd1306_update();
}

void ssd1306_clear(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
}

void ssd1306_update(void) {
    ssd1306_write_command(SSD1306_COLUMNADDR);
    ssd1306_write_command(0);
    ssd1306_write_command(SSD1306_WIDTH - 1);
    ssd1306_write_command(SSD1306_PAGEADDR);
    ssd1306_write_command(0);
    ssd1306_write_command(SSD1306_PAGES - 1);
    
    ssd1306_write_data(framebuffer, sizeof(framebuffer));
}

void ssd1306_set_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    
    uint16_t index = x + (y / 8) * SSD1306_WIDTH;
    uint8_t bit = y % 8;
    
    if (on) {
        framebuffer[index] |= (1 << bit);
    } else {
        framebuffer[index] &= ~(1 << bit);
    }
}

void ssd1306_draw_char(uint8_t x, uint8_t y, char c) {
    // Получаване на глиф на символа
    const uint8_t *glyph = font_get_glyph((uint8_t)c);
    
    if (!glyph) {
        glyph = font_get_glyph('?');  // Fallback на '?'
    }
    
    // Рисуване на глифа (всеки байт е колона, битовете от долу нагоре)
    for (int i = 0; i < FONT_WIDTH; i++) {
        uint8_t column = glyph[i];
        for (int j = 0; j < FONT_HEIGHT; j++) {
            bool pixel = (column >> j) & 0x01;
            ssd1306_set_pixel(x + i, y + j, pixel);
        }
    }
}

void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str) {
    uint8_t pos_x = x;
    while (*str && pos_x < SSD1306_WIDTH - FONT_WIDTH) {
        ssd1306_draw_char(pos_x, y, *str++);
        pos_x += FONT_WIDTH + FONT_CHAR_SPACING;  // Ширина на символа + интервал
    }
}

void ssd1306_draw_string_scroll(uint8_t x, uint8_t y, const char *str, uint8_t max_width) {
    size_t len = strlen(str);
    uint8_t char_width = FONT_WIDTH + FONT_CHAR_SPACING;
    
    if (len * char_width <= max_width) {
        ssd1306_draw_string(x, y, str);
    } else {
        // Скролиране на текста
        static uint8_t scroll_pos = 0;
        char display_str[22];
        uint8_t chars_to_show = max_width / char_width;
        if (scroll_pos + chars_to_show > len) {
            scroll_pos = 0;
        }
        strncpy(display_str, str + scroll_pos, chars_to_show);
        display_str[chars_to_show] = '\0';
        ssd1306_draw_string(x, y, display_str);
        scroll_pos++;
    }
}

