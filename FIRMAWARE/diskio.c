/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem Module  R0.14b                              /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 2021, ChaN, all right reserved.
/
/ FatFs module is an open source software. Redistribution and use of FatFs in
/ source and binary forms, with or without modification, are permitted provided
/ that the following condition is met:
/
/ 1. Redistributions of source code must retain the above copyright notice,
/    this condition and the following disclaimer.
/
/ This software is provided by the copyright holder and contributors "AS IS"
/ and any warranties related to this software are DISCLAIMED.
/ The copyright owner or contributors be NOT LIABLE for any damages caused
/ by use of this software.
/
/----------------------------------------------------------------------------*/

#include "diskio.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <stdio.h>

// SD Card SPI pins (дефинирани в основния файл)
extern spi_inst_t* SPI_PORT;
extern uint PIN_MISO;
extern uint PIN_CS;
extern uint PIN_SCK;
extern uint PIN_MOSI;

// Функции за SD карта (дефинирани в основния файл)
extern bool sd_init(void);
extern bool sd_read_block(uint32_t block_addr, uint8_t *buffer);
extern bool sd_write_block(uint32_t block_addr, const uint8_t *buffer);
extern bool sd_check_ready(void);

static bool sd_initialized = false;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive number to check the status */
)
{
	if (pdrv != 0) return STA_NOINIT;
	
	if (!sd_initialized) {
		return STA_NOINIT;
	}
	
	return 0;
}

/*-----------------------------------------------------------------------*/
/* Initalize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive number to identify the drive */
)
{
	if (pdrv != 0) return STA_NOINIT;
	
	// Ако картата вече е инициализирана, просто проверяваме дали е все още готова
	if (sd_initialized) {
		// Проверяваме дали картата все още отговаря с проста команда
		if (sd_check_ready()) {
			// Картата е готова
			return 0;
		}
		// Ако проверката не работи, картата може да е загубена - маркираме като неинициализирана
		sd_initialized = false;
	}
	
	// Пълна инициализация
	if (sd_init()) {
		sd_initialized = true;
		return 0;
	}
	
	return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive number to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv != 0) return RES_PARERR;
	if (!sd_initialized) {
		// Ако не е инициализирана, опитваме се да я инициализираме
		if (disk_initialize(pdrv) & STA_NOINIT) {
			return RES_NOTRDY;
		}
	}
	
	for (UINT i = 0; i < count; i++) {
		LBA_t current_sector = sector + i;
		if (!sd_read_block(current_sector, buff + (i * 512))) {
			return RES_ERROR;
		}
	}
	
	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive number to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (pdrv != 0) return RES_PARERR;
	if (!sd_initialized) return RES_NOTRDY;
	
	for (UINT i = 0; i < count; i++) {
		if (!sd_write_block(sector + i, buff + (i * 512))) {
			return RES_ERROR;
		}
	}
	
	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive number (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	if (pdrv != 0) return RES_PARERR;
	
	switch (cmd) {
		case CTRL_SYNC:
			return RES_OK;
		
		case GET_SECTOR_COUNT:
			*(LBA_t*)buff = 0xFFFFFFFF;  // Неизвестен размер
			return RES_OK;
		
		case GET_SECTOR_SIZE:
			*(WORD*)buff = 512;
			return RES_OK;
		
		case GET_BLOCK_SIZE:
			*(DWORD*)buff = 1;
			return RES_OK;
		
		default:
			return RES_PARERR;
	}
}

