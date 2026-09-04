/* flash_store — a tiny persistent text store in the LAST 64 KB of the STM32F7 internal flash
 * (0x081F0000..0x081FFFFF). One record: 16-byte header (magic, version, length, CRC32) + text.
 * The region sits inside the last sector in BOTH bank modes (single bank: sector 11, 256 KB;
 * dual bank: sector 23, 128 KB), so it survives a future switch to dual-bank A/B OTA slots as
 * long as the slots stay below 0x081F0000. Writing erases the whole last sector (≈1-2 s during
 * which the CPU stalls on flash: audio underruns are expected — save rarely, on request only).
 * Generic: no MusiCall vocabulary here; the application decides what text it stores. */
#ifndef FLASH_STORE_H
#define FLASH_STORE_H
#define FLASH_STORE_ADDR  0x081F0000u
#define FLASH_STORE_CAP   (0x10000u - 16u)
/* Copies the stored text into buf (NUL-terminated). Returns its length, or -1 if the store is
 * empty/corrupt/too large for cap. */
int flash_store_read(char *buf, int cap);
/* Erases the last sector and programs the record. Returns 0, -1 (bad length), -2 (verify failed). */
int flash_store_write(const char *text, int len);
/* Erases the last sector (store becomes empty). */
int flash_store_erase(void);
#endif
