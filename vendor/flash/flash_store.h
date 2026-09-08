/* flash_store — two tiny persistent text stores in the STM32F7 internal flash.
 * One record each: 16-byte header (magic, version, length, CRC32) + text. Generic: no MusiCall
 * vocabulary here; the application decides what text it stores.
 *
 *  CONFIG   (flash_store_*)  last 64 KB, 0x081F0000..0x081FFFFF — inside the last sector in BOTH bank
 *           modes (single bank: sector 11, 256 KB; dual bank: sector 23, 128 KB). Rewritten by every
 *           `save` (erase ≈ 1-2 s during which the CPU stalls on flash: save rarely, on request only).
 *  IDENTITY (flash_id_*)     first 64 KB of sector 10, 0x08180000 (SINGLE BANK ONLY — refused in dual
 *           bank, where that address is another sector). Written when the key is generated and when the
 *           certificate is received, NEVER by `save`: a power cut during a routine save must not be able
 *           to destroy the box's identity (decided 2026-09-08, docs/PKI.md §5). Sits between slot B
 *           (sectors 7-8) and the config: the OTA slots never touch it. */
#ifndef FLASH_STORE_H
#define FLASH_STORE_H
#define FLASH_STORE_ADDR  0x081F0000u
#define FLASH_STORE_CAP   (0x10000u - 16u)
#define FLASH_ID_ADDR     0x08180000u
#define FLASH_ID_SECTOR   10
#define FLASH_ID_CAP      (0x10000u - 16u)
/* Copies the stored text into buf (NUL-terminated). Returns its length, or -1 if the store is
 * empty/corrupt/too large for cap. */
int flash_store_read(char *buf, int cap);
/* Length of the stored text without copying it: -1 if empty/corrupt. */
int flash_store_len(void);
/* Erases the last sector and programs the record. Returns 0, -1 (bad length), -2 (verify failed). */
int flash_store_write(const char *text, int len);
/* Erases the last sector (store becomes empty). */
int flash_store_erase(void);
/* Same three operations on the IDENTITY store. Writes return -3 in dual-bank mode. */
int flash_id_read(char *buf, int cap);
int flash_id_len(void);
int flash_id_write(const char *text, int len);
int flash_id_erase(void);
#endif
