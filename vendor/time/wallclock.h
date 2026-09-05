/* Wall clock for a bare-metal target with no battery-backed RTC: UNIX seconds = a base stepped by
 * SNTP (or by hand on the bench) + the milliseconds elapsed on the board's 1 kHz tick since then.
 * Generic brick: bind the ms source once (e.g. net_millis), nothing else is assumed. Time is
 * 'unknown' until the first set — consumers (TLS certificate dates) must check wallclock_synced(). */
#ifndef MCU_WALLCLOCK_H
#define MCU_WALLCLOCK_H
#include <stdint.h>
void     wallclock_bind(uint32_t (*ms_now)(void));   /* the monotonic 1 kHz counter */
void     wallclock_set(uint32_t unix_sec);            /* step the clock (SNTP reply, bench command) */
void     wallclock_clear(void);                       /* back to 'unknown' */
int      wallclock_synced(void);                      /* 1 once set at least once */
uint32_t wallclock_now(void);                         /* UNIX seconds; 0 while unknown */
uint32_t wallclock_ms(void);                          /* the bound ms counter (0 if none) */
uint32_t wallclock_sets(void);                        /* diagnostics: number of sets so far */
int32_t  wallclock_last_step(void);                   /* diagnostics: seconds the last set jumped by (0 for the first) */
/* UTC calendar split (proleptic Gregorian, valid 1970-2106). wday: 0 = Sunday; yday: 0-365. Any out pointer may be NULL. */
void     wallclock_civil(uint32_t unix_sec, int *year, int *month, int *day, int *hour, int *min, int *sec, int *wday, int *yday);
/* "YYYY-MM-DDTHH:MM:SSZ" into buf (>= 21 bytes); returns buf */
const char *wallclock_iso(uint32_t unix_sec, char *buf);
#endif
