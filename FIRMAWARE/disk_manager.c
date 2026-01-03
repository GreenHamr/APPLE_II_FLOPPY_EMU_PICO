/*
 * Управление на множество дискови имиджи - имплементация
 */

#include "disk_manager.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"

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

// FatFS file attribute definitions
#ifndef AM_RDO
#define AM_RDO  0x01    // Read only
#define AM_HID  0x02    // Hidden
#define AM_SYS  0x04    // System
#define AM_VOL  0x08    // Volume label
#define AM_LFN  0x0F    // Long file name
#define AM_DIR  0x10    // Directory
#define AM_ARC  0x20    // Archive
#define AM_MASK 0x3F    // Mask of defined bits
#endif

void disk_manager_init(disk_manager_t *dm) {
    memset(dm, 0, sizeof(disk_manager_t));
    dm->current_index = 0;
    dm->disk_loaded = false;
    strncpy(dm->current_path, "", MAX_PATH_LEN - 1);
    dm->current_path[MAX_PATH_LEN - 1] = '\0';
}

// Стандартно сканиране (само корнева директория)
bool disk_manager_scan(disk_manager_t *dm) {
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint8_t count = 0;
    uint8_t total_files = 0;
    
    printf("=== Сканиране за .dsk файлове (само корнева директория) ===\n");
    printf("Започване на сканиране...\n");
    printf("ВНИМАНИЕ: Ако виждате FR_NO_FILE веднага, може да има проблем с FatFS имплементацията\n");
    
    // Забавяне преди започване на сканиране за стабилност
    sleep_ms(50);
    
    // Проверка дали файловата система е монтирана
    // (няма директен начин да проверим, но опитът за отваряне на директория ще покаже)
    
#if FF_USE_FIND
    // Използване на f_findfirst/f_findnext за по-надеждно търсене
    res = f_findfirst(&dir, &fno, "", "*.DSK");
    if (res == FR_OK) {
        // Търсене на .DSK файлове (главни букви)
        while (count < MAX_DISK_IMAGES && fno.fname[0] != 0) {
            if (!(fno.fattrib & AM_DIR) && !(fno.fattrib & AM_HID)) {
                strncpy(dm->images[count].filename, fno.fname, MAX_FILENAME_LEN - 1);
                dm->images[count].filename[MAX_FILENAME_LEN - 1] = '\0';
                dm->images[count].file_size = fno.fsize;
                dm->images[count].format = DISK_FORMAT_AUTO;
                dm->images[count].loaded = false;
                printf("Намерен .DSK файл: %s (размер: %lu байта)\n", 
                       dm->images[count].filename, 
                       (unsigned long)dm->images[count].file_size);
                count++;
            }
            res = f_findnext(&dir, &fno);
            if (res != FR_OK) break;
        }
        f_closedir(&dir);
    }
    
    // Търсене на .dsk файлове (малки букви)
    res = f_findfirst(&dir, &fno, "", "*.dsk");
    if (res == FR_OK) {
        while (count < MAX_DISK_IMAGES && fno.fname[0] != 0) {
            if (!(fno.fattrib & AM_DIR) && !(fno.fattrib & AM_HID)) {
                strncpy(dm->images[count].filename, fno.fname, MAX_FILENAME_LEN - 1);
                dm->images[count].filename[MAX_FILENAME_LEN - 1] = '\0';
                dm->images[count].file_size = fno.fsize;
                dm->images[count].format = DISK_FORMAT_AUTO;
                dm->images[count].loaded = false;
                printf("Намерен .dsk файл: %s (размер: %lu байта)\n", 
                       dm->images[count].filename, 
                       (unsigned long)dm->images[count].file_size);
                count++;
            }
            res = f_findnext(&dir, &fno);
            if (res != FR_OK) break;
        }
        f_closedir(&dir);
    }
#endif
    
    // Fallback: Стандартно сканиране на директорията
    printf("Опит за отваряне на корневата директория...\n");
    res = f_opendir(&dir, "");
    if (res != FR_OK) {
        printf("ГРЕШКА: Не може да се отвори директория (код: %d)\n", res);
        switch (res) {
            case FR_DISK_ERR:
                printf("  -> Грешка при достъп до диска\n");
                break;
            case FR_INT_ERR:
                printf("  -> Вътрешна грешка на файловата система\n");
                break;
            case FR_NOT_READY:
                printf("  -> Дискът не е готов\n");
                break;
            case FR_NO_FILESYSTEM:
                printf("  -> Няма валидна FAT файлова система\n");
                break;
            default:
                printf("  -> Непозната грешка\n");
                break;
        }
        if (count > 0) {
            dm->count = count;
            printf("Намерени са %d файла преди грешката\n", count);
            return true;
        }
        return false;
    }
    
    printf("Директорията е отворена успешно. Започване на четене на файлове...\n");
    
    // Забавяне преди започване на четене
    sleep_ms(10);
    
    // Търсене на .dsk файлове
    while (count < MAX_DISK_IMAGES) {
        // Забавяне между четенията за стабилност
        sleep_ms(5);
        
        res = f_readdir(&dir, &fno);
        printf("f_readdir резултат: код=%d", res);
        if (res == FR_OK) {
            printf(", име: '%s', размер: %lu\n", fno.fname, (unsigned long)fno.fsize);
        } else {
            printf("\n");
        }
        
        if (res != FR_OK) {
            // FR_NO_FILE (4) може да означава край на директорията
            if (res == FR_NO_FILE) {
                // Проверка дали това е първото четене (директорията може да е празна)
                if (total_files == 0) {
                    printf("ВНИМАНИЕ: Директорията изглежда е празна или има проблем с четенето\n");
                    printf("  Това може да означава:\n");
                    printf("  1. Директорията наистина е празна\n");
                    printf("  2. Проблем с FatFS имплементацията (опростена версия?)\n");
                    printf("  3. Проблем с файловата система на SD картата\n");
                    printf("  Опитваме се с по-дълго забавяне...\n");
                    sleep_ms(100);
                    res = f_readdir(&dir, &fno);
                    if (res == FR_OK && fno.fname[0] != 0) {
                        printf("  Повторен опит успешен! Продължаваме...\n");
                        // Продължаваме с обработката на файла
                    } else {
                        printf("  Повторен опит също неуспешен (код: %d)\n", res);
                        printf("  ВАЖНО: Проверете дали използвате ПЪЛНАТА FatFS библиотека, не опростената версия!\n");
                        printf("  Директорията е празна или има проблем с файловата система\n");
                        break;
                    }
                } else {
                    printf("Край на директорията (FR_NO_FILE) - прочетени %d елемента\n", total_files);
                    break;
                }
            } else {
                printf("ГРЕШКА при четене на директория (код: %d)\n", res);
                // При други грешки опитваме се още веднъж
                sleep_ms(20);
                res = f_readdir(&dir, &fno);
                if (res == FR_OK && fno.fname[0] != 0) {
                    printf("  Повторен опит успешен, продължаваме\n");
                    // Продължаваме с обработката
                } else {
                    printf("  Повторен опит също неуспешен (код: %d), спиране\n", res);
                    break;
                }
            }
        }
        
        // Край на директорията (нормален случай)
        if (fno.fname[0] == 0) {
            printf("Край на директорията (празно име) - прочетени %d елемента\n", total_files);
            break;
        }
        
        total_files++;
        printf("[%d] Намерен елемент: '%s' (атрибути: 0x%02X, размер: %lu)\n", 
               total_files, fno.fname, fno.fattrib, (unsigned long)fno.fsize);
        
        // Пропускане на директории и скрити файлове
        if (fno.fattrib & AM_DIR) {
            printf("  -> Пропусната директория: %s\n", fno.fname);
            continue;
        }
        
        // Пропускане на скрити файлове
        if (fno.fattrib & AM_HID) {
            printf("  -> Пропуснат скрит файл: %s\n", fno.fname);
            continue;
        }
        
        // Проверка за .dsk разширение (case-insensitive)
        size_t len = strlen(fno.fname);
        printf("  -> Дължина на името: %zu\n", len);
        if (len < 4) {
            printf("  -> Пропуснат файл (твърде кратко име): %s\n", fno.fname);
            continue;
        }
        
        const char *ext = fno.fname + len - 4;
        printf("  -> Разширение: '%s'\n", ext);
        // Проста case-insensitive проверка
        bool is_dsk = (ext[0] == '.' && 
                      (ext[1] == 'd' || ext[1] == 'D') &&
                      (ext[2] == 's' || ext[2] == 'S') &&
                      (ext[3] == 'k' || ext[3] == 'K'));
        
        printf("  -> Е .dsk файл: %s\n", is_dsk ? "ДА" : "НЕ");
        
        if (is_dsk) {
            // Проверка дали файлът вече не е добавен (от f_findfirst)
            bool already_added = false;
            for (uint8_t i = 0; i < count; i++) {
                if (strcmp(dm->images[i].filename, fno.fname) == 0) {
                    already_added = true;
                    break;
                }
            }
            
            if (!already_added) {
                strncpy(dm->images[count].filename, fno.fname, MAX_FILENAME_LEN - 1);
                dm->images[count].filename[MAX_FILENAME_LEN - 1] = '\0';
                dm->images[count].file_size = fno.fsize;
                dm->images[count].format = DISK_FORMAT_AUTO;  // Ще се определи автоматично
                dm->images[count].loaded = false;
                printf("Намерен .dsk файл: %s (размер: %lu байта)\n", 
                       dm->images[count].filename, 
                       (unsigned long)dm->images[count].file_size);
                count++;
            }
        } else {
            printf("Пропуснат файл (не е .dsk): %s\n", fno.fname);
        }
    }
    
    f_closedir(&dir);
    dm->count = count;
    
    printf("Общо файлове в директорията: %d\n", total_files);
    printf("Намерени %d дискови имиджа\n", count);
    
    if (count == 0 && total_files > 0) {
        printf("ПРЕДУПРЕЖДЕНИЕ: Намерени са файлове, но никой не е .dsk файл!\n");
        printf("Моля, проверете че файловете имат разширение .dsk (малки или главни букви)\n");
    } else if (count == 0 && total_files == 0) {
        printf("ПРЕДУПРЕЖДЕНИЕ: Директорията е празна или не може да се прочете!\n");
    }
    
    return count > 0;
}

