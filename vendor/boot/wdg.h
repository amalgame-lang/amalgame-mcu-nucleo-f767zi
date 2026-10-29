/* wdg — chien de garde matériel (IWDG) et cause du dernier redémarrage. Voir wdg.c pour le
 * dimensionnement du délai (les gels d'écriture flash le contraignent par le bas). */
#ifndef WDG_H
#define WDG_H
/* À appeler TÔT au démarrage, avant que quoi que ce soit ne remette RCC_CSR à zéro. */
void        wdg_capture_reset_cause(void);
const char *wdg_reset_cause_str(void);   /* chien-de-garde | logiciel | bouton | mise-sous-tension | … */
int         wdg_reset_by_watchdog(void); /* 1 = le redémarrage précédent était une ANOMALIE */
/* Démarre le chien de garde. IRRÉVERSIBLE : l'IWDG ne s'arrête plus jusqu'au prochain reset. */
void wdg_start(unsigned ms);
void wdg_kick(void);
#endif
