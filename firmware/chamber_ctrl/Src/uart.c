/*
 * uart.c
 * USART2 driver. Received bytes are stored by the interrupt in a ring buffer,
 * and the application reads them later with uart2_getc().
 */
#include "uart.h"
#include "stm32f407_regs.h"

/* ---------------- Receive ring buffer ---------------- */
#define RX_BUF_SIZE   64U                 /* must be a power of two */
#define RX_BUF_MASK   (RX_BUF_SIZE - 1U)

static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;     /* next free slot  - written only by the ISR  */
static volatile uint32_t rx_tail = 0;     /* next unread byte - written only by main code */

/* ---------------- Initialisation ---------------- */
void uart2_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA2 (TX) and PA3 (RX): alternate function mode = binary 10 */
    GPIO_MODER(GPIOA_BASE) &= ~((3UL << (2U * 2U)) | (3UL << (3U * 2U)));
    GPIO_MODER(GPIOA_BASE) |=  ((2UL << (2U * 2U)) | (2UL << (3U * 2U)));

    /* Select AF7 (USART2) for both pins: 4 bits per pin in AFRL */
    GPIO_AFRL(GPIOA_BASE) &= ~((0xFUL << (2U * 4U)) | (0xFUL << (3U * 4U)));
    GPIO_AFRL(GPIOA_BASE) |=  ((7UL   << (2U * 4U)) | (7UL   << (3U * 4U)));

    /* 115200 baud at 16 MHz: 16 000 000 / (16 x 115 200) = 8.68 -> mantissa 8, fraction 11 */
    USART2_BRR = (8UL << 4) | 11UL;

    /* Enable transmitter, receiver, receive interrupt, then the USART itself */
    USART2_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    /* Allow USART2's interrupt in the NVIC: number 38 -> register 1, bit 6 */
    NVIC_ISER(USART2_IRQn / 32U) = (1UL << (USART2_IRQn % 32U));
}

/* ---------------- Interrupt handler ---------------- */
/* Runs automatically each time a byte arrives. Kept short on purpose. */
void USART2_IRQHandler(void)
{
    if (USART2_SR & USART_SR_RXNE) {
        uint8_t  c    = (uint8_t)USART2_DR;           /* reading DR clears RXNE */
        uint32_t next = (rx_head + 1U) & RX_BUF_MASK;

        if (next != rx_tail) {                         /* space left?            */
            rx_buf[rx_head] = c;
            rx_head = next;
        }
        /* else: buffer full, byte is dropped */
    }
}

/* ---------------- Receive ---------------- */
int uart2_getc(void)
{
    if (rx_tail == rx_head) {
        return -1;                                     /* buffer empty */
    }
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1U) & RX_BUF_MASK;
    return (int)c;
}

/* ---------------- Transmit ---------------- */
void uart2_putc(char c)
{
    while ((USART2_SR & USART_SR_TXE) == 0U) {
        /* wait until the transmit register is empty */
    }
    USART2_DR = (uint32_t)(uint8_t)c;
}

void uart2_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart2_putc('\r');
        }
        uart2_putc(*s++);
    }
}

void uart2_put_uint(uint32_t value)
{
    char buf[10];
    int  i = 0;

    do {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U);

    while (i > 0) {
        uart2_putc(buf[--i]);
    }
}
