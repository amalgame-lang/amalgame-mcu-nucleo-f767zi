/* mbedTLS platform port: static-pool allocator, STM32 RNG entropy, stubs for the std functions
 * the freestanding build has no business calling (they are only reachable from info/debug paths). */
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/rng.h>
#include "mbedtls_port.h"
#include "wallclock.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"

/* ── allocator: first-fit free list over a static pool (mbedTLS frees in LIFO-ish order, so a
 * simple list with coalescing is enough; no fragmentation issue observed at these sizes) ── */
#ifndef MCU_TLS_POOL_BYTES
#define MCU_TLS_POOL_BYTES (28 * 1024)   /* measured: 13.6 KB to ClientHello; full handshake ≈ 20 KB (see docs) */
#endif
typedef struct blk { size_t size; struct blk *next; int used; } blk_t;   /* 12 B header, 8-aligned payload */
static uint8_t  pool[MCU_TLS_POOL_BYTES] __attribute__((aligned(8)));
static blk_t   *head;
static size_t   used_now, used_hwm;
static unsigned failures;
#define ALIGN8(x) (((x) + 7u) & ~7u)
#define HDR ALIGN8(sizeof(blk_t))

void *mcu_tls_calloc(size_t n, size_t size) {
    size_t want = ALIGN8(n * size);
    if (!want) want = 8;
    if (!head) { head = (blk_t *) pool; head->size = MCU_TLS_POOL_BYTES - HDR; head->next = 0; head->used = 0; }
    for (blk_t *b = head; b; b = b->next) {
        if (b->used || b->size < want) continue;
        if (b->size >= want + HDR + 16) {           /* split */
            blk_t *r = (blk_t *) ((uint8_t *) b + HDR + want);
            r->size = b->size - want - HDR; r->next = b->next; r->used = 0;
            b->size = want; b->next = r;
        }
        b->used = 1; used_now += b->size + HDR; if (used_now > used_hwm) used_hwm = used_now;
        void *p = (uint8_t *) b + HDR; memset(p, 0, want); return p;
    }
    failures++; return 0;
}
void mcu_tls_free(void *p) {
    if (!p) return;
    blk_t *b = (blk_t *) ((uint8_t *) p - HDR);
    b->used = 0; used_now -= b->size + HDR;
    for (blk_t *c = head; c; c = c->next)              /* coalesce forward (free run → one block) */
        while (!c->used && c->next && !c->next->used) { blk_t *n = c->next; c->size += HDR + n->size; c->next = n->next; }
}
size_t   mcu_tls_pool_used(void)     { return used_now; }
size_t   mcu_tls_pool_hwm(void)      { return used_hwm; }
unsigned mcu_tls_pool_failures(void) { return failures; }

/* ── entropy: STM32F7 RNG (TRNG, 32 bits per read) ── */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    static int on; (void) data;
    if (!on) { rcc_periph_clock_enable(RCC_RNG); rng_enable(); on = 1; }
    size_t i = 0;
    while (i < len) {
        uint32_t r = rng_get_random_blocking();
        size_t k = len - i < 4 ? len - i : 4;
        memcpy(output + i, &r, k); i += k;
    }
    *olen = len; return 0;
}

/* ── time: wall clock fed by SNTP (vendor/time/wallclock.c, bound by net_mcu.c). The linkproof / tls_probe
 * builds have no network: the weak stubs keep them linking with an 'unknown' clock. ── */
__attribute__((weak)) uint32_t wallclock_now(void) { return 0; }
__attribute__((weak)) uint32_t wallclock_ms(void)  { return 0; }
__attribute__((weak)) void wallclock_civil(uint32_t t, int *y, int *mo, int *d, int *h, int *mi, int *s, int *wd, int *yd) {
    (void) t; if (y) *y = 1970; if (mo) *mo = 1; if (d) *d = 1; if (h) *h = 0; if (mi) *mi = 0; if (s) *s = 0; if (wd) *wd = 4; if (yd) *yd = 0;
}
time_t mcu_tls_time(time_t *t) { time_t v = (time_t) wallclock_now(); if (t) *t = v; return v; }
mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t) wallclock_ms(); }
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm_buf) {
    if (!tt || !tm_buf || *tt < 0 || *tt > 0xFFFFFFFFll) return 0;
    int y, mo, d, h, mi, s, wd, yd;
    wallclock_civil((uint32_t) *tt, &y, &mo, &d, &h, &mi, &s, &wd, &yd);
    memset(tm_buf, 0, sizeof *tm_buf);
    tm_buf->tm_year = y - 1900; tm_buf->tm_mon = mo - 1; tm_buf->tm_mday = d;
    tm_buf->tm_hour = h; tm_buf->tm_min = mi; tm_buf->tm_sec = s; tm_buf->tm_wday = wd; tm_buf->tm_yday = yd;
    return tm_buf;
}

/* ── std stubs (never on the handshake path) ── */
int  mcu_tls_snprintf(char *s, size_t n, const char *fmt, ...) { (void) fmt; if (s && n) s[0] = 0; return 0; }
int  mcu_tls_printf(const char *fmt, ...) { (void) fmt; return 0; }
int  mcu_tls_fprintf(void *stream, const char *fmt, ...) { (void) stream; (void) fmt; return 0; }
void mcu_tls_exit(int status) { (void) status; for (;;) { } }
int  mcu_tls_setbuf(void *stream, char *buf) { (void) stream; (void) buf; return 0; }
