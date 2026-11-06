/*
 * fw_image.h
 * Internal flash memory map and firmware image records.
 * This file is IDENTICAL in the bootloader and the application project:
 * both must agree on where everything lives.
 *
 *   STM32F407 flash (1 MB) is divided into sectors of different sizes:
 *     sectors 0-3 : 16 KB each   (0x08000000 - 0x0800FFFF)
 *     sector  4   : 64 KB        (0x08010000 - 0x0801FFFF)
 *     sectors 5-11: 128 KB each  (0x08020000 - 0x080FFFFF)
 *   Flash can only be erased a whole sector at a time, so every area
 *   below starts at the beginning of its own sector.
 *
 *   0x08000000  sectors 0-1  BOOTLOADER (32 KB)        never changed by an update
 *   0x08008000  sector  2    APP RECORD                size + CRC of the installed app
 *   0x08020000  sector  5    APPLICATION (128 KB)      what normally runs
 *   0x08040000  sector  6    STAGING (128 KB)          a new image is received here first
 */
#ifndef FW_IMAGE_H
#define FW_IMAGE_H

#include <stdint.h>

#define BOOT_BASE        0x08000000UL

#define META_BASE        0x08008000UL
#define META_SECTOR      2U

#define APP_BASE         0x08020000UL
#define APP_SECTOR       5U
#define APP_MAX_SIZE     (128UL * 1024UL)

#define STAGE_BASE       0x08040000UL
#define STAGE_SECTOR     6U
#define STAGE_DATA       (STAGE_BASE + 0x100UL)          /* image starts after the header */
#define STAGE_MAX_SIZE   (128UL * 1024UL - 0x100UL)

#define FW_MAGIC         0xF1A5C0DEUL

/* Describes one image. Written to flash LAST, magic word very last:
 * if power fails before that, the record is simply "not there". */
typedef struct {
    uint32_t magic;          /* FW_MAGIC when valid        */
    uint32_t size;           /* bytes                      */
    uint32_t crc32;          /* CRC-32 of those bytes      */
    uint32_t reserved;
} fw_record_t;

#define META_RECORD      ((const volatile fw_record_t *)META_BASE)
#define STAGE_RECORD     ((const volatile fw_record_t *)STAGE_BASE)

/* RTC backup register used to ask the bootloader to install the staged image */
#define BKP_UPDATE_REQ   8U
#define UPDATE_REQ_MAGIC 0x55504454UL    /* "UPDT" */
#define BKP_BOOT_RESULT  9U              /* what the bootloader did, for the app to report */
#define BOOT_RES_NONE        0U
#define BOOT_RES_INSTALLED   1U
#define BOOT_RES_BAD_IMAGE   2U

#endif /* FW_IMAGE_H */
