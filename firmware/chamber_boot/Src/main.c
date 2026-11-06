/*
 * main.c - Chamber controller BOOTLOADER (project chamber_boot)
 *
 * The first program that runs after every reset (it sits at 0x08000000,
 * where the CPU looks for its vector table). Its only jobs:
 *
 *   1. If the application asked for it (backup register flag) and a valid
 *      image is waiting in the staging sector: copy it over the application
 *      and check the copy.
 *   2. Check the application's CRC against its record.
 *   3. Jump to the application - or, if it is corrupted, refuse and stay
 *      here (chamber outputs are all off after reset), red LED blinking.
 *
 * Rules a bootloader must follow:
 *   - small and simple (it can never be updated in the field)
 *   - never leaves the system with NO runnable program if power fails in
 *     the middle: every step can be repeated safely on the next boot.
 */
#include <stdint.h>
#include "stm32f407_regs.h"
#include "fw_image.h"
#include "crc32.h"
#include "iflash.h"

#define BOOT_VERSION   "1.0"
#define SCB_VTOR       REG32(0xE000ED08UL)
#define LED_RED_PIN    14U

/* ---------------- Minimal UART (transmit only, polled) ---------------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
    GPIO_MODER(GPIOA_BASE) &= ~(3UL << (2U * 2U));
    GPIO_MODER(GPIOA_BASE) |=  (2UL << (2U * 2U));      /* PA2 = alternate function */
    GPIO_AFRL(GPIOA_BASE)  &= ~(0xFUL << (2U * 4U));
    GPIO_AFRL(GPIOA_BASE)  |=  (7UL   << (2U * 4U));    /* AF7 = USART2 TX */
    USART2_BRR = (8UL << 4) | 11UL;                     /* 115200 at 16 MHz */
    USART2_CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void putc_(char c)
{
    while ((USART2_SR & USART_SR_TXE) == 0U) {
    }
    USART2_DR = (uint32_t)(uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s) {
        if (*s == '\n') { putc_('\r'); }
        putc_(*s++);
    }
}

static void puthex(uint32_t v)
{
    int i;
    puts_("0x");
    for (i = 28; i >= 0; i -= 4) {
        uint32_t n = (v >> i) & 0xFU;
        putc_((char)(n < 10U ? '0' + n : 'A' + n - 10U));
    }
}

static void putdec(uint32_t v)
{
    char b[11];
    int  i = 10;
    b[i] = '\0';
    do { b[--i] = (char)('0' + v % 10U); v /= 10U; } while (v && i);
    puts_(&b[i]);
}

/* ---------------- Checks ---------------- */
static uint32_t crc_of(uint32_t addr, uint32_t size)
{
    return crc32_update(0U, (const uint8_t *)addr, size);
}

static int record_ok(const volatile fw_record_t *r, uint32_t data_addr, uint32_t max)
{
    if (r->magic != FW_MAGIC || r->size == 0U || r->size > max) {
        return 0;
    }
    return crc_of(data_addr, r->size) == r->crc32;
}

/* The first two words of a program are its initial stack pointer (must be
 * in RAM) and its reset address (must be inside the application area). */
static int vectors_look_sane(void)
{
    uint32_t sp = *(volatile uint32_t *)APP_BASE;
    uint32_t pc = *(volatile uint32_t *)(APP_BASE + 4U);
    return (sp >= 0x20000000UL && sp <= 0x20040000UL) &&
           (pc >= APP_BASE && pc < APP_BASE + APP_MAX_SIZE);
}

