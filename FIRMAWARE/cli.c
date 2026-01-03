/*
 * CLI (Command Line Interface) имплементация
 */

#include "cli.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "disk_manager.h"

#define UART_ID uart1
#define UART_BAUD_RATE 115200
#define UART_TX_PIN 4  // GPIO 4
#define UART_RX_PIN 5  // GPIO 5

#define CLI_BUFFER_SIZE 128
#define CLI_MAX_ARGS 8

// CLI буфер
static char cli_buffer[CLI_BUFFER_SIZE];
static uint8_t cli_buffer_index = 0;
static bool cli_echo = true;

// Външни променливи (декларирани в Floppy_PICO_green.c)
extern uint8_t current_track;
extern bool motor_on;
extern bool write_protected;
extern bool disk_image_loaded;
extern disk_manager_t disk_manager;
extern gpio_config_t gpio_config;

// Forward декларации за функции от основния файл
extern bool load_track(uint8_t track);
extern void update_display(void);

// GPIO макроси
#define GPIO_WRITE_PROTECT gpio_config.write_protect

// Helper функция за изпращане на низ през UART
static void cli_uart_puts(uart_inst_t *uart, const char *str) {
    if (str == NULL) return;
    while (*str) {
        uart_putc(uart, *str++);
    }
}

// Инициализация на UART за CLI
void cli_init(void) {
    // Инициализация на UART1 (UART0 се използва за stdio)
    uart_init(UART_ID, UART_BAUD_RATE);
    
    // Настройка на GPIO пинове за UART
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Изпращане на приветствено съобщение
    cli_uart_puts(UART_ID, "\r\n=== Apple II Floppy Disk Emulator CLI ===\r\n");
    cli_uart_puts(UART_ID, "Въведете 'help' за списък с команди\r\n");
    cli_uart_puts(UART_ID, "> ");
    
    cli_buffer_index = 0;
    memset(cli_buffer, 0, sizeof(cli_buffer));
    
    printf("CLI инициализиран на UART1 (GPIO %d/%d, %d baud)\n", 
           UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
}

// Парсиране на команда
static void parse_command(char *line, char **argv, int *argc) {
    *argc = 0;
    char *token = strtok(line, " \t\n\r");
    
    while (token != NULL && *argc < CLI_MAX_ARGS) {
        argv[(*argc)++] = token;
        token = strtok(NULL, " \t\n\r");
    }
}

// Изпълнение на команда
static void execute_command(int argc, char **argv) {
    if (argc == 0) return;
    
    char *cmd = argv[0];
    
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cli_print_help();
    }
    else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "stat") == 0) {
        cli_uart_puts(UART_ID, "\r\n=== Статус ===\r\n");
        
        // Мотор
        cli_uart_puts(UART_ID, "Мотор: ");
        cli_uart_puts(UART_ID, motor_on ? "ВКЛЮЧЕН" : "ИЗКЛЮЧЕН");
        cli_uart_puts(UART_ID, "\r\n");
        
        // Пътека
        char buf[64];
        snprintf(buf, sizeof(buf), "Пътека: %d/%d\r\n", current_track, get_tracks_per_disk() - 1);
        cli_uart_puts(UART_ID, buf);
        
        // Диск
        if (disk_image_loaded) {
            const char *disk_name = disk_manager_get_current_name(&disk_manager);
            snprintf(buf, sizeof(buf), "Диск: %s\r\n", disk_name);
            cli_uart_puts(UART_ID, buf);
            
            disk_config_t *format = get_current_disk_format();
            if (format) {
                snprintf(buf, sizeof(buf), "Формат: %s\r\n", format->format_name);
                cli_uart_puts(UART_ID, buf);
            }
        } else {
            cli_uart_puts(UART_ID, "Диск: Не е зареден\r\n");
        }
        
        // Write Protect
        cli_uart_puts(UART_ID, "Write Protect: ");
        cli_uart_puts(UART_ID, write_protected ? "ДА" : "НЕ");
        cli_uart_puts(UART_ID, "\r\n");
    }
    else if (strcmp(cmd, "motor") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "on") == 0) {
                motor_on = true;
                if (load_track(current_track)) {
                    cli_uart_puts(UART_ID, "Мотор ВКЛЮЧЕН\r\n");
                }
            } else if (strcmp(argv[1], "off") == 0) {
                motor_on = false;
                cli_uart_puts(UART_ID, "Мотор ИЗКЛЮЧЕН\r\n");
            } else {
                cli_uart_puts(UART_ID, "Използване: motor on|off\r\n");
            }
        } else {
            cli_uart_puts(UART_ID, motor_on ? "Мотор: ВКЛЮЧЕН\r\n" : "Мотор: ИЗКЛЮЧЕН\r\n");
        }
    }
    else if (strcmp(cmd, "track") == 0) {
        if (argc > 1) {
            int track = atoi(argv[1]);
            if (track >= 0 && track < get_tracks_per_disk()) {
                current_track = track;
                if (motor_on && load_track(current_track)) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Пътека %d заредена\r\n", current_track);
                    cli_uart_puts(UART_ID, buf);
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Пътека зададена на %d\r\n", current_track);
                    cli_uart_puts(UART_ID, buf);
                }
            } else {
                cli_uart_puts(UART_ID, "Невалиден номер на пътека\r\n");
            }
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "Текуща пътека: %d\r\n", current_track);
            cli_uart_puts(UART_ID, buf);
        }
    }
    else if (strcmp(cmd, "disk") == 0) {
        uint8_t count = disk_manager_get_count(&disk_manager);
        
        if (argc > 1) {
            // Избор на диск
            int disk_num = atoi(argv[1]);
            if (disk_num >= 0 && disk_num < count) {
                if (disk_manager_load(&disk_manager, disk_num)) {
                    load_track(current_track);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Диск %d зареден: %s\r\n", 
                            disk_num, disk_manager_get_current_name(&disk_manager));
                    cli_uart_puts(UART_ID, buf);
                } else {
                    cli_uart_puts(UART_ID, "Грешка при зареждане на диск\r\n");
                }
            } else {
                cli_uart_puts(UART_ID, "Невалиден номер на диск\r\n");
            }
        } else {
            // Списък с дискове
            cli_uart_puts(UART_ID, "\r\n=== Налични дискове ===\r\n");
            for (uint8_t i = 0; i < count; i++) {
                disk_image_t *disk = disk_manager_get_disk(&disk_manager, i);
                if (disk) {
                    uint8_t current_idx = disk_manager_get_current_index(&disk_manager);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s%d: %s%s\r\n",
                            (i == current_idx) ? ">" : " ",
                            i, disk->filename,
                            (i == current_idx) ? " [АКТИВЕН]" : "");
                    cli_uart_puts(UART_ID, buf);
                }
            }
        }
    }
    else if (strcmp(cmd, "wprotect") == 0 || strcmp(cmd, "wp") == 0) {
        if (argc > 1) {
            if (strcmp(argv[1], "on") == 0) {
                write_protected = true;
                gpio_put(GPIO_WRITE_PROTECT, 0);
                cli_uart_puts(UART_ID, "Write Protect ВКЛЮЧЕН\r\n");
            } else if (strcmp(argv[1], "off") == 0) {
                write_protected = false;
                gpio_put(GPIO_WRITE_PROTECT, 1);
                cli_uart_puts(UART_ID, "Write Protect ИЗКЛЮЧЕН\r\n");
            } else {
                cli_uart_puts(UART_ID, "Използване: wprotect on|off\r\n");
            }
        } else {
            cli_uart_puts(UART_ID, write_protected ? "Write Protect: ВКЛЮЧЕН\r\n" : "Write Protect: ИЗКЛЮЧЕН\r\n");
        }
    }
    else if (strcmp(cmd, "reset") == 0) {
        cli_uart_puts(UART_ID, "Рестартиране на системата...\r\n");
        // В реална имплементация може да се използва watchdog или software reset
        cli_uart_puts(UART_ID, "Забележка: Рестартирането не е имплементирано\r\n");
    }
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        // Изчистване на екрана (ANSI escape sequence)
        cli_uart_puts(UART_ID, "\033[2J\033[H");
    }
    else {
        char buf[128];
        snprintf(buf, sizeof(buf), "Неизвестна команда: %s\r\nВъведете 'help' за списък с команди\r\n", cmd);
        cli_uart_puts(UART_ID, buf);
    }
}

