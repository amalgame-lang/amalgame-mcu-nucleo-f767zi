/* MusiCall-Box bootloader (sectors 0-2, 96 KB budget). Runs at reset from the HSI (16 MHz, no clock.c):
 *   1. read bootctl; the slot to try = pending (under trial) else active;
 *   2. verify that slot: header (magic, slot, size, load address), ed25519 signature over the header
 *      (Monocypher, RFC 8032), then SHA-512 of the body — every boot, no shortcut;
 *   3. pending slot: tries++ (written to flash); beyond BOOTCTL_MAX_TRIES → fall back to the other slot
 *      (bootctl: active = other, pending none, fallback = 1); a bad image is never jumped to;
 *   4. start the IWDG (the app kicks it from its loop; a frozen app resets and counts as a try),
 *      set VTOR/MSP, jump to the image's Reset_Handler (vector table right after the 512-byte header).
 * Nothing else is touched (no clock, no USART): the app brings the board up as before. */
#include <stdint.h>
#include <string.h>
#include <libopencm3/stm32/iwdg.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "image.h"
#include "bootctl.h"
#include "ota_pubkey.h"     /* const uint8_t ota_pubkey[32] — tools/ota-pubkey.sh */

#ifndef BOOT_IWDG_MS
#define BOOT_IWDG_MS 8000
#endif
#define SCB_VTOR (*(volatile uint32_t *) 0xE000ED08u)

static uint32_t slot_base(uint8_t slot) { return slot ? MC_SLOT_B_BASE : MC_SLOT_A_BASE; }

static const char *image_check(uint8_t slot) {           /* NULL = good, else the reason */
    uint32_t base = slot_base(slot);
    const mc_image_hdr_t *h = (const mc_image_hdr_t *) base;
    if (h->magic != MC_IMG_MAGIC) return "magic";
    if (h->slot != slot || h->load_addr != base) return "slot";
    if (h->body_size == 0 || h->body_size > MC_SLOT_SIZE - MC_IMG_HDR_SIZE) return "size";
    if (crypto_ed25519_check(h->signature, ota_pubkey, (const uint8_t *) h, MC_IMG_SIGNED) != 0) return "signature";
    uint8_t d[64]; crypto_sha512(d, (const uint8_t *) (base + MC_IMG_HDR_SIZE), h->body_size);
    if (crypto_verify64(d, h->body_sha512) != 0) return "hash";
    return 0;
}

static void __attribute__((noreturn)) jump(uint32_t base) {
    const uint32_t *vt = (const uint32_t *) (base + MC_IMG_HDR_SIZE);
    iwdg_set_period_ms(BOOT_IWDG_MS); iwdg_start();
    SCB_VTOR = base + MC_IMG_HDR_SIZE;
    __asm volatile ("dsb; isb");
    __asm volatile ("msr msp, %0" :: "r" (vt[0]));
    ((void (*)(void)) vt[1])();
    for (;;) {}
}

static void note(bootctl_t *r, const char *msg) { strncpy(r->last_error, msg, sizeof r->last_error - 1); }

void amc_main(void) {                                    /* entry called by startup.c's Reset_Handler */
    bootctl_t r; bootctl_read(&r);
    uint8_t slot = r.pending != BOOTCTL_NONE ? r.pending : r.active;
    uint8_t other = slot ? 0 : 1;
    if (r.pending != BOOTCTL_NONE) {                     /* image under trial: count this boot */
        if (r.tries >= BOOTCTL_MAX_TRIES) {              /* it never confirmed: roll back */
            r.active = other; r.pending = BOOTCTL_NONE; r.tries = 0; r.confirmed = 1; r.fallback = 1; note(&r, "tries");
            bootctl_write(&r); slot = other; other = slot ? 0 : 1;
        } else { r.tries++; bootctl_write(&r); }
    }
    const char *why = image_check(slot);
    if (why) {                                           /* bad image: try the other one, remember why */
        bootctl_t w = r; w.active = other; w.pending = BOOTCTL_NONE; w.tries = 0; w.confirmed = 1; w.fallback = 1; note(&w, why); bootctl_write(&w);
        if (image_check(other) == 0) jump(slot_base(other));
        for (;;) {}                                      /* no bootable image: stay here (ST-Link) */
    }
    jump(slot_base(slot));
}
