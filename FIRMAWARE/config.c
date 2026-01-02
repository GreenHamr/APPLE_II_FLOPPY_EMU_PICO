/*
 * Конфигурационна имплементация
 */

#include "config.h"

// Конфигурация по подразбиране
gpio_config_t gpio_config = {
    .ph0 = 3,
    .ph1 = 4,
    .ph2 = 5,
    .ph3 = 6,
    .motor_on = 7,
    .write_enable = 8,
    .write_data = 9,
    .read_data = 10,
    .track0 = 11,
    .write_protect = 12,

    .encoder_a = 13,
    .encoder_b = 14,
    .encoder_button = 15,

    .sd_miso = 16,
    .sd_cs = 17,
    .sd_sck = 18,
    .sd_mosi = 19,

    .i2c_sda = 20,
    .i2c_scl = 21,

    .led = 25
};

// Дискови конфигурации
disk_config_t disk_configs[] = {
    {
        .format = DISK_FORMAT_13_SECTOR,
        .sectors_per_track = 13,
        .bytes_per_sector = 256,
        .tracks_per_disk = 35,
        .format_name = "DOS 3.3"
    },
    {
        .format = DISK_FORMAT_16_SECTOR,
        .sectors_per_track = 16,
        .bytes_per_sector = 256,
        .tracks_per_disk = 35,
        .format_name = "ProDOS"
    }
};

disk_config_t *current_disk_config = &disk_configs[DISK_FORMAT_16_SECTOR];

void load_default_gpio_config(void) {
    // Конфигурацията вече е инициализирана статично
}

disk_config_t* get_disk_config(disk_format_t format) {
    if (format < DISK_FORMAT_13_SECTOR || format > DISK_FORMAT_16_SECTOR) {
        return &disk_configs[DISK_FORMAT_16_SECTOR];  // По подразбиране
    }
    return &disk_configs[format];
}

bool set_disk_format(disk_format_t format) {
    if (format == DISK_FORMAT_AUTO) {
        // Автоматично определяне - за сега използваме 16 сектора
        current_disk_config = &disk_configs[DISK_FORMAT_16_SECTOR];
        return true;
    }
    
    if (format >= DISK_FORMAT_13_SECTOR && format <= DISK_FORMAT_16_SECTOR) {
        current_disk_config = &disk_configs[format];
        return true;
    }
    
    return false;
}

// Helper функции за достъп до текущата дискова конфигурация
disk_config_t* get_current_disk_format(void) {
    return current_disk_config;
}

uint8_t get_tracks_per_disk(void) {
    if (current_disk_config) {
        return current_disk_config->tracks_per_disk;
    }
    return 35;  // По подразбиране
}

uint8_t get_sectors_per_track(void) {
    if (current_disk_config) {
        return current_disk_config->sectors_per_track;
    }
    return 16;  // По подразбиране
}

uint16_t get_bytes_per_sector(void) {
    if (current_disk_config) {
        return current_disk_config->bytes_per_sector;
    }
    return 256;  // По подразбиране
}

uint32_t get_track_size(void) {
    if (current_disk_config) {
        return (uint32_t)current_disk_config->sectors_per_track * 
               (uint32_t)current_disk_config->bytes_per_sector;
    }
    return 16 * 256;  // По подразбиране
}

