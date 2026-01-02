/*
 * Автоматично определяне на номера на сектора при запис
 */

#ifndef SECTOR_DETECTOR_H
#define SECTOR_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

// Структура за секторен адрес в Apple II формат
typedef struct {
    uint8_t track;
    uint8_t sector;
    bool valid;
} sector_address_t;

// Функции
sector_address_t detect_sector_from_data(uint8_t *data, uint32_t data_len, uint8_t current_track);
uint8_t detect_sector_from_gcr(uint8_t *gcr_data, uint32_t gcr_len);
bool parse_dos33_sector_header(uint8_t *data, sector_address_t *addr);
bool parse_prodos_sector_header(uint8_t *data, sector_address_t *addr);

#endif // SECTOR_DETECTOR_H

