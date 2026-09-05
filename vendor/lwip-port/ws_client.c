/* ws_client.c — see ws_client.h. Single connection, static buffers, no malloc. */
#include "ws_client.h"
#include "net_mcu.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include <string.h>
#include <stdint.h>
#if LWIP_ALTCP
/* altcp: same code path for ws:// (altcp_tcp) and wss:// (altcp_tls over mbedTLS, vendor/mbedtls-port) */
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "mbedtls/ssl.h"
#include "mbedtls_port.h"
#include "wallclock.h"
extern int altcp_mbedtls_last_hs_err; extern unsigned altcp_mbedtls_last_verify, altcp_mbedtls_hs_failures;   /* altcp_tls_mbedtls.c (MusiCall patch) */
#ifndef WS_TLS_NEEDS_TIME
#define WS_TLS_NEEDS_TIME 1
#endif
#ifndef WS_TLS_TIME_GRACE_MS
#define WS_TLS_TIME_GRACE_MS 60000
#endif
#define WS_PCB      struct altcp_pcb
#define ws_pcb_arg  altcp_arg
#define ws_pcb_recv altcp_recv
#define ws_pcb_err  altcp_err
#define ws_pcb_sent altcp_sent
#define ws_pcb_poll altcp_poll
#define ws_pcb_close altcp_close
#define ws_pcb_abort altcp_abort
#define ws_pcb_sndbuf altcp_sndbuf
#define ws_pcb_write altcp_write
#define ws_pcb_output altcp_output
#define ws_pcb_recved altcp_recved
#define ws_pcb_connect altcp_connect
static int ws_tls; static char ws_sni[64]; static struct altcp_tls_config *ws_tls_conf;
#else
#define WS_PCB      struct tcp_pcb
#define ws_pcb_arg  tcp_arg
#define ws_pcb_recv tcp_recv
#define ws_pcb_err  tcp_err
#define ws_pcb_sent tcp_sent
#define ws_pcb_poll tcp_poll
#define ws_pcb_close tcp_close
#define ws_pcb_abort tcp_abort
#define ws_pcb_sndbuf tcp_sndbuf
#define ws_pcb_write tcp_write
#define ws_pcb_output tcp_output
#define ws_pcb_recved tcp_recved
#define ws_pcb_connect tcp_connect
#endif

#define WS_RX_BUF   4096      /* server lines up to ~1 KB (OTA data), with room for a burst; a frame that cannot fit → reconnect */
#define WS_TX_MAX   700
#define WS_HS_MAX   400

enum { WS_IDLE, WS_CONNECTING, WS_HANDSHAKE, WS_OPEN, WS_CLOSED, WS_RESOLVING };

static WS_PCB *ws_pcb;
#if LWIP_ALTCP
/* TLS session resumption: the first handshake costs ~2.4 s of blocking crypto (RSA-4096 + ECDHE on the M7);
 * a resumed one (session ID, OpenSSL server cache) skips it. Saved once the WebSocket is open, reused on
 * every reconnect to the same server, dropped when ip/port/sni change. */
static struct altcp_tls_session ws_tls_sess; static int ws_tls_sess_ok; static uint32_t ws_tls_resumed_tries; static uint32_t ws_time_waits, ws_tls_degraded, ws_start_ms; static int ws_tls_dates_unchecked;
/* X.509 verify hook: with no clock at all, drop the two date flags (everything else stays fatal) and remember it */
static int ws_verify_cb(void *arg, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void) arg; (void) crt; (void) depth;
    if (!wallclock_synced() && (*flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))) {
        *flags &= ~(uint32_t) (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE); ws_tls_dates_unchecked = 1;
    }
    return 0;
}
static void ws_tls_session_drop(void) { if (ws_tls_sess_ok) { altcp_tls_free_session(&ws_tls_sess); ws_tls_sess_ok = 0; } }
static void ws_tls_session_save(void) {
    if (!ws_tls || !ws_pcb) return;
    ws_tls_session_drop();
    altcp_tls_init_session(&ws_tls_sess);
    if (altcp_tls_get_session(ws_pcb, &ws_tls_sess) == ERR_OK) ws_tls_sess_ok = 1;
}
#endif
static int      ws_state = WS_IDLE;
static ip_addr_t ws_ip; static uint16_t ws_port; static char ws_target[64]; static int ws_by_name; static uint32_t ws_dns_failures, ws_dns_gen, ws_resolve_ms;
static char     ws_path[64], ws_host[64], ws_hello[200];
static ws_line_cb ws_on_line;
static unsigned char ws_rx[WS_RX_BUF]; static int ws_rx_len;
static uint32_t ws_next_try_ms, ws_backoff_ms = 2000;
static uint32_t ws_last_rx_ms, ws_ping_sent_ms;
static uint32_t ws_reconnects, ws_frames, ws_rx_overflows;   /* overflows: pbufs that did not fit in ws_rx (connection reset, never truncated) */
static uint32_t ws_rand_state;
uint32_t ws_prof_sndbuf_us, ws_prof_write_us, ws_prof_output_us;   /* profiling of the last frame send (sndbuf / write / output) */

