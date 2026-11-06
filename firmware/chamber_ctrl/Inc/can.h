/*
 * can.h
 * CAN1 driver (STM32 bxCAN), register level.
 *   PB8 = CAN1_RX, PB9 = CAN1_TX (alternate function 9), 500 kbit/s.
 *   Only identifiers 0x200-0x23F (commands and firmware updates) are received:
 *   the hardware filter throws everything else away, so the CPU never sees it.
 *   Received frames go from the interrupt into a FreeRTOS queue.
 */
#ifndef CAN_H
#define CAN_H

#include <stdint.h>

typedef struct {
    uint16_t id;          /* 11-bit standard identifier: lower = higher priority */
    uint8_t  len;         /* 0-8 data bytes                                       */
    uint8_t  data[8];
} can_frame_t;

void     can1_init(void);
int      can1_rtos_init(void);                 /* creates the receive queue        */

int      can1_send(const can_frame_t *f);      /* 1 = queued in a mailbox, 0 = all 3 busy */
int      can1_receive(can_frame_t *f, uint32_t wait_ms);   /* 1 = got a frame   */

/* Statistics and error state for the console */
uint32_t can1_tx_count(void);
uint32_t can1_tx_dropped(void);
uint32_t can1_rx_count(void);
uint32_t can1_rx_overruns(void);
uint32_t can1_error_status(void);             /* raw ESR register                  */

#endif /* CAN_H */
