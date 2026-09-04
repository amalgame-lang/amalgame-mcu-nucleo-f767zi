#include <stdint.h>
#include <string.h>
#include <libopencm3/stm32/flash.h>
#include "flash_store.h"

#define FS_MAGIC 0x4746434Du   /* "MCFG" little-endian */

static uint32_t fs_crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* FLASH_OPTCR bit 29 = nDBANK: 1 = single bank (12 sectors, last = 11), 0 = dual bank (24, last = 23).
 * libopencm3 renumbers >= 12 internally (bank 2 encoding). */
static uint8_t fs_last_sector(void) { return (FLASH_OPTCR & (1u << 29)) ? 11 : 23; }

int flash_store_read(char *buf, int cap) {
    const volatile uint32_t *h = (const volatile uint32_t *) FLASH_STORE_ADDR;
    if (h[0] != FS_MAGIC) return -1;
    uint32_t len = h[2];
    if (len > FLASH_STORE_CAP || (int) len >= cap) return -1;
    const uint8_t *d = (const uint8_t *) (FLASH_STORE_ADDR + 16u);
    if (fs_crc32(d, len) != h[3]) return -1;
    memcpy(buf, d, len);
    buf[len] = 0;
    return (int) len;
}

int flash_store_erase(void) {
    flash_unlock();
    flash_erase_sector(fs_last_sector(), FLASH_CR_PROGRAM_X8);
    flash_lock();
    return 0;
}

int flash_store_write(const char *text, int len) {
    if (len < 0 || (uint32_t) len > FLASH_STORE_CAP) return -1;
    uint32_t hdr[4] = { FS_MAGIC, 1u, (uint32_t) len, fs_crc32((const uint8_t *) text, (uint32_t) len) };
    flash_unlock();
    flash_erase_sector(fs_last_sector(), FLASH_CR_PROGRAM_X8);
    flash_program(FLASH_STORE_ADDR, (const uint8_t *) hdr, 16u);
    flash_program(FLASH_STORE_ADDR + 16u, (const uint8_t *) text, (uint32_t) len);
    flash_lock();
    /* verify: header + payload read back through the flash interface */
    const volatile uint32_t *h = (const volatile uint32_t *) FLASH_STORE_ADDR;
    if (h[0] != FS_MAGIC || h[2] != (uint32_t) len) return -2;
    if (memcmp((const void *) (FLASH_STORE_ADDR + 16u), text, (size_t) len) != 0) return -2;
    return 0;
}
