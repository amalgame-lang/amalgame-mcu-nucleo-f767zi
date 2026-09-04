#ifndef MCNET_WS_CLIENT_H
#define MCNET_WS_CLIENT_H
/* Minimal WebSocket client (RFC 6455, text frames) on the lwIP raw TCP API, NO_SYS.
 * One connection, outbound, auto-reconnect with backoff, ping/pong, a "hello" line
 * re-sent on every (re)connect. Reference behaviour: musicall-server/tools/wsline.py.
 * Reusable brick: nothing MusiCall-specific except the defaults. */
#include <stdint.h>

typedef void (*ws_line_cb)(const char *line, int len);   /* one text frame received */

/* Start (or restart) the client towards ip:port path. host = Host: header value.
 * hello = text sent right after each successful handshake (may be NULL). */
void ws_client_start(const char *ip, uint16_t port, const char *path, const char *host,
                     const char *hello, ws_line_cb on_line);
void ws_client_stop(void);
void ws_client_poll(void);                   /* call often (every loop iteration is fine) */
int  ws_client_is_open(void);                /* 1 = handshake done, frames flowing */
int  ws_client_send_text(const char *s);     /* 1 = queued; 0 = not open / no buffer */
uint32_t ws_client_reconnects(void);         /* diagnostics */
uint32_t ws_client_rx_frames(void);
#endif
