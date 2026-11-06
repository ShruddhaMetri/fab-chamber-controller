/*
 * fwupdate.c
 * Firmware update receiver (runs inside the CANlink task).
 *
 *   BEGIN  : check it is allowed, stretch the watchdog, erase staging
 *   DATA   : check the sequence number, program the bytes (32 bits at a time)
 *   END    : CRC-32 over the whole staged image; only if it matches, write
 *            the staging record (magic number last) = "image complete"
 *   INSTALL: set the request flag in a backup register and restart.
 *            The BOOTLOADER copies staging -> application and checks it again.
 *
 * The running firmware never overwrites itself: if anything goes wrong
 * during the transfer, the old firmware simply keeps running.
 */
#include "fwupdate.h"
#include "fw_image.h"
#include "iflash.h"
#include "crc32.h"
#include "stm32f407_regs.h"
#include "health.h"
#include "process.h"
#include "uart.h"
#include "app_config.h"

typedef enum { ST_IDLE = 0, ST_RECEIVING, ST_VERIFIED } fw_state_t;

static fw_state_t state      = ST_IDLE;
static uint32_t   img_size   = 0U;
static uint32_t   img_crc    = 0U;
static uint32_t   received   = 0U;      /* bytes accepted so far              */
static uint16_t   next_seq   = 0U;
static uint8_t    wbuf[4];              /* bytes waiting to make a full word  */
static uint32_t   wfill      = 0U;
static uint32_t   waddr      = 0U;      /* next flash address to program      */
static uint32_t   last_crc   = 0U;      /* computed at END, for the 'fw' view */