bool disk_manager_load(disk_manager_t *dm, uint8_t index) {
    if (index >= dm->count) {
        printf("DEBUG disk_manager_load: index %d >= count %d\n", index, dm->count);
        return false;
    }
    
    printf("DEBUG disk_manager_load: Опит за зареждане на диск %d: %s\n", index, dm->images[index].filename);
    
    // Затваряне на текущия файл ако е отворен
    if (dm->disk_loaded && dm->images[dm->current_index].loaded) {
        f_close(&dm->images[dm->current_index].file_handle);
        dm->images[dm->current_index].loaded = false;
    }
    
    // Отваряне на новия файл
    FRESULT res = f_open(&dm->images[index].file_handle, 
                        dm->images[index].filename, 
                        FA_READ | FA_WRITE);
    printf("DEBUG disk_manager_load: f_open res %d FR_OK %d\n", res, FR_OK);
    if (res != FR_OK) {
        printf("DEBUG disk_manager_load: f_open FAILED с код %d за файл: %s\n", res, dm->images[index].filename);
        return false;
    }
    
    printf("DEBUG disk_manager_load: f_open успешен за файл: %s\n", dm->images[index].filename);
    
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

// Рекурсивно сканиране на директории за .dsk файлове
static void scan_directory_recursive(disk_manager_t *dm, const char *path, uint8_t *count) {
    FRESULT res;
    DIR dir;
    FILINFO fno;
    char full_path[MAX_PATH_LEN];
    char sub_path[MAX_PATH_LEN];
    uint8_t items_in_dir = 0;
    
    printf(">>> Сканиране на директория: '%s'\n", path ? path : "(root)");
    
    // Отваряне на директорията
    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        printf("  ГРЕШКА: Не може да се отвори директория '%s' (код: %d)\n", path ? path : "(root)", res);
        return;
    }
    
    printf("  Директорията е отворена успешно\n");
    
    // Забавяне преди започване на четене
    sleep_ms(10);
    
    // Четене на всички елементи в директорията
    while (*count < MAX_DISK_IMAGES) {
        // Забавяне между четенията за стабилност
        sleep_ms(5);
        
        res = f_readdir(&dir, &fno);
        if (res != FR_OK) {
            printf("  f_readdir върна код: %d\n", res);
            if (res == FR_NO_FILE) {
                printf("  Край на директорията (FR_NO_FILE) - прочетени %d елемента\n", items_in_dir);
                break;
            }
            // При други грешки опитваме се още веднъж с по-дълго забавяне
            sleep_ms(20);
            res = f_readdir(&dir, &fno);
            if (res != FR_OK) {
                printf("  Повторен опит също неуспешен (код: %d), спиране\n", res);
                break;
            }
        }
        
        if (fno.fname[0] == 0) {
            printf("  Край на директорията (празно име) - прочетени %d елемента\n", items_in_dir);
            break;
        }
        
        items_in_dir++;
        
        // Конструиране на пълен път
        if (strlen(path) == 0) {
            snprintf(full_path, MAX_PATH_LEN, "%s", fno.fname);
        } else {
            snprintf(full_path, MAX_PATH_LEN, "%s/%s", path, fno.fname);
        }
        
        printf("  [%d] Елемент: '%s' (път: '%s', атрибути: 0x%02X, размер: %lu)\n", 
               items_in_dir, fno.fname, full_path, fno.fattrib, (unsigned long)fno.fsize);
        
        // Пропускане на скрити файлове и системни файлове
        if (fno.fattrib & (AM_HID | AM_SYS | AM_VOL)) {
            printf("    -> Пропуснат (скрит/системен/volume label)\n");
            continue;
        }
        
        // Проверка дали е директория
        if (fno.fattrib & AM_DIR) {
            printf("    -> Директория, рекурсивно сканиране...\n");
            // Рекурсивно сканиране на поддиректорията
            scan_directory_recursive(dm, full_path, count);
        } else {
            printf("    -> Файл\n");
            // Проверка за .dsk разширение
            size_t len = strlen(fno.fname);
            printf("    -> Дължина на името: %zu\n", len);
            if (len >= 4) {
                const char *ext = fno.fname + len - 4;
                printf("    -> Разширение: '%s'\n", ext);
                bool is_dsk = (ext[0] == '.' && 
                              (ext[1] == 'd' || ext[1] == 'D') &&
                              (ext[2] == 's' || ext[2] == 'S') &&
                              (ext[3] == 'k' || ext[3] == 'K'));
                
                printf("    -> Е .dsk файл: %s\n", is_dsk ? "ДА" : "НЕ");
                
                if (is_dsk) {
                    strncpy(dm->images[*count].filename, full_path, MAX_FILENAME_LEN - 1);
                    dm->images[*count].filename[MAX_FILENAME_LEN - 1] = '\0';
                    dm->images[*count].file_size = fno.fsize;
                    dm->images[*count].format = DISK_FORMAT_AUTO;
                    dm->images[*count].loaded = false;
                    printf("    *** НАМЕРЕН .dsk ФАЙЛ: %s (размер: %lu байта) ***\n", 
                           full_path, (unsigned long)fno.fsize);
                    (*count)++;
                }
            } else {
                printf("    -> Пропуснат (името е твърде кратко)\n");
            }
        }
    }
    
    f_closedir(&dir);
    printf("<<< Завършено сканиране на директория '%s' (намерени %d .dsk файла общо)\n", 
           path ? path : "(root)", *count);
}

