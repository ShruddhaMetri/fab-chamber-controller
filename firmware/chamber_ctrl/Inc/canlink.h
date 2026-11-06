/*
 * canlink.h
 * The chamber's CAN interface to the fab host (the factory computer that
 * supervises many tools). Our own small application protocol on top of CAN:
 *
 *   ID     Direction       When              Bytes (multi-byte values little-endian)
 *   -----  --------------  ----------------  --------------------------------------------
 *   0x080  chamber->host   at once, on event 0 event code, 1 fault bits, 2 process step, 3 sequence no.
 *   0x100  chamber->host   every 500 ms      0 fault bits, 1 process step, 2-3 temperature (0.1 C),
 *                                            4-5 pressure (mTorr), 6 heater %, 7 valve %
 *   0x101  chamber->host   every 500 ms      0-1 SiH4 sccm, 2-3 N2O sccm, 4-5 deposit seconds left,
 *                                            6 alive counter, 7 host link (1 = up)
 *   0x180  chamber->host   reply to command  0 command, 1 result, 2 argument
 *   0x200  host->chamber   command           0 command (1 RUN, 2 ABORT, 3 PING), 1 argument
 *   0x210  host->chamber   every second      heartbeat: "the host is alive"
 *   0x220-0x223, 0x181                      firmware update, see fwupdate.h
 *
 * The lowest identifier wins arbitration, so events (0x080) always get on
 * the bus before routine status (0x100), even when both want to send.
 */
#ifndef CANLINK_H
#define CANLINK_H

#include <stdint.h>

#define CANID_EVENT      0x080U
#define CANID_STATUS     0x100U
#define CANID_PROCESS    0x101U
#define CANID_REPLY      0x180U
#define CANID_COMMAND    0x200U
#define CANID_HEARTBEAT  0x210U

/* Event codes (byte 0 of 0x080) */
#define EV_BOOT          1U
#define EV_FAULT         2U     /* fault bits changed (tripped or cleared) */
#define EV_STEP          3U     /* process step changed                     */

/* Commands (byte 0 of 0x200) and results (byte 1 of 0x180) */
#define CMD_RUN          1U
#define CMD_ABORT        2U
#define CMD_PING         3U
#define RES_OK           0U
#define RES_REFUSED      1U     /* busy or fault active          */
#define RES_BAD_RECIPE   2U     /* slot empty or corrupt         */
#define RES_UNKNOWN      3U     /* command not understood        */

void canlink_task(void *arg);   /* the FreeRTOS task body */
void canlink_print(void);       /* the 'can' console command */

#endif /* CANLINK_H */