static uint32_t ws_rand(void) {           /* xorshift, seeded with UID + time */
    if (!ws_rand_state) ws_rand_state = net_uid32() ^ (net_millis() * 2654435761u) ^ 0x9E3779B9u;
    ws_rand_state ^= ws_rand_state << 13; ws_rand_state ^= ws_rand_state >> 17; ws_rand_state ^= ws_rand_state << 5;
    return ws_rand_state;
}

static void ws_close_pcb(void) {
    if (ws_pcb) {
        ws_pcb_arg(ws_pcb, NULL); ws_pcb_recv(ws_pcb, NULL); ws_pcb_err(ws_pcb, NULL); ws_pcb_sent(ws_pcb, NULL); ws_pcb_poll(ws_pcb, NULL, 0);
        if (ws_pcb_close(ws_pcb) != ERR_OK) ws_pcb_abort(ws_pcb);
        ws_pcb = NULL;
    }
}

static void ws_schedule_retry(void) {
    ws_close_pcb();
    ws_state = WS_CLOSED;
    ws_rx_len = 0;
    ws_next_try_ms = net_millis() + ws_backoff_ms;
    if (ws_backoff_ms < 30000) ws_backoff_ms *= 2;
}

static void ws_err_cb(void *arg, err_t err) {
    (void) arg; (void) err;
    ws_pcb = NULL;                            /* already freed by lwIP */
    ws_state = WS_CLOSED; ws_rx_len = 0;
    ws_next_try_ms = net_millis() + ws_backoff_ms;
    if (ws_backoff_ms < 30000) ws_backoff_ms *= 2;
}

/* RFC 6455 client frame: FIN + opcode, MASK bit, masked payload. */
static int ws_send_frame(int opcode, const unsigned char *data, int n) {
    if (!ws_pcb || ws_state != WS_OPEN || n > WS_TX_MAX) return 0;
    unsigned char buf[WS_TX_MAX + 8]; int p = 0;
    buf[p++] = (unsigned char) (0x80 | opcode);
    if (n < 126) buf[p++] = (unsigned char) (0x80 | n);
    else { buf[p++] = 0x80 | 126; buf[p++] = (unsigned char) (n >> 8); buf[p++] = (unsigned char) n; }
    uint32_t r = ws_rand(); unsigned char mask[4] = { r, r >> 8, r >> 16, r >> 24 };
    memcpy(&buf[p], mask, 4); p += 4;
    for (int i = 0; i < n; i++) buf[p++] = data[i] ^ mask[i & 3];
    unsigned long long t0 = net_micros();
    if (ws_pcb_sndbuf(ws_pcb) < (u16_t) p) return 0;
    unsigned long long t1 = net_micros();
    if (ws_pcb_write(ws_pcb, buf, (u16_t) p, TCP_WRITE_FLAG_COPY) != ERR_OK) return 0;
    unsigned long long t2 = net_micros();
    ws_pcb_output(ws_pcb);
    unsigned long long t3 = net_micros();
    ws_prof_sndbuf_us = (uint32_t) (t1 - t0); ws_prof_write_us = (uint32_t) (t2 - t1); ws_prof_output_us = (uint32_t) (t3 - t2);
    return 1;
}

int ws_client_send_text(const char *s) { return ws_send_frame(1, (const unsigned char *) s, (int) strlen(s)); }

/* freestanding: no snprintf (it drags newlib syscalls in) */
static int ws_cat(char *d, const char *s) { int n = 0; while (s[n]) { d[n] = s[n]; n++; } d[n] = 0; return n; }
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void ws_key(char out[25]) {          /* base64 of 16 random bytes */
    unsigned char k[16]; for (int i = 0; i < 16; i += 4) { uint32_t r = ws_rand(); memcpy(&k[i], &r, 4); }
    int o = 0;
    for (int i = 0; i < 15; i += 3) {
        uint32_t v = (k[i] << 16) | (k[i+1] << 8) | k[i+2];
        out[o++] = b64[(v >> 18) & 63]; out[o++] = b64[(v >> 12) & 63]; out[o++] = b64[(v >> 6) & 63]; out[o++] = b64[v & 63];
    }
    uint32_t v = k[15] << 16; out[o++] = b64[(v >> 18) & 63]; out[o++] = b64[(v >> 12) & 63]; out[o++] = '='; out[o++] = '='; out[o] = 0;
}

