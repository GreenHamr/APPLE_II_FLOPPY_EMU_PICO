/*
 * Rotary Encoder Driver
 */

#ifndef ENCODER_H
#define ENCODER_H

#include "pico/stdlib.h"

typedef struct {
    uint8_t pin_a;
    uint8_t pin_b;
    uint8_t pin_button;
    int32_t position;
    int32_t last_position;
    bool button_pressed;
    bool button_last_state;
    uint8_t state;
    uint8_t last_state;
} encoder_t;

void encoder_init(encoder_t *enc, uint8_t pin_a, uint8_t pin_b, uint8_t pin_button);
int8_t encoder_read(encoder_t *enc);
bool encoder_button_pressed(encoder_t *enc);

#endif // ENCODER_H

