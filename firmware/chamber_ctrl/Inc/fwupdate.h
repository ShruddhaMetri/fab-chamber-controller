/*
 * fwupdate.h
 * Receiving a new firmware image over CAN from the fab host, while the
 * current firmware keeps running. The image goes into the STAGING sector;
 * only the bootloader ever writes the application area.
 *
 *   ID     Direction       Bytes
 *   -----  --------------  --------------------------------------------------
 *   0x220  host->chamber   BEGIN:   0-3 image size, 4-7 CRC-32 (little-endian)
 *   0x221  host->chamber   DATA:    0-1 sequence number, 2-7 up to 6 image bytes
 *   0x222  host->chamber   END:     (no data) - check the CRC
 *   0x223  host->chamber   INSTALL: (no data) - restart into the bootloader
 *   0x181  chamber->host   REPLY:   0 which (0x20-0x23), 1 status, 2-3 sequence
 *
 * The host sends the next DATA frame only after the reply to the previous
 * one ("stop-and-wait"), so the receiver can never be overrun.
 */
#ifndef FWUPDATE_H
#define FWUPDATE_H

#include <stdint.h>
#include "can.h"

#define CANID_FW_BEGIN    0x220U
#define CANID_FW_DATA     0x221U
#define CANID_FW_END      0x222U
#define CANID_FW_INSTALL  0x223U
#define CANID_FW_REPLY    0x181U

#define FW_OK             0U
#define FW_REFUSED        1U     /* a recipe is running: no update now      */
#define FW_BAD_SEQ        2U     /* frame out of order (reply says expected) */
#define FW_TOO_BIG        3U
#define FW_CRC_FAIL       4U     /* the image arrived damaged               */
#define FW_FLASH_ERR      5U
#define FW_NOT_READY      6U     /* DATA without BEGIN, INSTALL without END  */

int  fwupdate_is_frame(uint16_t id);          /* 1 for 0x220-0x223                */
void fwupdate_handle(const can_frame_t *f);   /* called by the CANlink task       */
void fwupdate_print(void);                    /* the 'fw' console command         */

#endif /* FWUPDATE_H */
