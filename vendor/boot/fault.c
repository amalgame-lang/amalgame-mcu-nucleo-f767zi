/* fault — gestionnaire de HardFault : enregistre POURQUOI la carte est tombée, puis redémarre.
 *
 * Sans lui, une faute tombe dans Default_Handler (boucle infinie) : le chien de garde reprend la
 * main au bout d'une vingtaine de secondes, la carte est sauvée — mais on ne sait pas pourquoi elle
 * est tombée. Avec lui : redémarrage immédiat (~0,5 s au lieu de ~20 s) ET le contexte de la faute
 * survit, lisible à distance dans `cfg`. C'est ce qui manquait le 2026-09-05, où un débordement de
 * tampon a exigé openocd et une lecture de pile à la main pour être compris.
 *
 * L'enregistrement vit en .noinit : hors de la plage que startup.c remet à zéro (voir les .ld).
 * Il survit donc à un redémarrage, mais pas à une coupure d'alimentation — acceptable, là quelqu'un
 * est intervenu physiquement.
 */
#include <stdint.h>
#include <libopencm3/cm3/scb.h>
#include "fault.h"

#define FAULT_MAGIC 0x464C5431u   /* "FLT1" */

static struct {
    uint32_t magic;
    uint32_t pc, lr, psr;    /* pris dans la trame empilée par le coeur */
    uint32_t cfsr, hfsr;     /* registres de faute : QUEL type de faute */
    uint32_t count;          /* fautes cumulées depuis la dernière coupure d'alimentation */
} fault_rec __attribute__((section(".noinit")));

/* Appelé par le gestionnaire nu avec le pointeur sur la trame empilée. Ne revient jamais. */
void fault_record(uint32_t *frame) __attribute__((used));
void fault_record(uint32_t *frame)
{
    uint32_t n = (fault_rec.magic == FAULT_MAGIC) ? fault_rec.count : 0;
    fault_rec.magic = FAULT_MAGIC;
    fault_rec.pc   = frame[6];      /* adresse fautive : c'est LA donnée utile */
    fault_rec.lr   = frame[5];      /* d'où l'on venait */
    fault_rec.psr  = frame[7];
    fault_rec.cfsr = SCB_CFSR;
    fault_rec.hfsr = SCB_HFSR;
    fault_rec.count = n + 1;
    scb_reset_system();             /* immédiat : inutile d'attendre le chien de garde */
    for (;;) { }
}

/* Gestionnaire nu : choisit la bonne pile (MSP ou PSP selon le bit 2 de EXC_RETURN) et passe la
 * trame. Nu (« naked ») pour que le compilateur n'empile rien avant qu'on ait lu la trame. */
__attribute__((naked)) void hard_fault_handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b fault_record    \n");
}
/* Les fautes mémoire/bus/usage escaladent en HardFault si elles ne sont pas activées séparément ;
 * on les capte quand même, au cas où quelqu'un les activerait plus tard. */
void mem_manage_handler(void)  __attribute__((alias("hard_fault_handler")));
void bus_fault_handler(void)   __attribute__((alias("hard_fault_handler")));
void usage_fault_handler(void) __attribute__((alias("hard_fault_handler")));

int fault_pending(void) { return fault_rec.magic == FAULT_MAGIC && fault_rec.count > 0; }
unsigned fault_count(void) { return fault_pending() ? fault_rec.count : 0; }
unsigned fault_pc(void)    { return fault_pending() ? fault_rec.pc : 0; }
unsigned fault_lr(void)    { return fault_pending() ? fault_rec.lr : 0; }
unsigned fault_cfsr(void)  { return fault_pending() ? fault_rec.cfsr : 0; }
void fault_clear(void)     { fault_rec.magic = 0; fault_rec.count = 0; }
