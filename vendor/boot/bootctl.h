/* bootctl — boot state record in its own 32 KB sector (sector 3, 0x08018000): which slot is active,
 * which one is pending (freshly written, not yet confirmed), how many boots it was given, and whether
 * the active image confirmed itself. Erase = a whole sector; written by the app on OTA end / confirm
 * and by the bootloader on each try / fallback (a few writes per update, fine for flash endurance). */
#ifndef MC_BOOTCTL_H
#define MC_BOOTCTL_H
#include <stdint.h>
#define BOOTCTL_ADDR    0x08018000u
#define BOOTCTL_SECTOR  3
#define BOOTCTL_MAGIC   0x4C544342u   /* "BCTL" */
#define BOOTCTL_NONE    0xFFu
#define BOOTCTL_MAX_TRIES 3
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;        /* incremented at each write (diagnostics) */
    uint8_t  active;     /* 0 = A, 1 = B: the slot to boot */
    uint8_t  pending;    /* BOOTCTL_NONE, or the slot under trial (== active while trying) */
    uint8_t  tries;      /* boots given to the pending slot */
    uint8_t  confirmed;  /* 1 once the active image called bootctl_confirm() */
    uint8_t  fallback;   /* 1 if the last update was rolled back (cleared by the next OTA end) */
    uint8_t  reserved[3];
    char     last_error[32];
    uint32_t crc;        /* CRC32 of the bytes above */
} bootctl_t;
int  bootctl_read(bootctl_t *out);        /* 0 ok, -1 empty/corrupt (out zeroed, active = A) */
int  bootctl_write(const bootctl_t *in);  /* erases the sector, programs, verifies: 0 ok */
/* application side */
int  bootctl_confirm(void);               /* active image OK: pending cleared, tries 0 (no write if already confirmed) */
int  bootctl_set_pending(uint8_t slot);   /* after a successful OTA write: boot `slot` at the next reset, on trial */
uint8_t bootctl_active(void);
uint32_t bootctl_crc32(const void *p, uint32_t n);
#endif