// Рекурсивно сканиране на всички поддиректории
bool disk_manager_scan_recursive(disk_manager_t *dm, const char *path) {
    uint8_t count = 0;
    
    printf("========================================\n");
    printf("=== РЕКУРСИВНО СКАНИРАНЕ ЗА .DSK ФАЙЛОВЕ ===\n");
    printf("Път: %s\n", path ? path : "(root)");
    printf("========================================\n");
    
    scan_directory_recursive(dm, path ? path : "", &count);
    
    dm->count = count;
    printf("========================================\n");
    printf("=== РЕЗУЛТАТ: Намерени %d дискови имиджа (рекурсивно) ===\n", count);
    printf("========================================\n");
    
    return count > 0;
}

// Получаване на текущия път
const char* disk_manager_get_current_path(disk_manager_t *dm) {
    return dm->current_path;
}

// Задаване на текущ път
bool disk_manager_set_path(disk_manager_t *dm, const char *path) {
    if (path == NULL) {
        dm->current_path[0] = '\0';
        return true;
    }
    
    if (strlen(path) >= MAX_PATH_LEN) {
        return false;
    }
    
    strncpy(dm->current_path, path, MAX_PATH_LEN - 1);
    dm->current_path[MAX_PATH_LEN - 1] = '\0';
    return true;
}

