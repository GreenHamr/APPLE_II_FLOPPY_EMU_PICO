/*
 * Interrupt обработка за по-добра производителност
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "config.h"

// Външни променливи (декларирани в Floppy_PICO_green.c)
extern volatile bool phase_change_detected;
extern volatile bool write_data_ready;

// GPIO interrupt handler за фазови промени
static void gpio_irq_handler(uint gpio, uint32_t events) {
    // Проверка за фазови сигнали
    if (gpio == gpio_config.ph0 || gpio == gpio_config.ph1 || 
        gpio == gpio_config.ph2 || gpio == gpio_config.ph3) {
        phase_change_detected = true;
    }
    
    // Проверка за WRITE_DATA промени
    if (gpio == gpio_config.write_data) {
        if (events & GPIO_IRQ_EDGE_RISE || events & GPIO_IRQ_EDGE_FALL) {
            write_data_ready = true;
        }
    }
}

// Инициализация на interrupts
void init_interrupts(void) {
    // Настройка на GPIO interrupts за фазови сигнали
    gpio_set_irq_enabled_with_callback(gpio_config.ph0, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(gpio_config.ph1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(gpio_config.ph2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(gpio_config.ph3, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    
    // Настройка на GPIO interrupt за WRITE_DATA
    gpio_set_irq_enabled_with_callback(gpio_config.write_data, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    
    printf("Interrupts инициализирани\n");
}

