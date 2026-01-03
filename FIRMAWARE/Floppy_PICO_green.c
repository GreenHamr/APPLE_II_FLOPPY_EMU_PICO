/*
 * Apple II Floppy Disk Emulator for Raspberry Pi Pico
 * Симулира флопи дисково устройство с 16 сектора на пътека
 * Чете дискови имиджи от SD карта
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "ff.h"
#include "diskio.h"
#include "read_data.pio.h"
#include "write_data.pio.h"
#include "ssd1306.h"
#include "encoder.h"
#include "hardware/i2c.h"
#include "config.h"
#include "disk_manager.h"
#include "sector_detector.h"
#include "cli.h"

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

// ============================================================================
// GPIO Pin Definitions (използват се от конфигурацията)
// ============================================================================
#define GPIO_PH0 gpio_config.ph0
#define GPIO_PH1 gpio_config.ph1
#define GPIO_PH2 gpio_config.ph2
#define GPIO_PH3 gpio_config.ph3
#define GPIO_MOTOR_ON gpio_config.motor_on
#define GPIO_WRITE_ENABLE gpio_config.write_enable
#define GPIO_WRITE_DATA gpio_config.write_data
#define GPIO_READ_DATA gpio_config.read_data
#define GPIO_TRACK0 gpio_config.track0
#define GPIO_WRITE_PROTECT gpio_config.write_protect
#define PIN_MISO gpio_config.sd_miso
#define PIN_CS gpio_config.sd_cs
#define PIN_SCK gpio_config.sd_sck
#define PIN_MOSI gpio_config.sd_mosi
#define I2C_SDA gpio_config.i2c_sda
#define I2C_SCL gpio_config.i2c_scl
#define ENCODER_PIN_A gpio_config.encoder_a
#define ENCODER_PIN_B gpio_config.encoder_b
#define ENCODER_BUTTON gpio_config.encoder_button
#define LED_PIN gpio_config.led

// SD Card SPI
spi_inst_t* SPI_PORT = spi0;
#define I2C_PORT i2c0

// ============================================================================
// Константи (сега се използват от конфигурацията)
// ============================================================================
#define TRACKS_PER_DISK 35  // Общо за всички формати

// GCR константи
#define GCR_BITS_PER_BYTE 10  // 8 данни + 2 синхронизация
#define GCR_CLOCK_RATE 125000  // 125 kHz за Apple II

// Времеви константи (в микросекунди)
#define STEP_DELAY_US 3000     // Забавяне между стъпки
#define TRACK_0_DELAY_US 100   // Забавяне за TRACK0 детекция

// Размер на дисковия имидж се определя динамично от disk_manager

// ============================================================================
// Глобални променливи
// ============================================================================
uint8_t current_track = 0;
bool motor_on = false;
bool write_protected = false;  // Разрешено запис (може да се конфигурира)
static uint8_t disk_image_buffer[35 * 16 * 256];  // Буфер за текущата пътека (максимален размер)
bool disk_image_loaded = false;
disk_manager_t disk_manager;  // Управление на множество дискови имиджи
static FATFS fs;       // FatFS файлова система обект
static bool sd_card_present = false;  // Статус на SD картата
static bool sd_is_sdhc = false;      // Дали SD картата е SDHC/SDXC (true) или SDSC (false)
static uint32_t last_sd_check = 0;   // Последна проверка за SD карта
#define SD_CHECK_INTERVAL_MS 1000     // Проверка на всеки 1 секунда

// PIO и DMA променливи
static PIO pio_read = pio0;
static PIO pio_write = pio1;
static uint sm_read = 0;
static uint sm_write = 0;
static uint offset_read = 0;
static uint offset_write = 0;
static int dma_channel_read = -1;
static int dma_channel_write = -1;

// Буфери за запис
static uint8_t write_buffer[256];  // Буфер за текущия сектор при запис (максимален размер)
static bool write_in_progress = false;
static uint8_t current_write_sector = 0;
static uint32_t write_bit_count = 0;
static uint8_t write_gcr_buffer[2];  // Буфер за GCR данни
static uint8_t write_gcr_index = 0;
static uint8_t write_fifo_buffer[256];  // Буфер за данни от PIO FIFO

// Interrupt обработка променливи
volatile bool phase_change_detected = false;
volatile bool write_data_ready = false;

// UI променливи
static encoder_t encoder;
static bool ui_active = true;
static uint8_t menu_selection = 0;
static uint8_t menu_page = 0;
static uint32_t last_display_update = 0;

// Меню режими
typedef enum {
    UI_MODE_NORMAL = 0,      // Нормален режим (показва статус)
    UI_MODE_DISK_SELECT = 1, // Режим за избор на диск (списък от всички .dsk файлове)
    UI_MODE_DIR_NAV = 2     // Режим за навигация в директории
} ui_mode_t;

static ui_mode_t ui_mode = UI_MODE_NORMAL;
static uint8_t disk_menu_selection = 0;  // Избран диск в менюто
static uint8_t disk_menu_start = 0;      // Първия диск на текущата страница

// Навигация в директории
static char dir_items[20][MAX_FILENAME_LEN];  // Елементи в текущата директория
static bool dir_item_is_dir[20];              // Дали елементът е директория
static uint8_t dir_item_count = 0;             // Брой елементи
static uint8_t dir_menu_selection = 0;          // Избран елемент
static uint8_t dir_menu_start = 0;              // Първия елемент на страницата
static uint32_t last_button_press = 0;         // Време на последното натискане на бутона

// GCR кодиране таблица (5-битови кодове за 4-битови данни)
static const uint8_t gcr_encode_table[16] = {
    0x0A, 0x0B, 0x12, 0x13,  // 0-3
    0x0E, 0x0F, 0x16, 0x17,  // 4-7
    0x09, 0x19, 0x1A, 0x1B,  // 8-11
    0x0D, 0x1D, 0x1E, 0x15   // 12-15
};

// GCR декодиране таблица
static const uint8_t gcr_decode_table[32] = {
    0xFF, 0xFF, 0xFF, 0xFF,  // 0x00-0x03 (невалидни)
    0xFF, 0xFF, 0xFF, 0xFF,  // 0x04-0x07
    0xFF, 0x08, 0x00, 0x01,  // 0x08-0x0B
    0xFF, 0x0C, 0x04, 0x05,  // 0x0C-0x0F
    0xFF, 0xFF, 0x02, 0x03,  // 0x10-0x13
    0xFF, 0x0F, 0x06, 0x07,  // 0x14-0x17
    0xFF, 0x09, 0x0A, 0x0B,  // 0x18-0x1B
    0xFF, 0x0D, 0x0E, 0xFF   // 0x1C-0x1F
};

// ============================================================================
// SD Card функции (опростена имплементация)
// ============================================================================

// Forward декларации
bool load_track(uint8_t track);
bool sd_read_block(uint32_t block_addr, uint8_t *buffer);

// Инициализация на SPI за SD карта
static void sd_spi_init(void) {
    spi_init(SPI_PORT, 400000);  // 400 kHz за инициализация
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    gpio_init(PIN_CS);
    gpio_pull_up(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // CS висок (неактивен)
    gpio_pull_up(PIN_MISO);
    gpio_pull_up(PIN_MOSI);
    gpio_pull_up(PIN_SCK);
}

// Изпращане на команда към SD карта
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t response;
    uint8_t crc;
    uint8_t dummy = 0xFF;
    
    // CRC за специфични команди
    if (cmd == 0x40) {
        crc = 0x95;  // CRC за CMD0
    } else if (cmd == 0x48 && arg == 0x000001AA) {
        crc = 0x87;  // CRC за CMD8 с аргумент 0x000001AA
    } else {
        crc = 0xFF;  // CRC disabled за останалите команди в SPI режим
    }
    
    // Изпращане на dummy byte преди CS (SD спецификация)
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    // Спускане на CS
    gpio_put(PIN_CS, 0);
    
    // Изчакване след CS (SD спецификация изисква минимум 1 dummy byte)
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    // Изпращане на команда
    spi_write_blocking(SPI_PORT, &cmd, 1);
    
    // Изпращане на аргумент
    uint8_t arg_bytes[4];
    arg_bytes[0] = (arg >> 24) & 0xFF;
    arg_bytes[1] = (arg >> 16) & 0xFF;
    arg_bytes[2] = (arg >> 8) & 0xFF;
    arg_bytes[3] = arg & 0xFF;
    spi_write_blocking(SPI_PORT, arg_bytes, 4);
    
    // Изпращане на CRC
    spi_write_blocking(SPI_PORT, &crc, 1);
    
    // Четене на отговор (до 8 байта за по-бавни карти)
    response = 0xFF;
    for (int i = 0; i < 8; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &response, 1);
        if ((response & 0x80) == 0) break;
    }
    
    // Вдигане на CS
    gpio_put(PIN_CS, 1);
    
    // Допълнителен clock след CS
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    return response;
}

// Инициализация на SD карта
bool sd_init(void) {
    uint8_t response;
    bool is_sdhc = false;
    uint8_t dummy = 0xFF;
    
    // Уверете се че CS е висок (неактивен) преди началото
    gpio_put(PIN_CS, 1);
    
    // Първоначални clock цикли (минимум 74 за SD карта, но изпращаме повече за надеждност)
    for (int i = 0; i < 100; i++) {
        spi_write_blocking(SPI_PORT, &dummy, 1);
    }
    
    // Изчакване преди първата команда
    sleep_ms(10);
    
    // CMD0 - Reset (GO_IDLE_STATE)
    response = sd_send_cmd(0x40, 0);
    if (response != 0x01) {
        printf("SD CMD0 failed: 0x%02X\n", response);
        // Опитваме се още веднъж с по-дълго забавяне
        sleep_ms(200);
        for (int i = 0; i < 100; i++) {
            spi_write_blocking(SPI_PORT, &dummy, 1);
        }
        sleep_ms(10);
        response = sd_send_cmd(0x40, 0);
        if (response != 0x01) {
            printf("SD CMD0 failed again: 0x%02X\n", response);
            printf("ВЪЗМОЖНИ ПРИЧИНИ:\n");
            printf("  1. SD картата не е правилно свързана\n");
            printf("  2. Проблем с SPI комуникацията\n");
            printf("  3. SD картата не е готова или е повредена\n");
            return false;
        }
    }
    
    // Изчакване след CMD0 (SD спецификация изисква минимум 1ms)
    sleep_ms(50);  // Увеличено за по-надеждна инициализация
    
    // CMD8 - Check voltage (SEND_IF_COND)
    // За CMD8 трябва да четем R7 отговора докато CS е ниско
    gpio_put(PIN_CS, 0);
    uint8_t cmd8_cmd = 0x48;
    uint8_t cmd8_crc = 0x87;
    uint8_t cmd8_arg[4] = {0x00, 0x00, 0x01, 0xAA};
    
    // Изпращане на CMD8
    spi_write_blocking(SPI_PORT, &cmd8_cmd, 1);
    spi_write_blocking(SPI_PORT, cmd8_arg, 4);
    spi_write_blocking(SPI_PORT, &cmd8_crc, 1);
    
    // Четене на отговор
    response = 0xFF;
    for (int i = 0; i < 8; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &response, 1);
        if ((response & 0x80) == 0) break;
    }
    
    if (response == 0x01) {
        // SDHC/SDXC карта - четем R7 отговор (докато CS е ниско)
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) {
            spi_read_blocking(SPI_PORT, 0xFF, &r7[i], 1);
        }
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        
        // Проверка дали картата поддържа напрежението
        if (r7[2] == 0x01 && r7[3] == 0xAA) {
            is_sdhc = true;
        }
    } else if (response == 0x05) {
        // Стара SD карта (SDSC) - не поддържа CMD8
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        is_sdhc = false;
    } else {
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        printf("SD CMD8 unexpected response: 0x%02X\n", response);
        // Продължаваме с опит за инициализация (предполагаме SDSC)
        is_sdhc = false;
    }
    
    // ACMD41 - Initialize (SD_SEND_OP_COND)
    // За SDHC/SDXC използваме 0x40000000, за SDSC използваме 0x00000000
    uint32_t acmd41_arg = is_sdhc ? 0x40000000 : 0x00000000;
    
    // Опитваме се за по-дълго време за бавни карти
    for (int i = 0; i < 200; i++) {
        // CMD55 - APP_CMD (задължително преди всяка ACMD команда)
        response = sd_send_cmd(0x77, 0);
        if (response != 0x01) {
            // Ако CMD55 не връща 0x01, картата не е готова - изчакваме по-дълго
            if (i < 10 || (i % 20 == 0)) {
                // Показваме само първите 10 опита или на всеки 20-ти опит
                printf("SD CMD55 не готов: 0x%02X (attempt %d)\n", response, i + 1);
            }
            sleep_ms(50);  // По-дълго изчакване при проблем с CMD55
            continue;
        }
        
        // Допълнителни clock цикли между CMD55 и ACMD41 (SD спецификация изисква това)
        for (int j = 0; j < 8; j++) {
            spi_write_blocking(SPI_PORT, &dummy, 1);
        }
        
        // ACMD41 - Initialize
        response = sd_send_cmd(0x69, acmd41_arg);
        if (response == 0x00) {
            // Успешна инициализация
            break;
        }
        
        // Ако получим 0x01, картата все още е в idle state - това е нормално, продължаваме
        if (response == 0x01) {
            // Нормално - картата все още се инициализира
            // Продължаваме с нормалното изчакване
        } else if ((response & 0x01) == 0x01) {
            // Отговор с bit 0 set (idle state) но с други битове - може да е временен проблем
            // Продължаваме но с по-дълго изчакване
            if (i < 5) {
                printf("SD ACMD41 response with idle bit: 0x%02X (attempt %d)\n", response, i + 1);
            }
            sleep_ms(50);
            continue;
        } else {
            // Неочакван отговор без idle bit - може да е проблем, но опитваме се отново
            if (i < 5) {
                printf("SD ACMD41 unexpected response: 0x%02X (attempt %d)\n", response, i + 1);
            }
            // При неочакван отговор правим по-дълго изчакване и опитваме се отново с CMD0
            sleep_ms(100);
            // Опитваме се да направим reset с CMD0
            uint8_t reset_response = sd_send_cmd(0x40, 0);
            if (reset_response == 0x01) {
                sleep_ms(50);
            }
            continue;
        }
        
        sleep_ms(10);  // Нормално изчакване когато отговорът е 0x01
    }
    
    if (response != 0x00) {
        printf("SD ACMD41 failed: 0x%02X (after 200 attempts)\n", response);
        return false;
    }
    
    // Увеличаване на скоростта
    spi_set_baudrate(SPI_PORT, 10000000);  // 10 MHz
    
    // Запазване на типа на картата за използване в sd_read_block и sd_write_block
    sd_is_sdhc = is_sdhc;
    
    printf("SD карта инициализирана успешно (%s)\n", is_sdhc ? "SDHC/SDXC" : "SDSC");
    return true;
}

// Проверка дали SD картата е готова (публична функция за diskio.c)
bool sd_check_ready(void) {
    // Използваме CMD13 (SEND_STATUS) - безопасна команда която не променя състоянието
    uint8_t response = sd_send_cmd(0x4D, 0);  // CMD13 - SEND_STATUS
    if (response == 0x00) {
        // Картата е готова - прочитаме статуса (2 байта) за да завършим транзакцията
        uint8_t status[2];
        uint8_t dummy = 0xFF;
        gpio_put(PIN_CS, 0);
        spi_read_blocking(SPI_PORT, 0xFF, status, 2);
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        return true;
    }
    return false;
}

// Проверка дали SD картата е налична (hotplug detection)
static bool sd_check_presence(void) {
    // Ако картата вече е инициализирана, опитваме се с безопасна команда
    if (sd_card_present) {
        // Използваме CMD13 (SEND_STATUS) - безопасна команда
        uint8_t response = sd_send_cmd(0x4D, 0);  // CMD13 - SEND_STATUS
        if (response == 0x00) {
            // Картата отговаря - прочитаме статуса (2 байта)
            uint8_t status[2];
            uint8_t dummy = 0xFF;
            gpio_put(PIN_CS, 0);
            spi_read_blocking(SPI_PORT, 0xFF, status, 2);
            gpio_put(PIN_CS, 1);
            spi_write_blocking(SPI_PORT, &dummy, 1);
            return true;
        }
        // Ако CMD13 не работи, картата може да е премахната
        return false;
    } else {
        // Картата не е инициализирана - опитваме се с CMD0
        // CMD0 винаги трябва да отговори с 0x01 (idle state) ако картата е налична
        uint8_t response = sd_send_cmd(0x40, 0);  // CMD0 - GO_IDLE_STATE
        if (response == 0x01) {
            // Картата е налична, но не е инициализирана
            return true;
        }
        return false;
    }
}

// Обработка на премахване на SD карта
static void handle_sd_card_removal(void) {
    printf("SD картата е премахната!\n");
    
    // Затваряне на текущия диск
    if (disk_image_loaded) {
        disk_manager_unload(&disk_manager);
        disk_image_loaded = false;
    }
    
    // Размонтиране на файловата система
    f_mount(NULL, "", 0);
    
    sd_card_present = false;
    printf("Файловата система е размонтирана\n");
}

// Обработка на вмъкване на SD карта
static bool handle_sd_card_insertion(void) {
    printf("Открита е SD карта, инициализиране...\n");
    
    // Инициализация на SD карта
    if (!sd_init()) {
        printf("ГРЕШКА: Не може да се инициализира SD картата!\n");
        return false;
    }
    
    // Монтиране на файловата система
    FRESULT res = f_mount(&fs, "", 1);
    if (res != FR_OK) {
        printf("ГРЕШКА: Не може да се монтира файловата система (код: %d)\n", res);
        
        if (res == FR_NO_FILESYSTEM) {
            printf("ПРИЧИНА: Няма валидна FAT файлова система на SD картата\n");
            printf("РЕШЕНИЕ: Форматирайте SD картата с FAT32 файлова система\n");
        }
        
        return false;
    }
    
    printf("Файловата система е монтирана успешно\n");
    
    // Кратко забавяне за да се стабилизира файловата система
    sleep_ms(50);
    
    // Инициализация на disk manager
    disk_manager_init(&disk_manager);
    
    // Намаляване на скоростта на SPI за по-стабилно сканиране
    spi_set_baudrate(SPI_PORT, 2000000);  // 2 MHz за сканиране (по-стабилно)
    printf("Скоростта на SPI е намалена до 2 MHz за сканиране\n");
    
    // Рекурсивно сканиране за дискови имиджи (включително поддиректории)
    bool scan_result = disk_manager_scan_recursive(&disk_manager, "");
    
    // Възстановяване на нормалната скорост
    spi_set_baudrate(SPI_PORT, 10000000);  // 10 MHz за нормална работа
    printf("Скоростта на SPI е възстановена до 10 MHz\n");
    
    if (!scan_result) {
        printf("ПРЕДУПРЕЖДЕНИЕ: Не са намерени .dsk файлове\n");
        // Картата е налична, но няма дискови имиджи
        sd_card_present = true;
        return true;
    }
    
    // Зареждане на първия диск
    if (!disk_manager_load(&disk_manager, 0)) {
        printf("ПРЕДУПРЕЖДЕНИЕ: Не може да се зареди първият диск\n");
        sd_card_present = true;
        return true;
    }
    
    disk_image_loaded = true;
    sd_card_present = true;
    
    // Зареждане на текущата пътека
    load_track(current_track);
    
    printf("SD картата е готова за използване!\n");
    return true;
}

// Четене на блок от SD карта (512 байта)
bool sd_read_block(uint32_t block_addr, uint8_t *buffer) {
    uint8_t response;
    uint8_t dummy = 0xFF;
    uint32_t address;
    uint8_t crc;
    
    // За SDHC/SDXC карти, адресът е директно в блокове
    // За SDSC карти, адресът трябва да се умножи по 512 (размер на блока)
    address = sd_is_sdhc ? block_addr : (block_addr * 512);
    
    // Опитваме се до 5 пъти при грешка
    for (int retry = 0; retry < 5; retry++) {
        // Изчакване между опитите (особено при повторни опити)
        if (retry > 0) {
            // По-дълго изчакване при повторни опити
            sleep_ms(20 + (retry * 10));
        }
        
        gpio_put(PIN_CS, 0);
        
        // Допълнителен dummy byte преди командата (SD спецификация)
        spi_write_blocking(SPI_PORT, &dummy, 1);
        
        // CMD17 - Read single block (изпращаме командата директно, без да използваме sd_send_cmd)
        uint8_t cmd = 0x51;
        crc = 0xFF;  // CRC disabled за SPI режим
        
        // Изпращане на команда
        spi_write_blocking(SPI_PORT, &cmd, 1);
        
        // Изпращане на аргумент
        uint8_t arg_bytes[4];
        arg_bytes[0] = (address >> 24) & 0xFF;
        arg_bytes[1] = (address >> 16) & 0xFF;
        arg_bytes[2] = (address >> 8) & 0xFF;
        arg_bytes[3] = address & 0xFF;
        spi_write_blocking(SPI_PORT, arg_bytes, 4);
        
        // Изпращане на CRC
        spi_write_blocking(SPI_PORT, &crc, 1);
        
        // Допълнителен dummy byte преди четенето на отговора (SD спецификация изисква минимум 1)
        spi_write_blocking(SPI_PORT, &dummy, 1);
        
        // Четене на отговор (до 8 байта)
        // Отговорът трябва да има бит 7 = 0 (0x00-0x7F), ако е 0xFF значи все още чака
        response = 0xFF;
        for (int i = 0; i < 8; i++) {
            uint8_t read_byte;
            spi_write_read_blocking(SPI_PORT, &dummy, &read_byte, 1);
            if ((read_byte & 0x80) == 0) {
                response = read_byte;
                break;
            }
        }
        
        if (response != 0x00) {
            gpio_put(PIN_CS, 1);
            spi_write_blocking(SPI_PORT, &dummy, 1);
            // Ако отговорът не е 0x00, това е грешка
            // Специална обработка за грешка 0x04 (Command CRC Error) - изчакваме по-дълго
            if (response == 0x04 && retry < 4) {
                // При 0x04 правим по-дълго изчакване преди повторен опит
                gpio_put(PIN_CS, 1);
                for (int i = 0; i < 10; i++) {
                    spi_write_blocking(SPI_PORT, &dummy, 1);
                }
                sleep_ms(50);
                continue;
            }
            // Опитваме се да направим повторен опит
            if (retry < 4) {
                continue;
            }
            // Ако и всички опити са неуспешни, връщаме грешка
            printf("ERROR sd_read_block: CMD17 FAILED след 5 опита: 0x%02X за блок %lu\n", 
                   response, (unsigned long)block_addr);
            return false;
        }
        
        // Успешен отговор - излизаме от retry цикъла
        break;
    }
    
    // Чакане за старт токен (0xFE)
    uint8_t token;
    for (int i = 0; i < 1000; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &token, 1);
        if (token == 0xFE) break;
    }
    
    if (token != 0xFE) {
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        printf("DEBUG sd_read_block: Старт токен FAILED: 0x%02X (очаквано 0xFE) за блок %lu\n", 
               token, (unsigned long)block_addr);
        return false;
    }
    
    // Четене на 512 байта
    spi_read_blocking(SPI_PORT, 0xFF, buffer, 512);
    
    // Четене на CRC (2 байта)
    spi_read_blocking(SPI_PORT, 0xFF, &dummy, 1);
    spi_read_blocking(SPI_PORT, 0xFF, &dummy, 1);
    
    gpio_put(PIN_CS, 1);
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    return true;
}

// Запис на блок в SD карта (512 байта)
bool sd_write_block(uint32_t block_addr, const uint8_t *buffer) {
    uint8_t response;
    uint8_t dummy = 0xFF;
    uint8_t token;
    uint32_t address;
    uint8_t crc;
    
    // За SDHC/SDXC карти, адресът е директно в блокове
    // За SDSC карти, адресът трябва да се умножи по 512 (размер на блока)
    address = sd_is_sdhc ? block_addr : (block_addr * 512);
    
    gpio_put(PIN_CS, 0);
    
    // CMD24 - Write single block (изпращаме командата директно, без да използваме sd_send_cmd)
    uint8_t cmd = 0x58;
    crc = 0xFF;  // CRC disabled за SPI режим
    
    // Изпращане на команда
    spi_write_blocking(SPI_PORT, &cmd, 1);
    
    // Изпращане на аргумент
    uint8_t arg_bytes[4];
    arg_bytes[0] = (address >> 24) & 0xFF;
    arg_bytes[1] = (address >> 16) & 0xFF;
    arg_bytes[2] = (address >> 8) & 0xFF;
    arg_bytes[3] = address & 0xFF;
    spi_write_blocking(SPI_PORT, arg_bytes, 4);
    
    // Изпращане на CRC
    spi_write_blocking(SPI_PORT, &crc, 1);
    
    // Четене на отговор
    response = 0xFF;
    for (int i = 0; i < 8; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &response, 1);
        if ((response & 0x80) == 0) break;
    }
    
    if (response != 0x00) {
        gpio_put(PIN_CS, 1);
        spi_write_blocking(SPI_PORT, &dummy, 1);
        return false;
    }
    if (response != 0x00) {
        gpio_put(PIN_CS, 1);
        return false;
    }
    
    // Изпращане на старт токен (0xFE)
    token = 0xFE;
    spi_write_blocking(SPI_PORT, &token, 1);
    
    // Изпращане на 512 байта данни
    spi_write_blocking(SPI_PORT, buffer, 512);
    
    // Изпращане на CRC (2 байта dummy)
    spi_write_blocking(SPI_PORT, &dummy, 1);
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    // Чакане за response token
    for (int i = 0; i < 100; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &token, 1);
        if ((token & 0x1F) == 0x05) break;  // Data accepted
    }
    
    if ((token & 0x1F) != 0x05) {
        gpio_put(PIN_CS, 1);
        return false;
    }
    
    // Чакане за завършване на записа
    for (int i = 0; i < 1000; i++) {
        spi_read_blocking(SPI_PORT, 0xFF, &token, 1);
        if (token != 0x00) break;
    }
    
    gpio_put(PIN_CS, 1);
    spi_write_blocking(SPI_PORT, &dummy, 1);
    
    return true;
}

// Зареждане на дисковия имидж от SD карта (използва disk_manager)
static bool load_disk_image(void) {
    FRESULT res;
    
    printf("Монтиране на файловата система...\n");
    
    // Монтиране на файловата система
    res = f_mount(&fs, "", 1);
    if (res != FR_OK) {
        printf("ГРЕШКА: Не може да се монтира файловата система (код: %d)\n", res);
        return false;
    }
    
    // Инициализация на disk manager
    disk_manager_init(&disk_manager);
    
    // Сканиране за дискови имиджи
    if (!disk_manager_scan(&disk_manager)) {
        printf("ГРЕШКА: Не са намерени .dsk файлове\n");
        return false;
    }
    
    // Зареждане на първия диск
    if (!disk_manager_load(&disk_manager, 0)) {
        printf("ГРЕШКА: Не може да се зареди първият диск\n");
        return false;
    }
    
    disk_image_loaded = true;
    
    printf("Дисковият имидж е зареден успешно!\n");
    return true;
}

// Зареждане на пътека от SD карта
bool load_track(uint8_t track) {
    // Проверка за наличие на SD карта
    if (!sd_card_present || !disk_image_loaded) {
        return false;
    }
    
    disk_image_t *current_disk = disk_manager_get_current(&disk_manager);
    if (!current_disk || !current_disk->loaded) {
        return false;
    }
    
    FRESULT res;
    UINT bytes_read;
    uint32_t track_size = get_track_size();
    
    // Изчисляване на позицията в файла
    FSIZE_t file_pos = (FSIZE_t)track * track_size;
    
    // Преместване на файловия указател
    res = f_lseek(&current_disk->file_handle, file_pos);
    if (res != FR_OK) {
        printf("ГРЕШКА: Не може да се премести файловият указател (код: %d)\n", res);
        return false;
    }
    
    // Четене на пътеката
    res = f_read(&current_disk->file_handle, disk_image_buffer, track_size, &bytes_read);
    if (res != FR_OK || bytes_read != track_size) {
        printf("ГРЕШКА: Не може да се прочете пътека %d (код: %d, прочетено: %u)\n", 
               track, res, bytes_read);
        return false;
    }
    
    return true;
}

// Запис на пътека в SD карта
static bool save_track(uint8_t track) {
    // Проверка за наличие на SD карта
    if (!sd_card_present || !disk_image_loaded) {
        return false;
    }
    
    disk_image_t *current_disk = disk_manager_get_current(&disk_manager);
    if (!current_disk || !current_disk->loaded || write_protected) {
        return false;
    }
    
    FRESULT res;
    UINT bytes_written;
    uint32_t track_size = get_track_size();
    
    // Изчисляване на позицията в файла
    FSIZE_t file_pos = (FSIZE_t)track * track_size;
    
    // Преместване на файловия указател
    res = f_lseek(&current_disk->file_handle, file_pos);
    if (res != FR_OK) {
        printf("ГРЕШКА: Не може да се премести файловият указател за запис (код: %d)\n", res);
        return false;
    }
    
    // Запис на пътеката
    res = f_write(&current_disk->file_handle, disk_image_buffer, track_size, &bytes_written);
    if (res != FR_OK || bytes_written != track_size) {
        printf("ГРЕШКА: Не може да се запише пътека %d (код: %d, записано: %u)\n", 
               track, res, bytes_written);
        return false;
    }
    
    // Синхронизация на файла
    f_sync(&current_disk->file_handle);
    
    printf("Пътека %d е записана успешно\n", track);
    return true;
}

// Запис на сектор в буфера на пътеката
static bool write_sector_to_track(uint8_t sector, uint8_t *data) {
    disk_config_t *format = get_current_disk_format();
    if (!format || sector >= format->sectors_per_track) {
        return false;
    }
    
    uint32_t sector_offset = sector * format->bytes_per_sector;
    memcpy(disk_image_buffer + sector_offset, data, format->bytes_per_sector);
    
    return true;
}

// ============================================================================
// PIO и DMA инициализация
// ============================================================================

// Инициализация на PIO за READ_DATA
static void init_read_data_pio(void) {
    // Зареждане на PIO програмата
    offset_read = pio_add_program(pio_read, &read_data_program);
    
    // Инициализация на PIO state machine
    read_data_program_init(pio_read, sm_read, offset_read, GPIO_READ_DATA);
    
    printf("PIO READ_DATA инициализиран (SM %d, offset %d)\n", sm_read, offset_read);
}

// Инициализация на PIO за WRITE_DATA
static void init_write_data_pio(void) {
    // Зареждане на PIO програмата
    offset_write = pio_add_program(pio_write, &write_data_program);
    
    // Инициализация на PIO state machine
    write_data_program_init(pio_write, sm_write, offset_write, GPIO_WRITE_DATA);
    
    printf("PIO WRITE_DATA инициализиран (SM %d, offset %d)\n", sm_write, offset_write);
}

// Инициализация на DMA за READ_DATA
static void init_read_data_dma(void) {
    dma_channel_read = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(dma_channel_read);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, pio_get_dreq(pio_read, sm_read, true));  // DREQ от PIO
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    
    dma_channel_configure(
        dma_channel_read,
        &c,
        &pio_read->txf[sm_read],  // Destination: PIO TX FIFO
        NULL,                      // Source (ще се зададе при започване)
        0,                         // Count (ще се зададе при започване)
        false                      // Don't start yet
    );
    
    printf("DMA READ_DATA канал инициализиран (канал %d)\n", dma_channel_read);
}

// Инициализация на DMA за WRITE_DATA
static void init_write_data_dma(void) {
    dma_channel_write = dma_claim_unused_channel(true);
    
    dma_channel_config c = dma_channel_get_default_config(dma_channel_write);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, pio_get_dreq(pio_write, sm_write, false));  // DREQ от PIO
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    
    dma_channel_configure(
        dma_channel_write,
        &c,
        write_fifo_buffer,          // Destination: буфер
        &pio_write->rxf[sm_write],  // Source: PIO RX FIFO
        0,                          // Count (ще се зададе при започване)
        false                       // Don't start yet
    );
    
    printf("DMA WRITE_DATA канал инициализиран (канал %d)\n", dma_channel_write);
}

// ============================================================================
// GCR кодиране/декодиране
// ============================================================================

// Кодиране на байт в GCR формат (2 байта = 10 бита)
static void gcr_encode_byte(uint8_t data, uint8_t *gcr_out) {
    uint8_t high_nibble = (data >> 4) & 0x0F;
    uint8_t low_nibble = data & 0x0F;
    
    gcr_out[0] = gcr_encode_table[high_nibble];
    gcr_out[1] = gcr_encode_table[low_nibble];
}

// Декодиране на GCR байт (2 байта -> 1 байт)
static bool gcr_decode_byte(uint8_t *gcr_in, uint8_t *data_out) {
    uint8_t high = gcr_decode_table[gcr_in[0] & 0x1F];
    uint8_t low = gcr_decode_table[gcr_in[1] & 0x1F];
    
    if (high == 0xFF || low == 0xFF) {
        return false;  // Невалиден GCR код
    }
    
    *data_out = (high << 4) | low;
    return true;
}

// Forward декларации
static void process_write_byte(uint8_t gcr_byte);
void init_interrupts(void);  // Дефинирана в interrupts.c

// Обработка на WRITE_DATA сигнал с PIO/DMA
static void process_write_data_pio(void) {
    // Проверка дали има данни в PIO FIFO
    if (pio_sm_is_rx_fifo_empty(pio_write, sm_write)) {
        return;
    }
    
    // Четене на данни от PIO FIFO (до 32 байта наведнъж)
    uint32_t bytes_to_read = pio_sm_get_rx_fifo_level(pio_write, sm_write);
    if (bytes_to_read > sizeof(write_fifo_buffer)) {
        bytes_to_read = sizeof(write_fifo_buffer);
    }
    
    // Четене директно от FIFO (по-бързо от DMA за малки количества)
    for (uint32_t i = 0; i < bytes_to_read; i++) {
        write_fifo_buffer[i] = pio_sm_get_blocking(pio_write, sm_write);
    }
    
    // Обработка на получените данни
    for (uint32_t i = 0; i < bytes_to_read; i++) {
        process_write_byte(write_fifo_buffer[i]);
    }
}

// Обработка на байт от WRITE_DATA (GCR формат)
static void process_write_byte(uint8_t gcr_byte) {
    if (!write_in_progress) {
        // Търсене на синхронизационни битове (0xFF)
        static uint32_t sync_count = 0;
        if (gcr_byte == 0xFF) {
            sync_count++;
            if (sync_count > 3) {
                // Започваме запис
                write_in_progress = true;
                write_bit_count = 0;
                write_gcr_index = 0;
                memset(write_buffer, 0, get_bytes_per_sector());
                printf("Започва запис на сектор...\n");
            }
        } else {
            sync_count = 0;
        }
        return;
    }
    
    // Събиране на GCR байтове (2 байта = 1 данен байт)
    write_gcr_buffer[write_gcr_index] = gcr_byte;
    write_gcr_index++;
    
    if (write_gcr_index >= 2) {
        // Декодиране на GCR байт
        uint8_t decoded_byte;
        if (gcr_decode_byte(write_gcr_buffer, &decoded_byte)) {
            uint16_t bytes_per_sector = get_bytes_per_sector();
            uint32_t byte_index = write_bit_count / 10;  // 10 бита на байт (8 данни + 2 sync)
            if (byte_index < bytes_per_sector) {
                write_buffer[byte_index] = decoded_byte;
            }
        }
        write_gcr_index = 0;
        write_bit_count += 10;
        
        // Проверка за край на сектора
        uint16_t bytes_per_sector = get_bytes_per_sector();
        if (write_bit_count >= (bytes_per_sector * 10)) {
            // Автоматично определяне на номера на сектора
            sector_address_t sector_addr = detect_sector_from_data(write_buffer, bytes_per_sector, current_track);
            if (sector_addr.valid) {
                current_write_sector = sector_addr.sector;
                printf("Определен сектор: %d на пътека %d\n", sector_addr.sector, sector_addr.track);
            }
            
            // Завършване на записа
            write_in_progress = false;
            printf("Запис на сектор %d завършен (%lu бита)\n", current_write_sector, write_bit_count);
            
            // Запис на сектора в пътеката
            if (write_sector_to_track(current_write_sector, write_buffer)) {
                // Запис на пътеката в файла
                save_track(current_track);
            }
        }
    }
}

// Стара функция за обратна съвместимост (не се използва с PIO)
static void process_write_bit(bool bit_value) {
    static uint32_t bit_shift = 0;
    static uint8_t current_gcr_byte = 0;
    
    if (!write_in_progress) {
        // Търсене на синхронизационни битове
        // Apple II изпраща синхронизационни битове преди данните
        static uint32_t sync_count = 0;
        if (bit_value) {
            sync_count++;
            if (sync_count > 20) {
                // Започваме запис
                write_in_progress = true;
                write_bit_count = 0;
                write_gcr_index = 0;
                bit_shift = 0;
                current_gcr_byte = 0;
                memset(write_buffer, 0, get_bytes_per_sector());
                printf("Започва запис на сектор...\n");
            }
        } else {
            sync_count = 0;
        }
        return;
    }
    
    // Събиране на GCR битове
    current_gcr_byte = (current_gcr_byte << 1) | (bit_value ? 1 : 0);
    bit_shift++;
    
    if (bit_shift >= 8) {
        // Имаме пълен GCR байт
        write_gcr_buffer[write_gcr_index] = current_gcr_byte;
        write_gcr_index++;
        bit_shift = 0;
        current_gcr_byte = 0;
        
        if (write_gcr_index >= 2) {
            // Декодиране на GCR байт
            uint8_t decoded_byte;
            if (gcr_decode_byte(write_gcr_buffer, &decoded_byte)) {
                uint16_t bytes_per_sector = get_bytes_per_sector();
                uint32_t byte_index = write_bit_count / 10;  // 10 бита на байт (8 данни + 2 sync)
                if (byte_index < bytes_per_sector) {
                    write_buffer[byte_index] = decoded_byte;
                }
            }
            write_gcr_index = 0;
        }
        
        write_bit_count++;
        
        // Проверка за край на сектора
        // Всеки байт е 10 бита в GCR формат
        uint16_t bytes_per_sector = get_bytes_per_sector();
        if (write_bit_count >= (bytes_per_sector * 10)) {
            // Автоматично определяне на номера на сектора
            sector_address_t sector_addr = detect_sector_from_data(write_buffer, bytes_per_sector, current_track);
            if (sector_addr.valid) {
                current_write_sector = sector_addr.sector;
                printf("Определен сектор: %d на пътека %d\n", sector_addr.sector, sector_addr.track);
            }
            
            // Завършване на записа
            write_in_progress = false;
            printf("Запис на сектор %d завършен (%lu бита)\n", current_write_sector, write_bit_count);
            
            // Запис на сектора в пътеката
            if (write_sector_to_track(current_write_sector, write_buffer)) {
                // Запис на пътеката в файла
                save_track(current_track);
            }
        }
    }
}

// ============================================================================
// Управление на стъпковия мотор
// ============================================================================

// Стъпка към следващата пътека (навътре)
static void step_in(void) {
    if (current_track < get_tracks_per_disk() - 1) {
        current_track++;
        printf("Стъпка НАВЪТРЕ -> Пътека %d\n", current_track);
        // Зареждане на новата пътека
        load_track(current_track);
    }
}

// Стъпка към предишната пътека (навън)
static void step_out(void) {
    if (current_track > 0) {
        current_track--;
        printf("Стъпка НАВЪН -> Пътека %d\n", current_track);
        // Зареждане на новата пътека
        load_track(current_track);
    }
}

// Обновяване на TRACK0 сигнала
static void update_track0(void) {
    gpio_put(GPIO_TRACK0, (current_track == 0) ? 0 : 1);  // Active low
}

// ============================================================================
// Обработка на команди от Apple II
// ============================================================================

// Обработка на фазови сигнали за стъпка
// Apple II използва 4-фазен стъпков мотор с последователни фази
static void handle_phase_step(void) {
    static uint8_t last_phase_state = 0;
    
    // Четене на текущото състояние на фазите
    uint8_t phase_state = 0;
    if (gpio_get(GPIO_PH0)) phase_state |= 0x01;
    if (gpio_get(GPIO_PH1)) phase_state |= 0x02;
    if (gpio_get(GPIO_PH2)) phase_state |= 0x04;
    if (gpio_get(GPIO_PH3)) phase_state |= 0x08;
    
    // Apple II контролерът управлява фазите директно
    // Ние само следваме позицията на главата
    // За опростена версия, използваме GPIO за четене на фазите
    
    // Определяне на посоката на движение
    if (phase_state != last_phase_state) {
        // Намиране на активната фаза
        uint8_t active_phase = 0;
        if (phase_state & 0x01) active_phase = 0;
        else if (phase_state & 0x02) active_phase = 1;
        else if (phase_state & 0x04) active_phase = 2;
        else if (phase_state & 0x08) active_phase = 3;
        
        uint8_t last_active = 0;
        if (last_phase_state & 0x01) last_active = 0;
        else if (last_phase_state & 0x02) last_active = 1;
        else if (last_phase_state & 0x04) last_active = 2;
        else if (last_phase_state & 0x08) last_active = 3;
        
        // Определяне на посоката
        int8_t diff = (active_phase - last_active) & 0x03;
        if (diff == 1 || diff == 3) {
            if (diff == 1) {
                step_in();
            } else {
                step_out();
            }
            update_track0();
        }
        
        last_phase_state = phase_state;
    }
}

// Генериране на READ DATA сигнал за сектор с PIO/DMA
static void generate_read_data(uint8_t sector) {
    if (!motor_on || !disk_image_loaded) {
        return;
    }
    
    // Изчисляване на адреса на сектора
    uint16_t bytes_per_sector = get_bytes_per_sector();
    uint32_t track_size = get_track_size();
    uint32_t sector_offset = sector * bytes_per_sector;
    if (sector_offset >= track_size) {
        return;
    }
    
    uint8_t *sector_data = disk_image_buffer + sector_offset;
    
    // Подготовка на GCR данни
    // Всеки байт се кодира в 2 GCR байта
    uint8_t gcr_data[256 * 2];  // Максимален размер
    for (int i = 0; i < bytes_per_sector; i++) {
        gcr_encode_byte(sector_data[i], &gcr_data[i * 2]);
    }
    
    // Подготовка на синхронизационни битове (40 бита = 5 байта)
    uint8_t sync_bytes[5];
    memset(sync_bytes, 0xFF, 5);  // Всички битове са 1
    
    // Общ буфер: синхронизация + данни
    uint8_t transmit_buffer[5 + (256 * 2)];  // Максимален размер
    memcpy(transmit_buffer, sync_bytes, 5);
    memcpy(transmit_buffer + 5, gcr_data, bytes_per_sector * 2);
    
    // Изчакване да се изпразни FIFO
    while (!pio_sm_is_tx_fifo_empty(pio_read, sm_read)) {
        tight_loop_contents();
    }
    
    // Стартиране на DMA за прехвърляне на данни към PIO
    dma_channel_set_read_addr(dma_channel_read, transmit_buffer, false);
    dma_channel_set_trans_count(dma_channel_read, sizeof(transmit_buffer), true);
    
    // Чакане за завършване на DMA
    dma_channel_wait_for_finish_blocking(dma_channel_read);
    
    printf("Генерирани данни за сектор %d на пътека %d (PIO/DMA)\n", sector, current_track);
}

// ============================================================================
// UI функции
// ============================================================================

// Обновяване на дисплея
static void update_display(void) {
    char buffer[64];
    
    ssd1306_clear();
    
    if (ui_mode == UI_MODE_DIR_NAV) {
        // Меню за навигация в директории
        const char *current_path = disk_manager_get_current_path(&disk_manager);
        
        // Заглавие с текущия път
        if (strlen(current_path) == 0) {
            snprintf(buffer, sizeof(buffer), "Dir: / (%d)", dir_item_count);
        } else {
            // Съкращаване на пътя ако е твърде дълъг
            char short_path[15];
            if (strlen(current_path) > 14) {
                snprintf(short_path, sizeof(short_path), "...%s", current_path + strlen(current_path) - 11);
            } else {
                strncpy(short_path, current_path, sizeof(short_path) - 1);
                short_path[sizeof(short_path) - 1] = '\0';
            }
            snprintf(buffer, sizeof(buffer), "Dir: %s", short_path);
        }
        ssd1306_draw_string(0, 0, buffer);
        
        // Показване на до 4 елемента на страница
        uint8_t items_per_page = 4;
        uint8_t start_idx = dir_menu_start;
        uint8_t end_idx = (start_idx + items_per_page < dir_item_count) ? 
                          (start_idx + items_per_page) : dir_item_count;
        
        for (uint8_t i = start_idx; i < end_idx; i++) {
            uint8_t y_pos = 10 + (i - start_idx) * 12;
            const char *marker = (i == dir_menu_selection) ? ">" : " ";
            const char *dir_marker = dir_item_is_dir[i] ? "[DIR]" : "     ";
            
            // Съкращаване на името ако е твърде дълго
            char short_name[10];
            strncpy(short_name, dir_items[i], 9);
            short_name[9] = '\0';
            
            snprintf(buffer, sizeof(buffer), "%s%s%.9s", marker, dir_marker, short_name);
            ssd1306_draw_string(0, y_pos, buffer);
        }
        
        // Инструкции
        ssd1306_draw_string(0, 58, "Btn:Open  Rot:Nav");
    } else if (ui_mode == UI_MODE_DISK_SELECT) {
        // Меню за избор на диск
        uint8_t disk_count = disk_manager_get_count(&disk_manager);
        
        // Заглавие
        snprintf(buffer, sizeof(buffer), "Select Disk (%d)", disk_count);
        ssd1306_draw_string(0, 0, buffer);
        
        // Показване на до 4 диска на страница (OLED има 64 пиксела височина, ~10px на ред)
        uint8_t items_per_page = 4;
        uint8_t start_idx = disk_menu_start;
        uint8_t end_idx = (start_idx + items_per_page < disk_count) ? 
                          (start_idx + items_per_page) : disk_count;
        
        for (uint8_t i = start_idx; i < end_idx; i++) {
            disk_image_t *disk = disk_manager_get_disk(&disk_manager, i);
            if (disk) {
                uint8_t y_pos = 10 + (i - start_idx) * 12;
                const char *marker = (i == disk_menu_selection) ? ">" : " ";
                
                // Съкращаване на името ако е твърде дълго
                char short_name[13];
                strncpy(short_name, disk->filename, 12);
                short_name[12] = '\0';
                
                snprintf(buffer, sizeof(buffer), "%s%02d:%.12s", marker, i, short_name);
                ssd1306_draw_string(0, y_pos, buffer);
            }
        }
        
        // Индикация за текущ избран диск
        if (disk_image_loaded) {
            uint8_t current_idx = disk_manager_get_current_index(&disk_manager);
            if (current_idx == disk_menu_selection) {
                ssd1306_draw_string(0, 58, "*ACTIVE*");
            }
        }
    } else {
        // Нормален режим - показване на статус
        // Заглавие
        ssd1306_draw_string(0, 0, "Apple II Floppy");
        
        // Статус на мотор
        if (motor_on) {
            ssd1306_draw_string(0, 10, "Motor: ON");
        } else {
            ssd1306_draw_string(0, 10, "Motor: OFF");
        }
        
        // Текуща пътека
        snprintf(buffer, sizeof(buffer), "Track: %02d/%02d", current_track, get_tracks_per_disk() - 1);
        ssd1306_draw_string(0, 20, buffer);
        
        // Статус на SD карта
        if (!sd_card_present) {
            ssd1306_draw_string(0, 30, "SD: NOT INSERTED");
        } else {
            // Статус на диск
            if (disk_image_loaded) {
                const char *disk_name = disk_manager_get_current_name(&disk_manager);
                snprintf(buffer, sizeof(buffer), "Disk: %.10s", disk_name);
                ssd1306_draw_string(0, 30, buffer);
            } else {
                ssd1306_draw_string(0, 30, "Disk: None");
            }
        }
        
        // Формат на диск
        if (current_disk_config) {
            snprintf(buffer, sizeof(buffer), "Fmt: %s", current_disk_config->format_name);
            ssd1306_draw_string(0, 40, buffer);
        }
        
        // Write protect статус
        if (write_protected) {
            ssd1306_draw_string(0, 50, "W: PROTECT");
        } else {
            ssd1306_draw_string(0, 50, "W: ENABLE");
        }
        
        // Индикация за меню опции
        const char *menu_items[] = {"[Motor]", "[WProt]", "[Disk]"};
        snprintf(buffer, sizeof(buffer), "%s", menu_items[menu_selection]);
        ssd1306_draw_string(90, 50, buffer);
    }
    
    ssd1306_update();
}

// Обработка на UI вход
static void handle_ui_input(void) {
    int8_t encoder_delta = encoder_read(&encoder);
    
    if (ui_mode == UI_MODE_DIR_NAV) {
        // Режим за навигация в директории
        if (encoder_delta > 0) {
            // Навигация надолу
            if (dir_menu_selection < dir_item_count - 1) {
                dir_menu_selection++;
                // Скролване на страницата ако е необходимо
                uint8_t items_per_page = 4;
                if (dir_menu_selection >= dir_menu_start + items_per_page) {
                    dir_menu_start = dir_menu_selection - items_per_page + 1;
                }
            }
            update_display();
        } else if (encoder_delta < 0) {
            // Навигация нагоре
            if (dir_menu_selection > 0) {
                dir_menu_selection--;
                // Скролване на страницата ако е необходимо
                if (dir_menu_selection < dir_menu_start) {
                    dir_menu_start = dir_menu_selection;
                }
            }
            update_display();
        }
        
        if (encoder_button_pressed(&encoder)) {
            // Проверка за двойно натискане (изход от режима)
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - last_button_press < 500 && last_button_press > 0) {
                // Двойно натискане - изход от режима на навигация
                ui_mode = UI_MODE_NORMAL;
                update_display();
                last_button_press = 0;
                return;
            }
            last_button_press = current_time;
            
            // Отваряне на избрания елемент
            if (dir_menu_selection < dir_item_count) {
                const char *current_path = disk_manager_get_current_path(&disk_manager);
                char new_path[MAX_PATH_LEN];
                
                if (dir_item_is_dir[dir_menu_selection]) {
                    // Проверка за връщане назад
                    if (strcmp(dir_items[dir_menu_selection], "..") == 0) {
                        // Връщане към родителската директория
                        if (strlen(current_path) > 0) {
                            // Намиране на последния "/"
                            char *last_slash = strrchr(current_path, '/');
                            if (last_slash != NULL) {
                                *last_slash = '\0';
                                strncpy(new_path, current_path, MAX_PATH_LEN - 1);
                                new_path[MAX_PATH_LEN - 1] = '\0';
                            } else {
                                new_path[0] = '\0';  // Връщане към корневата директория
                            }
                        } else {
                            new_path[0] = '\0';  // Вече сме в корневата директория
                        }
                    } else {
                        // Отваряне на поддиректория
                        if (strlen(current_path) == 0) {
                            snprintf(new_path, MAX_PATH_LEN, "%s", dir_items[dir_menu_selection]);
                        } else {
                            snprintf(new_path, MAX_PATH_LEN, "%s/%s", current_path, dir_items[dir_menu_selection]);
                        }
                    }
                    disk_manager_set_path(&disk_manager, new_path);
                    // Зареждане на елементите в новата директория
                    disk_manager_list_directory(&disk_manager, new_path, dir_items, dir_item_is_dir, 
                                                &dir_item_count, 20);
                    dir_menu_selection = 0;
                    dir_menu_start = 0;
                } else {
                    // Зареждане на .dsk файл
                    char file_path[MAX_PATH_LEN];
                    if (strlen(current_path) == 0) {
                        snprintf(file_path, MAX_PATH_LEN, "%s", dir_items[dir_menu_selection]);
                    } else {
                        snprintf(file_path, MAX_PATH_LEN, "%s/%s", current_path, dir_items[dir_menu_selection]);
                    }
                    
                    // Проверка дали е .dsk файл
                    size_t len = strlen(file_path);
                    if (len >= 4) {
                        const char *ext = file_path + len - 4;
                        bool is_dsk = (ext[0] == '.' && 
                                      (ext[1] == 'd' || ext[1] == 'D') &&
                                      (ext[2] == 's' || ext[2] == 'S') &&
                                      (ext[3] == 'k' || ext[3] == 'K'));
                        
                        if (is_dsk) {
                            // Зареждане на файла
                            FIL file;
                            FRESULT res = f_open(&file, file_path, FA_READ);
                            if (res == FR_OK) {
                                // Намиране на файла в списъка или добавяне
                                bool found = false;
                                uint8_t file_index = 0;
                                for (uint8_t i = 0; i < disk_manager.count; i++) {
                                    if (strcmp(disk_manager.images[i].filename, file_path) == 0) {
                                        found = true;
                                        file_index = i;
                                        break;
                                    }
                                }
                                
                                if (!found && disk_manager.count < MAX_DISK_IMAGES) {
                                    // Добавяне на нов файл
                                    file_index = disk_manager.count;
                                    strncpy(disk_manager.images[file_index].filename, file_path, MAX_FILENAME_LEN - 1);
                                    disk_manager.images[file_index].filename[MAX_FILENAME_LEN - 1] = '\0';
                                    
                                    // Получаване на размера на файла
                                    // Запазване на текущата позиция
                                    FSIZE_t current_pos = f_tell(&file);
                                    // Отиване в края на файла
                                    f_lseek(&file, 0xFFFFFFFF);  // Отиване в максимална позиция
                                    FSIZE_t file_size = f_tell(&file);
                                    // Връщане в началото
                                    f_lseek(&file, 0);
                                    
                                    disk_manager.images[file_index].file_size = file_size;
                                    disk_manager.images[file_index].format = DISK_FORMAT_AUTO;
                                    disk_manager.images[file_index].loaded = false;
                                    disk_manager.count++;
                                }
                                
                                f_close(&file);
                                
                                // Зареждане на файла
                                if (disk_manager_load(&disk_manager, file_index)) {
                                    load_track(current_track);
                                    printf("Зареден диск: %s\n", file_path);
                                }
                                
                                // Връщане към нормален режим
                                ui_mode = UI_MODE_NORMAL;
                                update_display();
                            }
                        }
                    }
                }
            }
            update_display();
        }
    } else if (ui_mode == UI_MODE_DISK_SELECT) {
        // Режим за избор на диск
        uint8_t disk_count = disk_manager_get_count(&disk_manager);
        
        if (encoder_delta > 0) {
            // Навигация надолу
            if (disk_menu_selection < disk_count - 1) {
                disk_menu_selection++;
                // Скролване на страницата ако е необходимо
                uint8_t items_per_page = 4;
                if (disk_menu_selection >= disk_menu_start + items_per_page) {
                    disk_menu_start = disk_menu_selection - items_per_page + 1;
                }
            }
            update_display();
        } else if (encoder_delta < 0) {
            // Навигация нагоре
            if (disk_menu_selection > 0) {
                disk_menu_selection--;
                // Скролване на страницата ако е необходимо
                if (disk_menu_selection < disk_menu_start) {
                    disk_menu_start = disk_menu_selection;
                }
            }
            update_display();
        }
        
        if (encoder_button_pressed(&encoder)) {
            // Избор на диск
            if (disk_manager_load(&disk_manager, disk_menu_selection)) {
                load_track(current_track);
                printf("Избран диск: %s\n", disk_manager_get_current_name(&disk_manager));
            }
            // Връщане към нормален режим
            ui_mode = UI_MODE_NORMAL;
            update_display();
        }
    } else {
        // Нормален режим
        if (encoder_delta > 0) {
            menu_selection = (menu_selection + 1) % 3;
            update_display();
        } else if (encoder_delta < 0) {
            menu_selection = (menu_selection - 1 + 3) % 3;
            update_display();
        }
        
        if (encoder_button_pressed(&encoder)) {
            // Проверка за двойно натискане (показване на списък с всички .dsk файлове)
            uint32_t current_time = to_ms_since_boot(get_absolute_time());
            if (current_time - last_button_press < 500 && last_button_press > 0 && menu_selection == 2) {
                // Двойно натискане на "Disk" - показване на списък с всички .dsk файлове
                ui_mode = UI_MODE_DISK_SELECT;
                disk_menu_selection = disk_manager_get_current_index(&disk_manager);
                uint8_t items_per_page = 4;
                if (disk_menu_selection >= items_per_page) {
                    disk_menu_start = disk_menu_selection - items_per_page + 1;
                } else {
                    disk_menu_start = 0;
                }
                update_display();
                last_button_press = 0;
                return;
            }
            last_button_press = current_time;
            
            // Изпълнение на избраното действие
            switch (menu_selection) {
                case 0:  // Toggle Motor
                    motor_on = !motor_on;
                    if (motor_on) {
                        load_track(current_track);
                    }
                    break;
                case 1:  // Toggle Write Protect
                    write_protected = !write_protected;
                    gpio_put(GPIO_WRITE_PROTECT, write_protected ? 0 : 1);
                    break;
                case 2:  // Select Disk / Navigate
                    // Превключване към режим за навигация в директории
                    ui_mode = UI_MODE_DIR_NAV;
                    disk_manager_set_path(&disk_manager, "");
                    // Зареждане на елементите в корневата директория
                    disk_manager_list_directory(&disk_manager, "", dir_items, dir_item_is_dir, 
                                                &dir_item_count, 20);
                    dir_menu_selection = 0;
                    dir_menu_start = 0;
                    update_display();
                    break;
            }
            update_display();
        }
    }
}

// Инициализация на UI
static void init_ui(void) {
    // Инициализация на енкодер
    encoder_init(&encoder, ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BUTTON);
    
    // Инициализация на OLED дисплей
    ssd1306_init(I2C_PORT, I2C_SDA, I2C_SCL);
    
    // Първоначално обновяване на дисплея
    update_display();
    
    printf("UI инициализиран\n");
}

// ============================================================================
// Главен цикъл
// ============================================================================

int main(void) {
    stdio_init_all();
    
    printf("\n=== Apple II Floppy Disk Emulator ===\n");
    printf("Версия: 1.3\n");
    printf("16 сектора на пътека\n\n");
    
    // Зареждане на конфигурация по подразбиране
    load_default_gpio_config();
    
    // Инициализация на GPIO (с конфигурация по подразбиране)
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    // Инициализация на входни сигнали от Apple II контролер
    // Apple II контролерът управлява фазите директно, така че ги четем като вход
    gpio_init(GPIO_PH0);
    gpio_init(GPIO_PH1);
    gpio_init(GPIO_PH2);
    gpio_init(GPIO_PH3);
    gpio_set_dir(GPIO_PH0, GPIO_IN);
    gpio_set_dir(GPIO_PH1, GPIO_IN);
    gpio_set_dir(GPIO_PH2, GPIO_IN);
    gpio_set_dir(GPIO_PH3, GPIO_IN);
    
    gpio_init(GPIO_MOTOR_ON);
    gpio_init(GPIO_WRITE_ENABLE);
    gpio_init(GPIO_WRITE_DATA);
    gpio_set_dir(GPIO_MOTOR_ON, GPIO_IN);
    gpio_set_dir(GPIO_WRITE_ENABLE, GPIO_IN);
    gpio_set_dir(GPIO_WRITE_DATA, GPIO_IN);
    
    // Инициализация на изходни сигнали
    gpio_init(GPIO_READ_DATA);
    gpio_init(GPIO_TRACK0);
    gpio_init(GPIO_WRITE_PROTECT);
    gpio_set_dir(GPIO_READ_DATA, GPIO_OUT);
    gpio_set_dir(GPIO_TRACK0, GPIO_OUT);
    gpio_set_dir(GPIO_WRITE_PROTECT, GPIO_OUT);
    
    gpio_put(GPIO_READ_DATA, 0);
    gpio_put(GPIO_TRACK0, 1);  // Active low
    gpio_put(GPIO_WRITE_PROTECT, write_protected ? 0 : 1);  // 0 = защитено, 1 = разрешено
    
    // Инициализация на PIO и DMA
    printf("Инициализация на PIO и DMA...\n");
    
    // Проверка за наличност на PIO state machines
    if (pio_can_add_program(pio0, &read_data_program)) {
        init_read_data_pio();
        init_read_data_dma();
    } else {
        printf("ГРЕШКА: Не може да се добави READ_DATA PIO програма\n");
    }
    
    if (pio_can_add_program(pio1, &write_data_program)) {
        init_write_data_pio();
        init_write_data_dma();
    } else {
        printf("ГРЕШКА: Не може да се добави WRITE_DATA PIO програма\n");
    }
    
    // Инициализация на interrupts
    printf("Инициализация на interrupts...\n");
    init_interrupts();
    
    // Инициализация на UI
    printf("Инициализация на UI...\n");
    init_ui();
    
    // Инициализация на CLI (UART0 на GPIO 0/1)
    printf("Инициализация на CLI (UART0 на GPIO 0/1)...\n");
    cli_init();
    

    // Инициализация на SD карта (hotplug поддръжка)
    printf("Инициализация на SD карта...\n");
    sd_spi_init();
    
    // Опит за инициализация на SD карта (не е задължителна при стартиране)
    if (sd_init()) {
        // Зареждане на дисковия имидж (монтира файловата система)
        if (load_disk_image()) {
            sd_card_present = true;
            printf("SD картата е готова при стартиране\n");
        } else {
            printf("ПРЕДУПРЕЖДЕНИЕ: SD картата е налична, но не може да се зареди дисковият имидж\n");
            sd_card_present = true;
        }
    } else {
        printf("ПРЕДУПРЕЖДЕНИЕ: SD картата не е налична при стартиране (hotplug поддръжка активна)\n");
        sd_card_present = false;
    }
    

    
    printf("Системата е готова!\n");
    gpio_put(LED_PIN, 1);
    
    // Главен цикъл
    uint32_t last_check = time_us_32();
    uint8_t last_track = 255;
    last_sd_check = time_us_32();
    
    while (1) {
        // Hotplug проверка на SD карта (на всеки 1 секунда)
        uint32_t current_time = time_us_32();
        if (current_time - last_sd_check > (SD_CHECK_INTERVAL_MS * 1000)) {
            last_sd_check = current_time;
            
            bool card_now_present = sd_check_presence();
            
            if (!sd_card_present && card_now_present) {
                // Картата е вмъкната
                handle_sd_card_insertion();
            } else if (sd_card_present && !card_now_present) {
                // Картата е премахната
                handle_sd_card_removal();
            }
        }
        
        // Проверка на мотор
        bool new_motor_on = gpio_get(GPIO_MOTOR_ON);
        if (new_motor_on != motor_on) {
            motor_on = new_motor_on;
            if (motor_on) {
                printf("Мотор ВКЛЮЧЕН\n");
                // Зареждане на текущата пътека
                if (!load_track(current_track)) {
                    printf("ГРЕШКА: Не може да се зареди пътека %d\n", current_track);
                } else {
                    last_track = current_track;
                }
            } else {
                printf("Мотор ИЗКЛЮЧЕН\n");
            }
        }
        
        // Обработка на фазови стъпки (само когато моторът е включен)
        // Използва interrupt за по-бърза реакция
        if (motor_on) {
            // Проверка за interrupt детектирана промяна
            if (phase_change_detected) {
                phase_change_detected = false;
                handle_phase_step();
                
                // Проверка дали пътеката е променена
                if (current_track != last_track) {
                    if (!load_track(current_track)) {
                        printf("ГРЕШКА: Не може да се зареди пътека %d\n", current_track);
                    } else {
                        last_track = current_track;
                    }
                }
            } else {
                // Fallback на polling ако interrupts не работят
                handle_phase_step();
                
                if (current_track != last_track) {
                    if (!load_track(current_track)) {
                        printf("ГРЕШКА: Не може да се зареди пътека %d\n", current_track);
                    } else {
                        last_track = current_track;
                    }
                }
            }
        }
        
        // Обновяване на TRACK0
        update_track0();
        
        // Обработка на запис с PIO (с interrupt поддръжка)
        if (motor_on && !write_protected) {
            bool write_enable = gpio_get(GPIO_WRITE_ENABLE);
            
            if (write_enable) {
                // Проверка за interrupt детектирани данни
                if (write_data_ready) {
                    write_data_ready = false;
                    process_write_data_pio();
                } else {
                    // Fallback на polling
                    process_write_data_pio();
                }
            } else {
                // Изключване на режим на запис
                if (write_in_progress) {
                    write_in_progress = false;
                    printf("Запис прекъснат\n");
                }
            }
        }
        
        // Обработка на CLI команди
        cli_process();
        
        // Обработка на UI вход
        handle_ui_input();
        
        // Обновяване на дисплея (на всеки 100ms)
        if (time_us_32() - last_display_update > 100000) {
            update_display();
            last_display_update = time_us_32();
        }
        
        // Мигане на LED за индикация (по-бавно когато моторът е изключен)
        static uint32_t led_toggle = 0;
        uint32_t led_interval = motor_on ? 500000 : 1000000;  // 0.5s или 1s
        if (time_us_32() - led_toggle > led_interval) {
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
            led_toggle = time_us_32();
        }
        
        // Малка забавяне за намаляване на натоварването
        sleep_us(50);
    }
    
    return 0;
}
