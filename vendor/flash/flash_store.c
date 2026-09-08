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
static int fs_single_bank(void) { return (FLASH_OPTCR & (1u << 29)) ? 1 : 0; }
static uint8_t fs_last_sector(void) { return fs_single_bank() ? 11 : 23; }

/* ── generic record operations on (base address, erase sector) ── */
static int fs_len_at(uint32_t base, uint32_t capmax) {
    const volatile uint32_t *h = (const volatile uint32_t *) base;
    if (h[0] != FS_MAGIC) return -1;
    uint32_t len = h[2];
    if (len > capmax) return -1;
    if (fs_crc32((const uint8_t *) (base + 16u), len) != h[3]) return -1;
    return (int) len;
}
static int fs_read_at(uint32_t base, uint32_t capmax, char *buf, int cap) {
    int len = fs_len_at(base, capmax);
    if (len < 0 || len >= cap) return -1;
    memcpy(buf, (const uint8_t *) (base + 16u), (size_t) len);
    buf[len] = 0;
    return len;
}
static int fs_erase_at(uint8_t sector) {
    flash_unlock();
    flash_erase_sector(sector, FLASH_CR_PROGRAM_X8);
    flash_lock();
    return 0;
}
static int fs_write_at(uint32_t base, uint32_t capmax, uint8_t sector, const char *text, int len) {
    if (len < 0 || (uint32_t) len > capmax) return -1;
    uint32_t hdr[4] = { FS_MAGIC, 1u, (uint32_t) len, fs_crc32((const uint8_t *) text, (uint32_t) len) };
    flash_unlock();
    flash_erase_sector(sector, FLASH_CR_PROGRAM_X8);
    flash_program(base, (const uint8_t *) hdr, 16u);
    flash_program(base + 16u, (const uint8_t *) text, (uint32_t) len);
    flash_lock();
    /* verify: header + payload read back through the flash interface */
    const volatile uint32_t *h = (const volatile uint32_t *) base;
    if (h[0] != FS_MAGIC || h[2] != (uint32_t) len) return -2;
    if (memcmp((const void *) (base + 16u), text, (size_t) len) != 0) return -2;
    return 0;
}

/* ── CONFIG store (last 64 KB, last sector in both bank modes) ── */
int flash_store_read(char *buf, int cap) { return fs_read_at(FLASH_STORE_ADDR, FLASH_STORE_CAP, buf, cap); }
int flash_store_len(void)                { return fs_len_at(FLASH_STORE_ADDR, FLASH_STORE_CAP); }
int flash_store_erase(void)              { return fs_erase_at(fs_last_sector()); }
int flash_store_write(const char *text, int len) { return fs_write_at(FLASH_STORE_ADDR, FLASH_STORE_CAP, fs_last_sector(), text, len); }

/* ── IDENTITY store (sector 10, single bank only) ── */
int flash_id_read(char *buf, int cap) { return fs_single_bank() ? fs_read_at(FLASH_ID_ADDR, FLASH_ID_CAP, buf, cap) : -1; }
int flash_id_len(void)                { return fs_single_bank() ? fs_len_at(FLASH_ID_ADDR, FLASH_ID_CAP) : -1; }
int flash_id_erase(void)              { return fs_single_bank() ? fs_erase_at(FLASH_ID_SECTOR) : -3; }
int flash_id_write(const char *text, int len) { return fs_single_bank() ? fs_write_at(FLASH_ID_ADDR, FLASH_ID_CAP, FLASH_ID_SECTOR, text, len) : -3; }
