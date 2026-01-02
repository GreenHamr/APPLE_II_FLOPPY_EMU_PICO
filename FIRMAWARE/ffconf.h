/*----------------------------------------------------------------------------/
/  FatFs - Configuration file
/-----------------------------------------------------------------------------/
/
/ Copy this file to user project directory as "ffconf.h" and customize it
/ to suit your application. This file is not a part of the FatFs source code
/ module but an application file.
/
/----------------------------------------------------------------------------*/

#ifndef FF_DEFINED
#define FF_DEFINED	8051	/* Revision ID */

/*----------------------------------------------------------------------------/
/ Function Configurations
/----------------------------------------------------------------------------*/

#define FF_FS_READONLY	0
/* This option switches read-only configuration. (0:Read/Write or 1:Read-only)
/  Read-only configuration removes writing API functions, f_write(), f_sync(),
/  f_unlink(), f_mkdir(), f_chmod(), f_rename(), f_truncate(), f_getfree()
/  and optional writing functions as well. */


#define FF_FS_MINIMIZE	0
/* This option defines minimization level to remove some basic API functions.
/
/   0: All basic functions are enabled.
/   1: f_stat(), f_getfree(), f_unlink(), f_mkdir(), f_chmod(), f_utime(),
/      f_truncate() and f_rename() function are removed.
/   2: f_opendir(), f_readdir() and f_closedir() are removed in addition to 1.
/   3: f_lseek() function is removed in addition to 2. */


#define FF_USE_STRFUNC	0
/* This option switches string functions, f_gets(), f_putc(), f_puts() and
/  f_printf().
/
/  0: Disable string functions.
/  1: Enable without LF-CRLF conversion.
/  2: Enable with LF-CRLF conversion. */


#define FF_USE_FIND		1
/* This option switches filtered directory read functions, f_findfirst() and
/  f_findnext(). (0:Disable, 1:Enable 2:Enable with matching altname[] too) */


#define FF_USE_MKFS		0
/* This option switches f_mkfs() function. (0:Disable or 1:Enable) */


#define FF_USE_FASTSEEK	0
/* This option switches fast seek function. (0:Disable or 1:Enable) */


#define FF_USE_EXPAND	0
/* This option switches f_expand() function. (0:Disable or 1:Enable) */


#define FF_USE_CHMOD	0
/* This option switches attribute manipulation functions, f_chmod() and f_utime().
/  (0:Disable or 1:Enable) Also FF_FS_READONLY needs to be 0 when this option is 1. */


#define FF_USE_LABEL	0
/* This option switches volume label functions, f_getlabel() and f_setlabel().
/  (0:Disable or 1:Enable) */


#define FF_USE_FORWARD	0
/* This option switches f_forward() function. (0:Disable or 1:Enable) */


/*----------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/----------------------------------------------------------------------------*/

#define FF_CODE_PAGE	437

/* Character type */
#ifndef TCHAR
#define TCHAR char
#endif
/* This option specifies the OEM code page to be used on the target system.
/  Incorrect code page setting can cause a file open failure.
/
/   437 - U.S.
/   720 - Arabic
/   737 - Greek
/   771 - KBL
/   775 - Baltic
/   850 - Latin 1
/   852 - Latin 2
/   857 - Turkish
/   860 - Portuguese
/   861 - Icelandic
/   862 - Hebrew
/   863 - Canadian French
/   864 - Arabic
/   865 - Nordic
/   866 - Russian
/   869 - Greek 2
/   932 - Japanese (DBCS)
/   936 - Simplified Chinese (DBCS)
/   949 - Korean (DBCS)
/   950 - Traditional Chinese (DBCS)
*/


#define FF_USE_LFN		0
#define FF_MAX_LFN		255
/* The FF_USE_LFN switches the support of long file name (LFN).
/
/   0: Disable LFN. FF_MAX_LFN has no effect.
/   1: Enable LFN with static  working buffer on the BSS. NOT REENTRANT!
/   2: Enable LFN with dynamic working buffer on the STACK.
/   3: Enable LFN with dynamic working buffer on the HEAP.
/
/  The LFN requires a certain working buffer size (FF_MAX_LFN * 2 + 44 bytes).
/  When enable LFN with static working buffer (option 1), the compiler will
/   complain about the memory shortage. In that case, reduce FF_MAX_LFN.
/  The LFN working buffer can be allocated on the stack (option 2) or heap (option 3).
/  When enable LFN with dynamic working buffer (option 2 or 3), the working buffer
/   is allocated on the stack or heap when needed. */


#define FF_LFN_UNICODE	0
/* This option switches the character encoding on the API when LFN is enabled.
/
/   0: ANSI/OEM in current CP (TCHAR = char)
/   1: Unicode in UTF-16 (TCHAR = WCHAR)
/   2: Unicode in UTF-8 (TCHAR = char)
/   3: Unicode in UTF-32 (TCHAR = DWORD)
/
/  Also behavior of string I/O functions will be affected by this option. */


#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
/* This set of options defines size of file name working buffer used to read/write
/  a long file name. The memory for the buffers is allocated on the stack when
/  needed. These options have no effect at read-only configuration (FF_FS_READONLY = 1). */


