/*
 * Font 5x7 с поддръжка на ASCII и кирилица (CP1251)
 * Всеки символ е 5 байта ширина, 7 пиксела височина
 */

#ifndef FONT_5X7_H
#define FONT_5X7_H

#include <stdint.h>
#include <stdbool.h>

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_CHAR_SPACING 1  // Интервал между символите

// Функция за получаване на глиф на символ
// Връща указател към 5 байта данни или NULL ако символът не е поддържан
const uint8_t* font_get_glyph(uint8_t ch);

// Проверка дали символът е поддържан
bool font_is_supported(uint8_t ch);

#endif // FONT_5X7_H

