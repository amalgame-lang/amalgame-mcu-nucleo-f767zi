#include <string.h>
#include <libopencm3/stm32/flash.h>
#include "bootctl.h"

uint32_t bootctl_crc32(const void *p, uint32_t n) {
    const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
int bootctl_read(bootctl_t *out) {
    const bootctl_t *r = (const bootctl_t *) BOOTCTL_ADDR;
    if (r->magic == BOOTCTL_MAGIC && r->crc == bootctl_crc32(r, sizeof *r - 4u)) { memcpy(out, r, sizeof *out); return 0; }
    memset(out, 0, sizeof *out); out->magic = BOOTCTL_MAGIC; out->active = 0; out->pending = BOOTCTL_NONE; out->confirmed = 1;
    return -1;
}
int bootctl_write(const bootctl_t *in) {
    bootctl_t r = *in; r.magic = BOOTCTL_MAGIC; r.seq++; r.crc = bootctl_crc32(&r, sizeof r - 4u);
    flash_unlock();
    flash_erase_sector(BOOTCTL_SECTOR, FLASH_CR_PROGRAM_X8);
    flash_program(BOOTCTL_ADDR, (const uint8_t *) &r, sizeof r);
    flash_lock();
    return memcmp((const void *) BOOTCTL_ADDR, &r, sizeof r) == 0 ? 0 : -2;
}
int bootctl_confirm(void) {
    bootctl_t r; bootctl_read(&r);
    if (r.confirmed && r.pending == BOOTCTL_NONE) return 0;
    r.confirmed = 1; r.pending = BOOTCTL_NONE; r.tries = 0;
    return bootctl_write(&r);
}
int bootctl_set_pending(uint8_t slot) {
    bootctl_t r; bootctl_read(&r);
    r.active = slot; r.pending = slot; r.tries = 0; r.confirmed = 0; r.fallback = 0; r.last_error[0] = 0;
    return bootctl_write(&r);
}
uint8_t bootctl_active(void) { bootctl_t r; bootctl_read(&r); return r.active; }
