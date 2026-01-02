/*
 * Rotary Encoder Driver Implementation
 */

#include "encoder.h"
#include "pico/time.h"

// Таблица за декодиране на енкодер (Gray code)
static const int8_t encoder_table[] = {
    0, 1, -1, 0,
    -1, 0, 0, 1,
    1, 0, 0, -1,
    0, -1, 1, 0
};

void encoder_init(encoder_t *enc, uint8_t pin_a, uint8_t pin_b, uint8_t pin_button) {
    enc->pin_a = pin_a;
    enc->pin_b = pin_b;
    enc->pin_button = pin_button;
    enc->position = 0;
    enc->last_position = 0;
    enc->button_pressed = false;
    enc->button_last_state = false;
    enc->state = 0;
    enc->last_state = 0;
    
    gpio_init(pin_a);
    gpio_init(pin_b);
    gpio_init(pin_button);
    gpio_set_dir(pin_a, GPIO_IN);
    gpio_set_dir(pin_b, GPIO_IN);
    gpio_set_dir(pin_button, GPIO_IN);
    gpio_pull_up(pin_a);
    gpio_pull_up(pin_b);
    gpio_pull_up(pin_button);
    
    // Четене на начално състояние
    enc->state = (gpio_get(pin_a) << 1) | gpio_get(pin_b);
    enc->last_state = enc->state;
}

int8_t encoder_read(encoder_t *enc) {
    uint8_t a = gpio_get(enc->pin_a);
    uint8_t b = gpio_get(enc->pin_b);
    enc->state = (a << 1) | b;
    
    int8_t delta = encoder_table[(enc->last_state << 2) | enc->state];
    
    if (delta != 0) {
        enc->position += delta;
        enc->last_state = enc->state;
        return delta;
    }
    
    return 0;
}

bool encoder_button_pressed(encoder_t *enc) {
    bool current_state = !gpio_get(enc->pin_button);  // Active low
    bool pressed = current_state && !enc->button_last_state;
    enc->button_last_state = current_state;
    
    if (pressed) {
        enc->button_pressed = true;
        return true;
    }
    
    return false;
}

