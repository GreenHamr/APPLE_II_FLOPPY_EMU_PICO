/*
 * CLI (Command Line Interface) за управление през UART
 */

#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stdbool.h>

// Инициализация на CLI
void cli_init(void);

// Обработка на CLI команди (трябва да се извиква периодично)
void cli_process(void);

// Печат на помощна информация
void cli_print_help(void);

#endif // CLI_H