// Списък на елементите в директория (файлове и поддиректории)
bool disk_manager_list_directory(disk_manager_t *dm, const char *path, 
                                  char items[][MAX_FILENAME_LEN], 
                                  bool is_dir[], 
                                  uint8_t *count, 
                                  uint8_t max_items) {
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint8_t item_count = 0;
    
    // Добавяне на опция за връщане назад (ако не сме в корневата директория)
    if (path != NULL && strlen(path) > 0) {
        strncpy(items[item_count], "..", MAX_FILENAME_LEN - 1);
        items[item_count][MAX_FILENAME_LEN - 1] = '\0';
        is_dir[item_count] = true;
        item_count++;
    }
    
    // Отваряне на директорията
    res = f_opendir(&dir, path ? path : "");
    if (res != FR_OK) {
        *count = item_count;
        return item_count > 0;  // Връщаме true ако имаме поне ".."
    }
    
    // Четене на всички елементи
    while (item_count < max_items) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) {
            break;
        }
        
        // Пропускане на скрити файлове и системни файлове
        if (fno.fattrib & (AM_HID | AM_SYS | AM_VOL)) {
            continue;
        }
        
        // Проверка дали е директория
        is_dir[item_count] = (fno.fattrib & AM_DIR) != 0;
        
        // Копиране на името
        strncpy(items[item_count], fno.fname, MAX_FILENAME_LEN - 1);
        items[item_count][MAX_FILENAME_LEN - 1] = '\0';
        
        item_count++;
    }
    
    f_closedir(&dir);
    *count = item_count;
    
    return true;
}