/* ---------------- Install: staging -> application ---------------- */
static int install(void)
{
    uint32_t size = STAGE_RECORD->size;
    uint32_t crc  = STAGE_RECORD->crc32;
    uint32_t i;

    puts_("[BOOT] Installing new firmware: ");
    putdec(size);
    puts_(" bytes, CRC ");
    puthex(crc);
    puts_("\n");

    iflash_unlock();
    /* Erase the record FIRST: if power fails during the copy, the next boot
     * sees "application not valid" and simply installs again. */
    if (!iflash_erase_sector(META_SECTOR) || !iflash_erase_sector(APP_SECTOR)) {
        iflash_lock();
        puts_("[BOOT] Erase failed\n");
        return 0;
    }
    for (i = 0U; i < size; i += 4U) {
        if (!iflash_program_word(APP_BASE + i, *(volatile uint32_t *)(STAGE_DATA + i))) {
            iflash_lock();
            puts_("[BOOT] Programming failed at ");
            puthex(APP_BASE + i);
            puts_("\n");
            return 0;
        }
    }
    if (crc_of(APP_BASE, size) != crc) {
        iflash_lock();
        puts_("[BOOT] Verify failed: copy does not match\n");
        return 0;
    }
    /* Commit: write the record, magic number LAST */
    (void)iflash_program_word(META_BASE + 4U,  size);
    (void)iflash_program_word(META_BASE + 8U,  crc);
    (void)iflash_program_word(META_BASE + 0U,  FW_MAGIC);
    iflash_lock();
    puts_("[BOOT] Installed and verified.\n");
    return 1;
}

/* ---------------- Jump ---------------- */
static void jump_to_app(void)
{
    uint32_t sp = *(volatile uint32_t *)APP_BASE;
    uint32_t pc = *(volatile uint32_t *)(APP_BASE + 4U);

    SCB_VTOR = APP_BASE;               /* interrupts now use the app's vector table */
    __asm volatile (
        "msr msp, %0 \n"               /* the app's own stack                */
        "bx  %1      \n"               /* ...and go: never comes back        */
        : : "r" (sp), "r" (pc)
    );
}

int main(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;  /* backup registers: request flag in, result out */
    PWR_CR      |= PWR_CR_DBP;

    uart_init();
    puts_("\n[BOOT] Chamber bootloader v" BOOT_VERSION "\n");
    RTC_BKP(BKP_BOOT_RESULT) = BOOT_RES_NONE;

    /* 1. Install only when the application ASKED for it. The flag is cleared
     *    only after the attempt: if power fails during the copy, the flag is
     *    still set at the next boot and the install simply runs again. */
    if (RTC_BKP(BKP_UPDATE_REQ) == UPDATE_REQ_MAGIC) {
        if (record_ok(STAGE_RECORD, STAGE_DATA, STAGE_MAX_SIZE)) {
            RTC_BKP(BKP_BOOT_RESULT) = install() ? BOOT_RES_INSTALLED : BOOT_RES_BAD_IMAGE;
        } else {
            puts_("[BOOT] Update requested but the staged image is not valid - ignored\n");
            RTC_BKP(BKP_BOOT_RESULT) = BOOT_RES_BAD_IMAGE;
        }
        RTC_BKP(BKP_UPDATE_REQ) = 0U;
    }

    /* 2. Check the application before running it */
    if (META_RECORD->magic == FW_MAGIC) {
        if (record_ok(META_RECORD, APP_BASE, APP_MAX_SIZE)) {
            puts_("[BOOT] Application CRC OK (");
            puthex(META_RECORD->crc32);
            puts_("), starting\n");
            jump_to_app();
        }
        puts_("[BOOT] APPLICATION CORRUPTED: CRC does not match its record. Not starting it.\n");
    } else if (vectors_look_sane()) {
        /* Loaded by the debugger or Renode, never installed by us: no record */
        puts_("[BOOT] Application has no CRC record (loaded by debugger) - starting anyway\n");
        jump_to_app();
    } else {
        puts_("[BOOT] NO APPLICATION FOUND.\n");
    }

    /* 3. Nothing safe to run: stay here. All outputs are off after reset. */
    puts_("[BOOT] Staying in the bootloader. Reload the firmware with the debugger.\n");
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIO_MODER(GPIOD_BASE) |= (1UL << (LED_RED_PIN * 2U));
    for (;;) {
        volatile uint32_t n;
        GPIO_BSRR(GPIOD_BASE) = (1UL << LED_RED_PIN);
        for (n = 0U; n < 800000U; n++) { }
        GPIO_BSRR(GPIOD_BASE) = (1UL << (LED_RED_PIN + 16U));
        for (n = 0U; n < 800000U; n++) { }
    }
}