static err_t ws_connected_cb(void *arg, WS_PCB *pcb, err_t err) {
    (void) arg;
    if (err != ERR_OK) { ws_schedule_retry(); return err; }
    char key[25]; ws_key(key);
    char hs[WS_HS_MAX]; int n = 0;
    n += ws_cat(hs + n, "GET "); n += ws_cat(hs + n, ws_path); n += ws_cat(hs + n, " HTTP/1.1\r\nHost: "); n += ws_cat(hs + n, ws_host); n += ws_cat(hs + n, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
    n += ws_cat(hs + n, "Sec-WebSocket-Key: "); n += ws_cat(hs + n, key); n += ws_cat(hs + n, "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    if (ws_pcb_write(pcb, hs, (u16_t) n, TCP_WRITE_FLAG_COPY) != ERR_OK) { ws_schedule_retry(); return ERR_OK; }
    ws_pcb_output(pcb);
    ws_state = WS_HANDSHAKE; ws_rx_len = 0; ws_last_rx_ms = net_millis();
    return ERR_OK;
}

/* Consume complete frames from ws_rx; returns when a partial frame remains. */
static void ws_parse_frames(void) {
    for (;;) {
        if (ws_rx_len < 2) return;
        int op = ws_rx[0] & 0x0F, hdr = 2; uint32_t n = ws_rx[1] & 0x7F; int masked = ws_rx[1] & 0x80;
        if (n == 126) { if (ws_rx_len < 4) return; n = (ws_rx[2] << 8) | ws_rx[3]; hdr = 4; }
        else if (n == 127) { ws_schedule_retry(); return; }         /* never from our server */
        if (masked) hdr += 4;
        if (hdr + n > (uint32_t) WS_RX_BUF) { ws_schedule_retry(); return; } /* oversized: resync by reconnect */
        if ((uint32_t) ws_rx_len < hdr + n) return;
        unsigned char *pl = &ws_rx[hdr];
        if (masked) { unsigned char *m = &ws_rx[hdr - 4]; for (uint32_t i = 0; i < n; i++) pl[i] ^= m[i & 3]; }
        ws_frames++;
        if (op == 1 && ws_on_line) { unsigned char save = pl[n]; pl[n] = 0; ws_on_line((const char *) pl, (int) n); pl[n] = save; }
        else if (op == 9) ws_send_frame(10, pl, (int) n);            /* ping -> pong */
        else if (op == 8) { ws_schedule_retry(); return; }
        int used = hdr + (int) n;
        memmove(ws_rx, ws_rx + used, ws_rx_len - used); ws_rx_len -= used;
    }
}

static err_t ws_recv_cb(void *arg, WS_PCB *pcb, struct pbuf *p, err_t err) {
    (void) arg; (void) err;
    if (!p) { ws_schedule_retry(); return ERR_OK; }              /* remote close */
    int room = WS_RX_BUF - ws_rx_len;
    if (p->tot_len > room) {                                     /* would truncate a frame → the parser would wait forever: resync by reconnecting */
        ws_rx_overflows++; ws_pcb_recved(pcb, p->tot_len); pbuf_free(p); ws_schedule_retry(); return ERR_OK;
    }
    pbuf_copy_partial(p, ws_rx + ws_rx_len, (u16_t) p->tot_len, 0);
    ws_rx_len += p->tot_len;
    ws_pcb_recved(pcb, p->tot_len);
    pbuf_free(p);
    ws_last_rx_ms = net_millis();
    if (ws_state == WS_HANDSHAKE) {
        ws_rx[ws_rx_len < WS_RX_BUF ? ws_rx_len : WS_RX_BUF - 1] = 0;
        char *end = strstr((char *) ws_rx, "\r\n\r\n");
        if (!end) { if (ws_rx_len >= WS_RX_BUF - 1) ws_schedule_retry(); return ERR_OK; }
        if (!strstr((char *) ws_rx, " 101 ")) { ws_schedule_retry(); return ERR_OK; }
        int used = (int) (end + 4 - (char *) ws_rx);
        memmove(ws_rx, ws_rx + used, ws_rx_len - used); ws_rx_len -= used;
        ws_state = WS_OPEN; ws_backoff_ms = 2000; ws_ping_sent_ms = 0;
#if LWIP_ALTCP
        ws_tls_session_save();
#endif
        if (ws_hello[0]) ws_client_send_text(ws_hello);
    }
    if (ws_state == WS_OPEN) ws_parse_frames();
    return ERR_OK;
}

static void ws_connect_ip(void);
static void ws_dns_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void) name;
    if ((uint32_t) (uintptr_t) arg != ws_dns_gen || ws_state != WS_RESOLVING) return;   /* stale (stop/restart since) */
    if (addr && !ip_addr_isany(addr)) { ws_ip = *addr; ws_connect_ip(); }
    else { ws_dns_failures++; ws_schedule_retry(); }
}
static void ws_connect(void) {
    ws_close_pcb();
#if LWIP_DNS
    if (ws_by_name) {
        ip_addr_t a; ws_dns_gen++;
        err_t e = dns_gethostbyname(ws_target, &a, ws_dns_cb, (void *) (uintptr_t) ws_dns_gen);
        if (e == ERR_OK) { ws_ip = a; }
        else if (e == ERR_INPROGRESS) { ws_state = WS_RESOLVING; ws_resolve_ms = net_millis(); return; }
        else { ws_dns_failures++; ws_schedule_retry(); return; }
    }
#endif
    ws_connect_ip();
}
static void ws_connect_ip(void) {
#if LWIP_ALTCP
    if (ws_tls) {
        if (!ws_tls_conf) {
            ws_tls_conf = altcp_tls_create_config_client((const u8_t *) mcu_tls_roots_pem, mcu_tls_roots_pem_len);
            /* struct altcp_tls_config starts with the mbedtls_ssl_config: cap records at 4 KB (our buffers) */
            if (ws_tls_conf) mbedtls_ssl_conf_max_frag_len((mbedtls_ssl_config *) ws_tls_conf, MBEDTLS_SSL_MAX_FRAG_LEN_4096);
            if (ws_tls_conf) mbedtls_ssl_conf_verify((mbedtls_ssl_config *) ws_tls_conf, ws_verify_cb, NULL);
        }
        ws_pcb = ws_tls_conf ? altcp_tls_new(ws_tls_conf, IPADDR_TYPE_V4) : NULL;
        if (ws_pcb) mbedtls_ssl_set_hostname((mbedtls_ssl_context *) altcp_tls_context(ws_pcb), ws_sni);   /* SNI + name check */
        if (ws_pcb && ws_tls_sess_ok) { if (altcp_tls_set_session(ws_pcb, &ws_tls_sess) == ERR_OK) ws_tls_resumed_tries++; }
        else ws_tls_dates_unchecked = 0;   /* full handshake: the verify hook decides again (a resumed one inherits the flag) */
        if (ws_pcb && !wallclock_synced()) ws_tls_degraded++;
    } else {
        ws_pcb = altcp_tcp_new();
    }
#else
    ws_pcb = tcp_new();
#endif
    if (!ws_pcb) { ws_schedule_retry(); return; }
    ws_pcb_err(ws_pcb, ws_err_cb);
    ws_pcb_recv(ws_pcb, ws_recv_cb);
    ws_state = WS_CONNECTING; ws_rx_len = 0;
    ws_reconnects++;
    if (ws_pcb_connect(ws_pcb, &ws_ip, ws_port, ws_connected_cb) != ERR_OK) ws_schedule_retry();
}