static uint32_t get32(const uint8_t *d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

static void reply(uint8_t which, uint8_t status, uint16_t seq)
{
    can_frame_t r = { CANID_FW_REPLY, 4U, { which, status, (uint8_t)(seq & 0xFFU), (uint8_t)(seq >> 8) } };
    (void)can1_send(&r);
}

static void end_session(void)
{
    iflash_lock();
    health_watchdog_set_timeout(1000U);  /* back to normal */
}

static int flush_word(void)
{
    uint32_t w;
    while (wfill < 4U) {
        wbuf[wfill++] = 0xFFU;           /* pad: erased flash value */
    }
    w = get32(wbuf);
    wfill = 0U;
    if (!iflash_program_word(waddr, w)) {
        return 0;
    }
    waddr += 4U;
    return 1;
}

int fwupdate_is_frame(uint16_t id)
{
    return (id >= CANID_FW_BEGIN) && (id <= CANID_FW_INSTALL);
}

static void on_begin(const can_frame_t *f)
{
    if (f->len < 8U) {
        reply(0x20, FW_NOT_READY, 0U);
        return;
    }
    if (process_is_running()) {          /* never during a deposition */
        reply(0x20, FW_REFUSED, 0U);
        return;
    }
    if (state == ST_RECEIVING) {
        end_session();                   /* a new BEGIN restarts cleanly */
    }
    img_size = get32(&f->data[0]);
    img_crc  = get32(&f->data[4]);
    if (img_size == 0U || img_size > STAGE_MAX_SIZE) {
        state = ST_IDLE;
        reply(0x20, FW_TOO_BIG, 0U);
        return;
    }

    /* Erasing 128 KB stalls the CPU for 1-2 s on a real chip: longer than
     * the normal 1 s watchdog, so stretch it for the whole session. */
    health_watchdog_set_timeout(4000U);
    iflash_unlock();
    if (!iflash_erase_sector(STAGE_SECTOR)) {
        end_session();
        state = ST_IDLE;
        reply(0x20, FW_FLASH_ERR, 0U);
        return;
    }
    received = 0U;
    next_seq = 0U;
    wfill    = 0U;
    waddr    = STAGE_DATA;
    state    = ST_RECEIVING;
    uart2_puts("\n[FW] Receiving new firmware: ");
    uart2_put_uint(img_size);
    uart2_puts(" bytes over CAN...\n> ");
    reply(0x20, FW_OK, 0U);
}

static void on_data(const can_frame_t *f)
{
    uint16_t seq;
    uint32_t i, n;

    if (state != ST_RECEIVING || f->len < 2U) {
        reply(0x21, FW_NOT_READY, 0U);
        return;
    }
    seq = (uint16_t)(f->data[0] | (f->data[1] << 8));
    if (seq != next_seq) {
        reply(0x21, FW_BAD_SEQ, next_seq);       /* tells the host where to resume */
        return;
    }
    n = f->len - 2U;
    for (i = 0U; i < n && received < img_size; i++) {
        wbuf[wfill++] = f->data[2U + i];
        received++;
        if (wfill == 4U && !flush_word()) {
            end_session();
            state = ST_IDLE;
            reply(0x21, FW_FLASH_ERR, seq);
            return;
        }
    }
    next_seq++;
    reply(0x21, FW_OK, seq);
}

static void on_end(void)
{
    if (state != ST_RECEIVING || received != img_size) {
        reply(0x22, FW_NOT_READY, next_seq);
        return;
    }
    if (wfill > 0U && !flush_word()) {
        end_session();
        state = ST_IDLE;
        reply(0x22, FW_FLASH_ERR, 0U);
        return;
    }
    last_crc = crc32_update(0U, (const uint8_t *)STAGE_DATA, img_size);
    if (last_crc != img_crc) {
        end_session();
        state = ST_IDLE;
        uart2_puts("\n[FW] Image REJECTED: CRC mismatch (damaged in transfer)\n> ");
        reply(0x22, FW_CRC_FAIL, 0U);
        return;
    }
    /* Image complete and correct: write the staging record, magic LAST */
    if (!iflash_program_word(STAGE_BASE + 4U, img_size) ||
        !iflash_program_word(STAGE_BASE + 8U, img_crc)  ||
        !iflash_program_word(STAGE_BASE + 0U, FW_MAGIC)) {
        end_session();
        state = ST_IDLE;
        reply(0x22, FW_FLASH_ERR, 0U);
        return;
    }
    end_session();
    state = ST_VERIFIED;
    uart2_puts("\n[FW] New image verified (CRC OK). Waiting for INSTALL from the host.\n> ");
    reply(0x22, FW_OK, 0U);
}

static void on_install(void)
{
    if (state != ST_VERIFIED) {
        reply(0x23, FW_NOT_READY, 0U);
        return;
    }
    if (process_is_running()) {
        reply(0x23, FW_REFUSED, 0U);
        return;
    }
    reply(0x23, FW_OK, 0U);
    RTC_BKP(BKP_UPDATE_REQ) = UPDATE_REQ_MAGIC;   /* message to the bootloader */
    uart2_panic_puts("\n[FW] Restarting into the bootloader to install...\n");
    health_reset(RR_UPDATE, 0U, "updt");          /* safe state, record, reset */
}

void fwupdate_handle(const can_frame_t *f)
{
    switch (f->id) {
    case CANID_FW_BEGIN:   on_begin(f); break;
    case CANID_FW_DATA:    on_data(f);  break;
    case CANID_FW_END:     on_end();    break;
    case CANID_FW_INSTALL: on_install(); break;
    default: break;
    }
}

/* ---------------- 'fw' console command ---------------- */
static void put_hex(uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    int i;
    uart2_puts("0x");
    for (i = 28; i >= 0; i -= 4) {
        uart2_putc(hx[(v >> i) & 0xFU]);
    }
}

static void print_record(const char *name, const volatile fw_record_t *r, uint32_t data)
{
    uart2_puts(name);
    if (r->magic != FW_MAGIC) {
        uart2_puts("none\n");
        return;
    }
    uart2_put_uint(r->size);
    uart2_puts(" bytes, CRC ");
    put_hex(r->crc32);
    uart2_puts(crc32_update(0U, (const uint8_t *)data, r->size) == r->crc32 ? " (checks OK)\n" : " (MISMATCH)\n");
}

void fwupdate_print(void)
{
    uart2_puts("Running     : " FW_NAME " v" FW_VERSION ", built " __DATE__ " " __TIME__ "\n");
    uart2_puts("Located at  : ");  put_hex(APP_BASE);  uart2_puts(" (bootloader at ");
    put_hex(BOOT_BASE);  uart2_puts(")\n");
    print_record("Installed   : ", META_RECORD, APP_BASE);
    print_record("Staged      : ", STAGE_RECORD, STAGE_DATA);
    uart2_puts("Transfer    : ");
    if (state == ST_RECEIVING) {
        uart2_put_uint(received);
        uart2_puts(" / ");
        uart2_put_uint(img_size);
        uart2_puts(" bytes received\n");
    } else if (state == ST_VERIFIED) {
        uart2_puts("complete and verified, waiting for INSTALL\n");
    } else {
        uart2_puts("none\n");
    }
}
