#ifndef MCNET_WS_CLIENT_H
#define MCNET_WS_CLIENT_H
/* Minimal WebSocket client (RFC 6455, text frames) on the lwIP raw TCP API, NO_SYS.
 * One connection, outbound, auto-reconnect with backoff, ping/pong, a "hello" line
 * re-sent on every (re)connect. Reference behaviour: musicall-server/tools/wsline.py.
 * Reusable brick: nothing MusiCall-specific except the defaults. */
#include <stdint.h>

typedef void (*ws_line_cb)(const char *line, int len);   /* one text frame received */

/* Start (or restart) the client towards target:port path — target = IPv4 literal or a DNS name (LWIP_DNS,
 * resolved before each connect; lwIP caches by TTL). host = Host: header value (defaults to target).
 * hello = text sent right after each successful handshake (may be NULL). */
void ws_client_start(const char *ip, uint16_t port, const char *path, const char *host,
                     const char *hello, ws_line_cb on_line);
void ws_client_stop(void);
void ws_client_poll(void);                   /* call often (every loop iteration is fine) */
int  ws_client_is_open(void);                /* 1 = handshake done, frames flowing */
int  ws_client_send_text(const char *s);     /* 1 = queued; 0 = not open / no buffer */
uint32_t ws_client_reconnects(void);         /* diagnostics */
uint32_t ws_client_rx_frames(void);
uint32_t ws_client_dns_failures(void);       /* name resolutions that failed (target by name) */
const char *ws_client_target(void);          /* the configured target (name or ip) */
/* wss:// (build with MC_TLS=1): TLS with SNI/name check `sni` against the embedded roots. Returns 0 if
 * the firmware was built without TLS. Call before ws_client_start(). */
int ws_client_set_tls(int on, const char *sni);
int ws_client_is_tls(void);
/* profiling: µs spent in the last send, split sndbuf / write (TLS encrypt) / output (tcp_output) */
extern uint32_t ws_prof_sndbuf_us, ws_prof_write_us, ws_prof_output_us;
/* TLS session resumption state: a session is saved after each successful handshake and offered on reconnect */
int ws_client_tls_session_saved(void);
uint32_t ws_client_tls_resumed_tries(void);
int ws_client_tls_session_id_len(void);
void ws_client_forget_session(void);         /* drop the saved TLS session (next connect = full handshake) */
/* wss:// needs the wall clock (certificate dates): while wallclock_synced() is 0 no handshake is attempted
 * (WS_TLS_NEEDS_TIME=0 at build time to disable — bench only). Bounded degradation: after WS_TLS_TIME_GRACE_MS
 * (60 s) without a clock the handshake goes ahead and tolerates ONLY the two date flags (expired / not yet
 * valid) — chain and name are still enforced; such a connection is flagged (dates_unchecked) so the server/UI
 * can show it, and the server hands the box the time (`time <unix>`) on hello. */
uint32_t ws_client_time_waits(void);         /* polls skipped because the clock was unknown */
uint32_t ws_client_tls_degraded(void);       /* handshakes attempted without a clock (grace elapsed) */
int      ws_client_tls_dates_unchecked(void);/* 1 = the current (or last) connection skipped the date check */
/* last TLS handshake failure seen by the altcp glue: mbedTLS error (e.g. -0x2700 = certificate verify
 * failed) and the X.509 verify flags (0x01 expired, 0x200 not yet valid …); 0/0 = none since boot */
int ws_client_tls_last_error(void);
unsigned ws_client_tls_last_verify(void);
unsigned ws_client_tls_failures(void);
#endif