void ws_client_start(const char *ip, uint16_t port, const char *path, const char *host, const char *hello, ws_line_cb on_line) {
    ws_client_stop();
#if LWIP_ALTCP
    if (strncmp(ip, ws_target, sizeof ws_target) != 0 || port != ws_port || strncmp(host ? host : ip, ws_host, sizeof ws_host) != 0) ws_tls_session_drop();
#endif
    strncpy(ws_target, ip, sizeof ws_target - 1); ws_target[sizeof ws_target - 1] = 0;
    ws_by_name = !ipaddr_aton(ip, &ws_ip);          /* not an IPv4 literal → resolve before each connect (LWIP_DNS) */
    ws_port = port; ws_on_line = on_line;
    strncpy(ws_path, path ? path : "/", sizeof ws_path - 1);
    strncpy(ws_host, host ? host : ip, sizeof ws_host - 1);
    if (hello) strncpy(ws_hello, hello, sizeof ws_hello - 1); else ws_hello[0] = 0;
    ws_backoff_ms = 2000; ws_reconnects = 0; ws_start_ms = net_millis();
    ws_state = WS_CLOSED; ws_next_try_ms = net_millis();     /* connect on the next poll */
}

void ws_client_stop(void) { ws_close_pcb(); ws_state = WS_IDLE; ws_rx_len = 0; }

