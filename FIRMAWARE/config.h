/*
 * Конфигурационен файл за Floppy PICO
 * Дефинира GPIO пинове и формати
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Формати на диск
// ============================================================================
typedef enum {
    DISK_FORMAT_13_SECTOR = 0,  // DOS 3.3 - 13 сектора на пътека
    DISK_FORMAT_16_SECTOR = 1,  // ProDOS - 16 сектора на пътека
    DISK_FORMAT_AUTO = 2        // Автоматично определяне
} disk_format_t;

// ============================================================================
// GPIO конфигурация
// ============================================================================
typedef struct {
    // Фазови сигнали
    uint8_t ph0;
    uint8_t ph1;
    uint8_t ph2;
    uint8_t ph3;
    
    // Сигнали от Apple II
    uint8_t motor_on;
    uint8_t write_enable;
    uint8_t write_data;
    
    // Сигнали към Apple II
    uint8_t read_data;
    uint8_t track0;
    uint8_t write_protect;
    
    // SD Card SPI
    uint8_t sd_miso;
    uint8_t sd_cs;
    uint8_t sd_sck;
    uint8_t sd_mosi;
    
    // I2C за OLED
    uint8_t i2c_sda;
    uint8_t i2c_scl;
    
    // Енкодер
    uint8_t encoder_a;
    uint8_t encoder_b;
    uint8_t encoder_button;
    
    // LED
    uint8_t led;
} gpio_config_t;

// ============================================================================
// Дискова конфигурация
// ============================================================================
typedef struct {
    disk_format_t format;
    uint8_t sectors_per_track;
    uint16_t bytes_per_sector;
    uint8_t tracks_per_disk;
    const char *format_name;
} disk_config_t;

// ============================================================================
// Глобални конфигурации
// ============================================================================
extern gpio_config_t gpio_config;
extern disk_config_t disk_configs[];
extern disk_config_t *current_disk_config;

// Функции
void load_default_gpio_config(void);
disk_config_t* get_disk_config(disk_format_t format);
bool set_disk_format(disk_format_t format);

// Helper функции за достъп до текущата дискова конфигурация
disk_config_t* get_current_disk_format(void);
uint8_t get_tracks_per_disk(void);
uint8_t get_sectors_per_track(void);
uint16_t get_bytes_per_sector(void);
uint32_t get_track_size(void);

#endif // CONFIG_H