#define FF_STRFUNC_ENABLE	0
/* This option switches string I/O functions, f_gets(), f_putc(), f_puts() and
/  f_printf().
/
/  0: Disable string I/O functions.
/  1: Enable without LF-CRLF conversion.
/  2: Enable with LF-CRLF conversion. */


#define FF_FS_RPATH		0
/* This option configures support of relative path.
/
/   0: Disable relative path and remove related functions.
/   1: Enable relative path. f_chdir() and f_chdrive() are available.
/   2: f_getcwd() function is available in addition to 1.
/
/  Note: Directory items read via f_readdir() are affected by this option. */


/*----------------------------------------------------------------------------/
/ Drive/Volume Configurations
/----------------------------------------------------------------------------*/

#define FF_VOLUMES		1
/* Number of volumes (logical drives) to be used. (1-10) */


#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
/* FF_STR_VOLUME_ID switches support for string volume ID.
/  When FF_STR_VOLUME_ID is set to 1 or 2, arbitrary strings can be used as drive
/  number in the path name. FF_VOLUME_STRS defines the drive ID strings for each
/  logical drives. Number of items must not be less than FF_VOLUMES. Valid
/  characters for the drive ID strings are: A-Z and 0-9. */


#define FF_MULTI_PARTITION	0
/* This option switches support for multiple volumes on the physical drive.
/  By default (0), each logical drive number is bound to the same physical drive
/  number and only an FAT volume found on the physical drive will be mounted.
/  When this function is enabled (1), each logical drive number can be bound to
/  arbitrary physical drive and partition listed in the VolToPart[]. Also f_fdisk()
/  funciton will be available. */


#define FF_MIN_SS		512
#define FF_MAX_SS		512
/* This set of options configures the range of sector size to be supported. (512,
/  1024, 2048 or 4096) Always set both 512 for most systems, all memory card and
/  harddisk. But a larger value may be required for on-board flash memory and some
/  type of optical media. When FF_MAX_SS is larger than FF_MIN_SS, FatFs is configured
/  for variable sector size and GET_SECTOR_SIZE command must be implemented to the
/  disk_ioctl() function. */


#define FF_USE_TRIM		0
/* This option switches support for ATA-TRIM. (0:Disable or 1:Enable)
/  To enable ATA-TRIM, also CTRL_TRIM command should be implemented to the
/  disk_ioctl() function. */


#define FF_FS_NOFSINFO	0
/* If you need to know correct free space on the FAT32 volume, set bit 0 of this
/  option, and f_getfree() function will return the correct free space. However,
/  this option will cause a performance penalty. */


/*----------------------------------------------------------------------------/
/ System Configurations
/----------------------------------------------------------------------------*/

#define FF_FS_TINY		0
/* This option switches tiny buffer configuration. (0:Normal or 1:Tiny)
/  At the tiny configuration, size of the file object (FIL) is reduced FF_MAX_SS
/  bytes. Instead of private sector buffer eliminated from the file object,
/  common sector buffer in the file system object (FATFS) is used for the file
/  data transfer. */


#define FF_FS_EXFAT		0
/* This option switches support for exFAT file system. (0:Disable or 1:Enable)
/  To enable exFAT, also LFN must be enabled. (FF_USE_LFN >= 1)
/  Note that enabling exFAT discards the C standard compliance. */


#define FF_FS_NORTC		1
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2021
/* The option FF_FS_NORTC switches timestamp feature. If the system does not have
/  an RTC or valid timestamp is not needed, set FF_FS_NORTC = 1 to disable the
/  timestamp feature. Every object modified by FatFs will have a fixed timestamp
/  defined by FF_NORTC_MON, FF_NORTC_MDAY and FF_NORTC_YEAR.
/  In case of FF_FS_NORTC = 0, the get_fattime() function need to be added to the
/  project to read current time from the RTC. */


#define FF_FS_LOCK		0
/* The option FF_FS_LOCK switches file lock feature to control duplicated file open
/  and illegal open mode.
/
/  0: Disable file lock feature. To avoid file corruption, application program
/     should avoid illegal open, remove and rename to the open objects.
/  >0: Enable file lock feature. The value defines how many files/sub-directories
/     can be opened simultaneously under file lock control. Note that the file
/     lock feature is independent of re-entrancy. */


#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT		1000
#define FF_SYNC_t			HANDLE
/* The option FF_FS_REENTRANT switches the re-entrancy (thread safe) of the FatFs
/  module itself. Note that regardless of this option, file access to different
/  volume is always re-entrant and volume control functions, f_mount(), f_mkfs()
/  and f_fdisk() function, are always not re-entrant. Only file/directory access
/  to the same volume is under control of this feature.
/
/   0: Disable re-entrancy. FF_FS_TIMEOUT and FF_SYNC_t have no effect.
/   1: Enable re-entrancy with the option b) for the O/S dependent sync object.
/      To use the option b), the O/S dependent sync object, FF_SYNC_t, must be
/      added to the project. */


/*--- End of configuration options ---*/

#endif /* FF_DEFINED */

