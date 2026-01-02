/*
 * Управление на множество дискови имиджи
 */

#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include "ff.h"
#include "config.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_DISK_IMAGES 50  // Поддръжка на до 50 дискови имиджи
#define MAX_FILENAME_LEN 32

typedef struct {
    char filename[MAX_FILENAME_LEN];
    disk_format_t format;
    bool loaded;
    FIL file_handle;
    uint32_t file_size;
} disk_image_t;

typedef struct {
    disk_image_t images[MAX_DISK_IMAGES];
    uint8_t count;
    uint8_t current_index;
    bool disk_loaded;
} disk_manager_t;

// Функции
void disk_manager_init(disk_manager_t *dm);
bool disk_manager_scan(disk_manager_t *dm);
bool disk_manager_load(disk_manager_t *dm, uint8_t index);
bool disk_manager_unload(disk_manager_t *dm);
bool disk_manager_next(disk_manager_t *dm);
bool disk_manager_prev(disk_manager_t *dm);
disk_image_t* disk_manager_get_current(disk_manager_t *dm);
const char* disk_manager_get_current_name(disk_manager_t *dm);
uint8_t disk_manager_get_count(disk_manager_t *dm);
disk_image_t* disk_manager_get_disk(disk_manager_t *dm, uint8_t index);
uint8_t disk_manager_get_current_index(disk_manager_t *dm);

#endif // DISK_MANAGER_H

