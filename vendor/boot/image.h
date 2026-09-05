/* OTA image header — 512 bytes at the start of a slot, before the vector table (VTOR needs 512-byte
 * alignment for 120 vectors). Written by tools/ota-sign.py; verified by the bootloader (ed25519 over the
 * first MC_IMG_SIGNED bytes, which include the SHA-512 of the body). Generic brick (no MusiCall vocabulary). */
#ifndef MC_IMAGE_H
#define MC_IMAGE_H
#include <stdint.h>
#define MC_IMG_MAGIC      0x5746434Du          /* "MCFW" little-endian */
#define MC_IMG_HDR_SIZE   512u
#define MC_IMG_SIGNED     192u                 /* bytes covered by the signature (header incl. body hash) */
typedef struct __attribute__((packed)) {
    uint32_t magic;            /* MC_IMG_MAGIC */
    uint32_t hdr_version;      /* 1 */
    uint32_t body_size;        /* bytes after the header */
    uint32_t slot;             /* 0 = A, 1 = B (the link edition) */
    uint32_t load_addr;        /* slot base (header address) */
    uint32_t flags;            /* reserved */
    char     version[40];      /* "5134cd7+12" … NUL-padded */
    uint8_t  reserved[64];
    uint8_t  body_sha512[64];  /* SHA-512 of the body (Monocypher optional module, same as the signing tool) */  /* offset 128..191 */
    uint8_t  signature[64];    /* ed25519 (RFC 8032, SHA-512) over bytes [0, 192) */
    uint8_t  pad[512 - 192 - 64];
} mc_image_hdr_t;
#define MC_SLOT_A_BASE  0x08040000u
#define MC_SLOT_B_BASE  0x080C0000u
#define MC_SLOT_SIZE    0x00080000u          /* 512 KB = 2 × 256 KB sectors */
#define MC_SLOT_A_SECTOR_FIRST 5
#define MC_SLOT_B_SECTOR_FIRST 7
#endif
