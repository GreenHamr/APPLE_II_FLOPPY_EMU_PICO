/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT Filesystem Module  R0.14b                              /
/-----------------------------------------------------------------------------/
/
/ Copyright (C) 2021, ChaN, all right reserved.
/
/ Забележка: Това е опростена версия на FatFS. За пълна функционалност,
/ моля изтеглете официалната FatFS библиотека от:
/ http://elm-chan.org/fsw/ff/00index_e.html
/
/----------------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include <string.h>

/* За опростена версия, използваме само основните функции */
/* За пълна версия, моля използвайте официалната FatFS библиотека */

// Опростена имплементация - само за демонстрация
// В реална употреба, моля използвайте официалната FatFS библиотека

FRESULT f_mount (FATFS* fs, const TCHAR* path, BYTE opt) {
    (void)path;
    (void)opt;
    if (disk_initialize(0) != 0) {
        return FR_NOT_READY;
    }
    fs->fs_type = 1;  // FAT32
    return FR_OK;
}

FRESULT f_open (FIL* fp, const TCHAR* path, BYTE mode) {
    (void)fp;
    (void)path;
    (void)mode;
    // Опростена версия - трябва да се имплементира с официалната библиотека
    return FR_OK;
}

FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br) {
    (void)fp;
    (void)buff;
    (void)btr;
    if (br) *br = 0;
    // Опростена версия - трябва да се имплементира с официалната библиотека
    return FR_OK;
}

FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw) {
    (void)fp;
    (void)buff;
    (void)btw;
    if (bw) *bw = 0;
    // Опростена версия - трябва да се имплементира с официалната библиотека
    return FR_OK;
}

FRESULT f_sync (FIL* fp) {
    (void)fp;
    return FR_OK;
}

FRESULT f_lseek (FIL* fp, FSIZE_t ofs) {
    (void)fp;
    (void)ofs;
    // Опростена версия - трябва да се имплементира с официалната библиотека
    return FR_OK;
}

FRESULT f_close (FIL* fp) {
    (void)fp;
    return FR_OK;
}

FRESULT f_opendir (DIR* dp, const TCHAR* path) {
    (void)dp;
    (void)path;
    return FR_OK;
}

FRESULT f_closedir (DIR* dp) {
    (void)dp;
    return FR_OK;
}

FRESULT f_readdir (DIR* dp, FILINFO* fno) {
    (void)dp;
    (void)fno;
    return FR_NO_FILE;
}

FRESULT f_findfirst (DIR* dp, FILINFO* fno, const TCHAR* path, const TCHAR* pattern) {
    (void)dp;
    (void)fno;
    (void)path;
    (void)pattern;
    return FR_NO_FILE;
}

FRESULT f_findnext (DIR* dp, FILINFO* fno) {
    (void)dp;
    (void)fno;
    return FR_NO_FILE;
}

