/* fault — contexte du dernier HardFault, survit au redémarrage (.noinit). Voir fault.c. */
#ifndef FAULT_H
#define FAULT_H
int      fault_pending(void);   /* 1 = la carte est tombée sur une faute depuis la mise sous tension */
unsigned fault_count(void);     /* nombre de fautes cumulées */
unsigned fault_pc(void);        /* adresse fautive — à passer à arm-none-eabi-addr2line */
unsigned fault_lr(void);        /* d'où l'on venait */
unsigned fault_cfsr(void);      /* registre de faute : quel type */
void     fault_clear(void);     /* après lecture, pour ne pas la re-signaler indéfiniment */
#endif
