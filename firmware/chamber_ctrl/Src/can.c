/*
 * can.c
 * CAN1 driver for the STM32F4 bxCAN controller.
 *
 * CAN in one paragraph: all nodes share two wires (CAN-H, CAN-L). Every
 * message has an IDENTIFIER instead of an address. When two nodes start
 * sending at the same moment, the bits of their identifiers are compared on
 * the wire and the LOWER identifier wins without being damaged ("arbitration")
 * - so the identifier is also the message's priority. The hardware adds a
 * CRC, waits for an acknowledgement from the other nodes and retransmits
 * automatically on errors. The software only fills in identifier, length and
 * up to 8 data bytes.
 */
#include "can.h"
#include "stm32f407_regs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define RX_QUEUE_LEN   8U

static QueueHandle_t     rx_queue    = NULL;
static volatile uint32_t tx_count    = 0U;
static volatile uint32_t tx_dropped  = 0U;
static volatile uint32_t rx_count    = 0U;
static volatile uint32_t rx_overruns = 0U;

/* Wait until a status bit becomes the wanted value, but never forever */
static int wait_bit(volatile uint32_t *reg, uint32_t bit, int want_set)
{
    uint32_t n;
    for (n = 0U; n < 100000U; n++) {
        if (((*reg & bit) != 0U) == (want_set != 0)) {
            return 1;
        }
    }
    return 0;
}

void can1_init(void)
{
    /* 1. Clocks: GPIOB for the pins, CAN1 itself */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_CAN1EN;

    /* 2. PB8 (RX) and PB9 (TX): alternate function 9 = CAN1 */
    GPIO_MODER(GPIOB_BASE) &= ~((3UL << (8U * 2U)) | (3UL << (9U * 2U)));
    GPIO_MODER(GPIOB_BASE) |=  ((2UL << (8U * 2U)) | (2UL << (9U * 2U)));
    GPIO_AFRH(GPIOB_BASE)  &= ~((0xFUL << 0) | (0xFUL << 4));      /* pins 8, 9 */
    GPIO_AFRH(GPIOB_BASE)  |=  ((9UL   << 0) | (9UL   << 4));

    /* 3. Enter initialisation mode (and leave sleep mode). Bit timing and
     *    most settings can only be changed while the controller is in it. */
    CAN_MCR &= ~CAN_MCR_SLEEP;
    CAN_MCR |=  CAN_MCR_INRQ;
    (void)wait_bit(&CAN_MSR, CAN_MSR_INAK, 1);

    /* 4. Options: send mailboxes in request order; recover from bus-off on
     *    its own (after 128 x 11 recessive bits, as the CAN standard allows) */
    CAN_MCR |= CAN_MCR_TXFP | CAN_MCR_ABOM;

    /* 5. Bit timing for 500 kbit/s with the 16 MHz APB1 clock:
     *    prescaler 2 -> one time quantum = 125 ns
     *    bit = 1 (sync) + 13 (segment 1) + 2 (segment 2) = 16 quanta = 2 us
     *    sample point at 14/16 = 87.5 %, the usual value for CAN
     *    Registers hold "value - 1". */
    CAN_BTR = (0UL << 24)          /* SJW  = 1 quantum  */
            | (1UL << 20)          /* TS2  = 2 quanta   */
            | (12UL << 16)         /* TS1  = 13 quanta  */
            | (1UL << 0);          /* BRP  = 2          */

    /* 6. Acceptance filter 0: accept identifiers 0x200-0x21F only.
     *    Mask mode: a received ID passes if (ID & mask) == (filter & mask).
     *    Mask 0x7E0 compares the top 6 bits, so the low 5 bits may be anything.
     *    In the 32-bit registers the 11-bit standard ID sits in bits 21-31. */
    CAN_FMR  |=  CAN_FMR_FINIT;
    CAN_FA1R &= ~1UL;              /* filter 0 off while changing it */
    CAN_FS1R |=  1UL;              /* 32-bit scale                   */
    CAN_FM1R &= ~1UL;              /* mask mode                      */
    CAN_FFA1R &= ~1UL;             /* matches go to FIFO 0           */
    CAN_F0R1 = (0x200UL << 21);
    CAN_F0R2 = (0x7E0UL << 21) | (1UL << 2);   /* also require IDE = 0 (standard ID) */
    CAN_FA1R |=  1UL;
    CAN_FMR  &= ~CAN_FMR_FINIT;

    /* 7. Interrupt when FIFO 0 holds a message */
    CAN_IER |= CAN_IER_FMPIE0;

    /* 8. Leave initialisation mode: the controller joins the bus after it
     *    has seen 11 recessive bits in a row (bus idle) */
    CAN_MCR &= ~CAN_MCR_INRQ;
    (void)wait_bit(&CAN_MSR, CAN_MSR_INAK, 0);
}

