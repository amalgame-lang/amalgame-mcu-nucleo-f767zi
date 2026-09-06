/* wdg — chien de garde matériel (IWDG) + cause du dernier redémarrage.
 *
 * POURQUOI : sans lui, un boîtier qui se fige reste figé. Sur le banc on débranche ; chez un
 * musicien, la boîte est morte et personne ne peut la reflasher. Un HardFault silencieux a déjà été
 * vécu le 2026-09-05 (carte muette sur le LAN, mais « en ligne » côté serveur, TCP à moitié ouvert).
 *
 * DURÉE : l'écriture en flash GÈLE le CPU — `save` mesuré à ~1,9 s, et l'OTA efface DEUX secteurs.
 * `flash_erase_sector` rend la main avant la fin : le gel se produit au premier accès flash suivant,
 * donc il n'est pas mesurable dans l'appel (erase_ms=2 relevé). Le délai doit donc largement couvrir
 * ces gels, sinon le chien de garde redémarre la carte AU MILIEU d'une écriture flash — pire que le
 * mal qu'il soigne. 16 s nominal : le LSI du F7 dérive de 17 à 47 kHz, ce qui donne 11 à 31 s réels.
 * Le plancher (11 s) reste très au-dessus des ~4 s de gel du pire cas.
 */
#include <libopencm3/stm32/iwdg.h>
#include <libopencm3/stm32/rcc.h>
#include "wdg.h"

static unsigned wdg_cause;   /* copie des drapeaux RCC_CSR lus AVANT de les effacer */

void wdg_capture_reset_cause(void)
{
    wdg_cause = RCC_CSR >> 24;      /* octet haut : les drapeaux de cause */
    RCC_CSR |= RCC_CSR_RMVF;        /* effacés, sinon ils s'accumulent d'un boot à l'autre */
}

int wdg_reset_by_watchdog(void) { return (wdg_cause & (RCC_CSR_IWDGRSTF >> 24)) != 0; }

const char *wdg_reset_cause_str(void)
{
    /* Ordre volontaire : le chien de garde d'abord, c'est le seul qui signale une ANOMALIE. */
    if (wdg_cause & (RCC_CSR_IWDGRSTF >> 24)) return "chien-de-garde";
    if (wdg_cause & (RCC_CSR_WWDGRSTF >> 24)) return "fenetre";
    if (wdg_cause & (RCC_CSR_SFTRSTF >> 24))  return "logiciel";   /* ota apply, scb_reset_system */
    if (wdg_cause & (RCC_CSR_PINRSTF >> 24))  return "bouton";     /* NRST : st-flash reset, reset carte */
    if (wdg_cause & (RCC_CSR_BORRSTF >> 24))  return "brownout";
    if (wdg_cause & (RCC_CSR_PORRSTF >> 24))  return "mise-sous-tension";
    return "inconnue";
}

void wdg_start(unsigned ms) { iwdg_set_period_ms(ms); iwdg_start(); }
void wdg_kick(void)         { iwdg_reset(); }
