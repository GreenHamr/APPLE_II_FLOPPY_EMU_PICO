/*
 * Управление на множество дискови имиджи - имплементация
 */

#include "disk_manager.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

// FatFS file access mode definitions (ако не са дефинирани в ff.h)
#ifndef FA_READ
#define FA_READ         0x01
#define FA_WRITE        0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW   0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS  0x10
#define FA_OPEN_APPEND  0x30
#endif

void disk_manager_init(disk_manager_t *dm) {
    memset(dm, 0, sizeof(disk_manager_t));
    dm->current_index = 0;
    dm->disk_loaded = false;
}

bool disk_manager_scan(disk_manager_t *dm) {
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint8_t count = 0;
    
    // Отваряне на директория
    res = f_opendir(&dir, "");
    if (res != FR_OK) {
        return false;
    }
    
    // Търсене на .dsk файлове
    while (count < MAX_DISK_IMAGES) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) {
            break;
        }
        
        // Проверка за .dsk разширение (case-insensitive)
        size_t len = strlen(fno.fname);
        if (len >= 4) {
            const char *ext = fno.fname + len - 4;
            // Проста case-insensitive проверка
            bool is_dsk = (ext[0] == '.' && 
                          (ext[1] == 'd' || ext[1] == 'D') &&
                          (ext[2] == 's' || ext[2] == 'S') &&
                          (ext[3] == 'k' || ext[3] == 'K'));
            
            if (is_dsk) {
                strncpy(dm->images[count].filename, fno.fname, MAX_FILENAME_LEN - 1);
                dm->images[count].filename[MAX_FILENAME_LEN - 1] = '\0';
                dm->images[count].file_size = fno.fsize;
                dm->images[count].format = DISK_FORMAT_AUTO;  // Ще се определи автоматично
                dm->images[count].loaded = false;
                count++;
            }
        }
    }
    
    f_closedir(&dir);
    dm->count = count;
    
    printf("Намерени %d дискови имиджа\n", count);
    return count > 0;
}

bool disk_manager_load(disk_manager_t *dm, uint8_t index) {
    if (index >= dm->count) {
        return false;
    }
    
    // Затваряне на текущия файл ако е отворен
    if (dm->disk_loaded && dm->images[dm->current_index].loaded) {
        f_close(&dm->images[dm->current_index].file_handle);
        dm->images[dm->current_index].loaded = false;
    }
    
    // Отваряне на новия файл
    FRESULT res = f_open(&dm->images[index].file_handle, 
                        dm->images[index].filename, 
                        FA_READ | FA_WRITE);
    if (res != FR_OK) {
        return false;
    }
    
    // Автоматично определяне на формат
    uint32_t file_size = dm->images[index].file_size;
    uint32_t size_13 = 35 * 13 * 256;  // DOS 3.3
    uint32_t size_16 = 35 * 16 * 256;  // ProDOS
    
    if (file_size == size_13) {
        dm->images[index].format = DISK_FORMAT_13_SECTOR;
    } else if (file_size == size_16) {
        dm->images[index].format = DISK_FORMAT_16_SECTOR;
    } else {
        // По подразбиране използваме 16 сектора
        dm->images[index].format = DISK_FORMAT_16_SECTOR;
    }
    
    dm->current_index = index;
    dm->images[index].loaded = true;
    dm->disk_loaded = true;
    
    // Обновяване на конфигурацията
    set_disk_format(dm->images[index].format);
    
    printf("Зареден диск: %s (формат: %s)\n", 
           dm->images[index].filename,
           get_disk_config(dm->images[index].format)->format_name);
    
    return true;
}

bool disk_manager_unload(disk_manager_t *dm) {
    if (!dm->disk_loaded) {
        return false;
    }
    
    if (dm->images[dm->current_index].loaded) {
        f_close(&dm->images[dm->current_index].file_handle);
        dm->images[dm->current_index].loaded = false;
    }
    
    dm->disk_loaded = false;
    return true;
}

bool disk_manager_next(disk_manager_t *dm) {
    if (dm->count == 0) {
        return false;
    }
    
    uint8_t next = (dm->current_index + 1) % dm->count;
    return disk_manager_load(dm, next);
}

bool disk_manager_prev(disk_manager_t *dm) {
    if (dm->count == 0) {
        return false;
    }
    
    uint8_t prev = (dm->current_index - 1 + dm->count) % dm->count;
    return disk_manager_load(dm, prev);
}

disk_image_t* disk_manager_get_current(disk_manager_t *dm) {
    if (!dm->disk_loaded || dm->current_index >= dm->count) {
        return NULL;
    }
    return &dm->images[dm->current_index];
}

const char* disk_manager_get_current_name(disk_manager_t *dm) {
    disk_image_t *img = disk_manager_get_current(dm);
    if (img) {
        return img->filename;
    }
    return "None";
}

uint8_t disk_manager_get_count(disk_manager_t *dm) {
    return dm->count;
}

disk_image_t* disk_manager_get_disk(disk_manager_t *dm, uint8_t index) {
    if (index >= dm->count) {
        return NULL;
    }
    return &dm->images[index];
}

uint8_t disk_manager_get_current_index(disk_manager_t *dm) {
    return dm->current_index;
}