// Обработка на CLI команди
void cli_process(void) {
    // Проверка за налични данни в UART (обработваме всички налични символи)
    printf("cli PROCESS \n");
    while (uart_is_readable(UART_ID)) {
        printf("cli PROCESS in while \n");
        char c = uart_getc(UART_ID);
        
        // Echo на символа
        if (cli_echo) {
            uart_putc(UART_ID, c);
        }
        
        // Обработка на символа
        if (c == '\r' || c == '\n') {
            if (cli_buffer_index > 0) {
                cli_buffer[cli_buffer_index] = '\0';
                
                // Debug: показване на получената команда
                printf("CLI команда получена: '%s'\n", cli_buffer);
                
                // Парсиране и изпълнение на команда
                char *argv[CLI_MAX_ARGS];
                int argc;
                parse_command(cli_buffer, argv, &argc);
                
                printf("CLI команда парсирана: argc=%d\n", argc);
                
                if (argc > 0) {
                    execute_command(argc, argv);
                }
                
                // Изчистване на буфера
                cli_buffer_index = 0;
                memset(cli_buffer, 0, sizeof(cli_buffer));
                
                cli_uart_puts(UART_ID, "\r\n> ");
            } else {
                cli_uart_puts(UART_ID, "\r\n> ");
            }
        }
        else if (c == '\b' || c == 0x7F) {  // Backspace
            if (cli_buffer_index > 0) {
                cli_buffer_index--;
                cli_buffer[cli_buffer_index] = '\0';
                if (cli_echo) {
                    cli_uart_puts(UART_ID, "\b \b");
                }
            }
        }
        else if (cli_buffer_index < CLI_BUFFER_SIZE - 1) {
            cli_buffer[cli_buffer_index++] = c;
        } else {
            // Буферът е пълен
            cli_uart_puts(UART_ID, "\r\nБуферът е пълен!\r\n> ");
            cli_buffer_index = 0;
            memset(cli_buffer, 0, sizeof(cli_buffer));
        }
    }
}

// Печат на помощна информация
void cli_print_help(void) {
    cli_uart_puts(UART_ID, "\r\n=== CLI Команди ===\r\n");
    cli_uart_puts(UART_ID, "help, ?          - Показва този списък\r\n");
    cli_uart_puts(UART_ID, "status, stat     - Показва статус на системата\r\n");
    cli_uart_puts(UART_ID, "motor [on|off]   - Управление на мотора\r\n");
    cli_uart_puts(UART_ID, "track [num]      - Задава/показва текущата пътека\r\n");
    cli_uart_puts(UART_ID, "disk [num]       - Показва списък или избира диск\r\n");
    cli_uart_puts(UART_ID, "wprotect, wp [on|off] - Управление на write protect\r\n");
    cli_uart_puts(UART_ID, "reset            - Рестартиране на системата\r\n");
    cli_uart_puts(UART_ID, "clear, cls       - Изчистване на екрана\r\n");
    cli_uart_puts(UART_ID, "\r\n");
}