int can1_rtos_init(void)
{
    rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(can_frame_t));
    configASSERT(rx_queue != NULL);

    /* Priority level 7: below configMAX_SYSCALL (5), because this ISR calls
     * xQueueSendFromISR. Enabled only now, when the queue exists. */
    NVIC_IPR(CAN1_RX0_IRQn) = 0x70U;
    NVIC_ISER(CAN1_RX0_IRQn / 32U) = (1UL << (CAN1_RX0_IRQn % 32U));
    return 1;
}

/* ---------------- Transmit ---------------- */
static void put_bytes(uint32_t *lo, uint32_t *hi, const uint8_t *d)
{
    /* Byte 0 is the lowest byte of the "low" register (little-endian) */
    *lo = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    *hi = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
}

int can1_send(const can_frame_t *f)
{
    uint32_t mb, lo, hi;
    uint8_t  buf[8] = {0};
    uint32_t i, len = (f->len > 8U) ? 8U : f->len;

    /* Find an empty transmit mailbox (the hardware has three) */
    for (mb = 0U; mb < 3U; mb++) {
        if (CAN_TSR & (CAN_TSR_TME0 << mb)) {
            break;
        }
    }
    if (mb == 3U) {
        tx_dropped++;                   /* all busy: bus overloaded or nobody listening */
        return 0;
    }

    for (i = 0U; i < len; i++) {
        buf[i] = f->data[i];
    }
    put_bytes(&lo, &hi, buf);

    CAN_TDTR(mb) = len;                                  /* data length code */
    CAN_TDLR(mb) = lo;
    CAN_TDHR(mb) = hi;
    CAN_TIR(mb)  = ((uint32_t)(f->id & 0x7FFU) << 21)    /* standard ID; IDE = 0, RTR = 0 */
                 | CAN_TIR_TXRQ;                         /* ...and go: written LAST */
    tx_count++;
    return 1;
}

/* ---------------- Receive ---------------- */
/* Name must match the vector table in the startup file exactly */
void CAN1_RX0_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;

    if (CAN_RF0R & CAN_RF0R_FOVR0) {
        rx_overruns++;                  /* hardware FIFO (3 deep) was full */
        CAN_RF0R = CAN_RF0R_FOVR0;      /* write 1 to clear */
    }

    while (CAN_RF0R & CAN_RF0R_FMP0) {  /* as long as messages are waiting */
        can_frame_t f;
        uint32_t lo = CAN_RDL0R;
        uint32_t hi = CAN_RDH0R;
        uint32_t i;

        f.id  = (uint16_t)((CAN_RI0R >> 21) & 0x7FFU);
        f.len = (uint8_t)(CAN_RDT0R & 0xFU);
        if (f.len > 8U) { f.len = 8U; }
        for (i = 0U; i < 4U; i++) {
            f.data[i]      = (uint8_t)(lo >> (8U * i));
            f.data[i + 4U] = (uint8_t)(hi >> (8U * i));
        }
        CAN_RF0R = CAN_RF0R_RFOM0;      /* release: the next message moves up */

        rx_count++;
        (void)xQueueSendFromISR(rx_queue, &f, &woken);
    }
    portYIELD_FROM_ISR(woken);          /* switch to the CAN task now if it was waiting */
}

int can1_receive(can_frame_t *f, uint32_t wait_ms)
{
    return xQueueReceive(rx_queue, f, pdMS_TO_TICKS(wait_ms)) == pdPASS;
}

uint32_t can1_tx_count(void)     { return tx_count; }
uint32_t can1_tx_dropped(void)   { return tx_dropped; }
uint32_t can1_rx_count(void)     { return rx_count; }
uint32_t can1_rx_overruns(void)  { return rx_overruns; }
uint32_t can1_error_status(void) { return CAN_ESR; }