/* wss://: TLS with server-name = sni (SNI + certificate name check against the embedded roots).
 * Call before ws_client_start. Ignored (returns 0) when built without MC_TLS. */
int ws_client_set_tls(int on, const char *sni) {
#if LWIP_ALTCP
    ws_tls = on ? 1 : 0;
    if (sni) strncpy(ws_sni, sni, sizeof ws_sni - 1); else ws_sni[0] = 0;
    return 1;
#else
    (void) on; (void) sni; return 0;
#endif
}
int ws_client_tls_session_saved(void) {
#if LWIP_ALTCP
    return ws_tls_sess_ok;
#else
    return 0;
#endif
}
uint32_t ws_client_tls_resumed_tries(void) {
#if LWIP_ALTCP
    return ws_tls_resumed_tries;
#else
    return 0;
#endif
}
int ws_client_tls_session_id_len(void) {   /* 0 = no session id (server did not issue one → nothing to resume) */
#if LWIP_ALTCP
    return ws_tls_sess_ok ? (int) ws_tls_sess.data.id_len : -1;
#else
    return -1;
#endif
}
void ws_client_forget_session(void) {
#if LWIP_ALTCP
    ws_tls_session_drop();
#endif
}
uint32_t ws_client_time_waits(void) {
#if LWIP_ALTCP
    return ws_time_waits;
#else
    return 0;
#endif
}
uint32_t ws_client_tls_degraded(void) {
#if LWIP_ALTCP
    return ws_tls_degraded;
#else
    return 0;
#endif
}
int ws_client_tls_dates_unchecked(void) {
#if LWIP_ALTCP
    return ws_tls_dates_unchecked;
#else
    return 0;
#endif
}
int ws_client_tls_last_error(void) {
#if LWIP_ALTCP
    return altcp_mbedtls_last_hs_err;
#else
    return 0;
#endif
}
unsigned ws_client_tls_last_verify(void) {
#if LWIP_ALTCP
    return altcp_mbedtls_last_verify;
#else
    return 0;
#endif
}
unsigned ws_client_tls_failures(void) {
#if LWIP_ALTCP
    return altcp_mbedtls_hs_failures;
#else
    return 0;
#endif
}
int ws_client_is_tls(void) {
#if LWIP_ALTCP
    return ws_tls;
#else
    return 0;
#endif
}

void ws_client_poll(void) {
    uint32_t now = net_millis();
    if (ws_state == WS_CLOSED && (int32_t) (now - ws_next_try_ms) >= 0) {
#if LWIP_ALTCP
        if (ws_tls && WS_TLS_NEEDS_TIME && !wallclock_synced() && now - ws_start_ms < WS_TLS_TIME_GRACE_MS) { ws_time_waits++; ws_next_try_ms = now + 500; return; }   /* no clock → dates unverifiable → wait (bounded) */
#endif
        ws_connect(); return;
    }
    if (ws_state == WS_RESOLVING) { if (now - ws_resolve_ms > 10000) { ws_dns_failures++; ws_schedule_retry(); } return; }
    if (ws_state == WS_CONNECTING || ws_state == WS_HANDSHAKE) {
        if (now - ws_last_rx_ms > 10000 && ws_state == WS_HANDSHAKE) ws_schedule_retry();
        return;
    }
    if (ws_state == WS_OPEN) {
        /* liveness: ping after 20 s of silence, give up 10 s later */
        if (now - ws_last_rx_ms > 20000 && !ws_ping_sent_ms) { ws_send_frame(9, (const unsigned char *) "mc", 2); ws_ping_sent_ms = now; }
        if (ws_ping_sent_ms && now - ws_ping_sent_ms > 10000) ws_schedule_retry();
        if (ws_ping_sent_ms && ws_last_rx_ms > ws_ping_sent_ms) ws_ping_sent_ms = 0;
    }
}

int ws_client_is_open(void) { return ws_state == WS_OPEN; }
uint32_t ws_client_reconnects(void) { return ws_reconnects; }
uint32_t ws_client_rx_frames(void) { return ws_frames; }
uint32_t ws_client_rx_overflows(void) { return ws_rx_overflows; }
uint32_t ws_client_dns_failures(void) { return ws_dns_failures; }
const char *ws_client_target(void) { return ws_target; }
