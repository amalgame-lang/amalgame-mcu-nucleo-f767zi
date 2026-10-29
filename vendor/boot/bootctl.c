#include <string.h>
#include <libopencm3/stm32/flash.h>
#include "bootctl.h"

uint32_t bootctl_crc32(const void *p, uint32_t n) {
    const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= b[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
/* COPIE DE SECOURS (relecture du 2026-10-26). Un seul enregistrement effacé-puis-reprogrammé : une coupure dans
 * la fenêtre (à chaque essai de démarrage, à `ota end`) le rendait illisible → repli « A, confirmé, rien à
 * l'essai » : une image neuve tournait alors HORS essai, et si elle se figeait, plus aucun retour arrière.
 * Désormais : la copie (secteur 4, libre derrière le chargeur) est écrite D'ABORD, le secteur 3 ENSUITE ; la
 * lecture prend l'enregistrement valide de plus grand seq (égalité : secteur 3). À tout instant l'un des deux
 * est entier. Un chargeur d'avant ne lit que le secteur 3, toujours écrit en dernier : il voit ce qu'il voyait.
 * (Le secteur 4 appartient au code d'une image monolithique : celle-ci n'écrit jamais bootctl, net_lwip.am.) */
static int rec_ok(const bootctl_t *r) { return r->magic == BOOTCTL_MAGIC && r->crc == bootctl_crc32(r, sizeof *r - 4u); }
int bootctl_read(bootctl_t *out) {
    const bootctl_t *a = (const bootctl_t *) BOOTCTL_ADDR, *b = (const bootctl_t *) BOOTCTL_BAK_ADDR;
    const bootctl_t *r = rec_ok(a) ? a : 0;
    if (rec_ok(b) && (!r || (int32_t) (b->seq - r->seq) > 0)) r = b;
    if (r) { memcpy(out, r, sizeof *out); return 0; }
    memset(out, 0, sizeof *out); out->magic = BOOTCTL_MAGIC; out->active = 0; out->pending = BOOTCTL_NONE; out->confirmed = 1;
    return -1;
}
static int rec_write(uint32_t addr, uint8_t sector, const bootctl_t *r) {
    flash_unlock(); flash_clear_status_flags();
    flash_erase_sector(sector, FLASH_CR_PROGRAM_X8);
    flash_program(addr, (const uint8_t *) r, sizeof *r);
    flash_lock();
    return memcmp((const void *) addr, r, sizeof *r) == 0 ? 0 : -2;
}
int bootctl_write(const bootctl_t *in) {
    bootctl_t r = *in; r.magic = BOOTCTL_MAGIC; r.seq++; r.crc = bootctl_crc32(&r, sizeof r - 4u);
    rec_write(BOOTCTL_BAK_ADDR, BOOTCTL_BAK_SECTOR, &r);      /* copie d'abord (son échec n'empêche pas le principal) */
    return rec_write(BOOTCTL_ADDR, BOOTCTL_SECTOR, &r);
}
/* Confirme le slot QUI TOURNE (relecture du 2026-10-26) : avec un chargeur d'avant et une coupure entre les deux
 * écritures, l'enregistrement lu (copie, secteur 4) peut désigner un AUTRE slot que celui que le chargeur a lancé.
 * Confirmer « l'actif de l'enregistrement » aurait alors validé une image jamais essayée. */
int bootctl_needs_confirm(uint8_t slot) { bootctl_t r; bootctl_read(&r); return !(r.confirmed && r.pending == BOOTCTL_NONE && r.active == slot); }
int bootctl_confirm_slot(uint8_t slot) {
    bootctl_t r; bootctl_read(&r);
    if (r.confirmed && r.pending == BOOTCTL_NONE && r.active == slot) return 0;
    r.active = slot; r.confirmed = 1; r.pending = BOOTCTL_NONE; r.tries = 0;
    return bootctl_write(&r);
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
