/* See wallclock.h. Calendar maths = Howard Hinnant's days_from_civil / civil_from_days (public domain). */
#include "wallclock.h"

static uint32_t (*ms_fn)(void);
static uint32_t base_sec, base_ms, sets;
static int32_t  last_step;
static int      synced, source;

void wallclock_bind(uint32_t (*ms_now)(void)) { ms_fn = ms_now; }
uint32_t wallclock_ms(void) { return ms_fn ? ms_fn() : 0; }
uint32_t wallclock_now(void) {
    if (!synced) return 0;
    return base_sec + (wallclock_ms() - base_ms) / 1000u;     /* unsigned wrap-safe for < 49 days between sets */
}
int wallclock_set_src(uint32_t unix_sec, int src) {
    if (src == WALLCLOCK_SERVER && source > WALLCLOCK_SERVER) return 0;
    last_step = synced ? (int32_t) (unix_sec - wallclock_now()) : 0;
    base_sec = unix_sec; base_ms = wallclock_ms(); synced = 1; sets++; source = src;
    return 1;
}
void wallclock_set(uint32_t unix_sec) { wallclock_set_src(unix_sec, WALLCLOCK_MANUAL); }
int wallclock_source(void) { return source; }
void wallclock_clear(void) { synced = 0; base_sec = 0; source = WALLCLOCK_NONE; }
int wallclock_synced(void) { return synced; }
uint32_t wallclock_sets(void) { return sets; }
int32_t wallclock_last_step(void) { return last_step; }

static int32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t) era * 146097 + doe - 719468;
}
void wallclock_civil(uint32_t t, int *year, int *month, int *day, int *hour, int *min, int *sec, int *wday, int *yday) {
    uint32_t days = t / 86400u, rem = t % 86400u;
    uint32_t z = days + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y = (int) (yoe + era * 400u);
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    int d = (int) (doy - (153u * mp + 2u) / 5u + 1u);
    int m = (int) (mp < 10u ? mp + 3u : mp - 9u);
    y += m <= 2;
    if (year) *year = y; if (month) *month = m; if (day) *day = d;
    if (hour) *hour = (int) (rem / 3600u); if (min) *min = (int) (rem / 60u % 60u); if (sec) *sec = (int) (rem % 60u);
    if (wday) *wday = (int) ((days + 4u) % 7u);                       /* 1970-01-01 was a Thursday */
    if (yday) *yday = (int) ((int32_t) days - days_from_civil(y, 1, 1));
}
static char *put2(char *p, int v) { p[0] = (char) ('0' + v / 10); p[1] = (char) ('0' + v % 10); return p + 2; }
const char *wallclock_iso(uint32_t t, char *buf) {
    int y, mo, d, h, mi, s; wallclock_civil(t, &y, &mo, &d, &h, &mi, &s, 0, 0);
    char *p = buf; p = put2(p, y / 100); p = put2(p, y % 100); *p++ = '-'; p = put2(p, mo); *p++ = '-'; p = put2(p, d);
    *p++ = 'T'; p = put2(p, h); *p++ = ':'; p = put2(p, mi); *p++ = ':'; p = put2(p, s); *p++ = 'Z'; *p = 0;
    return buf;
}
