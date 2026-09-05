#ifndef MCNET_NET_MCU_H
#define MCNET_NET_MCU_H

/* Bring up lwIP + the ETH netif + DHCP (call once from setup{}). */
void net_init(void);

/* Service the MAC RX path and lwIP timeouts (call every loop{} tick). */
void net_poll(void);

/* Bring-up diagnostics. */
const char *net_ip_str(void);   /* "0.0.0.0" until DHCP completes */
int         net_link_up(void);  /* PHY link state (cached by the 250 ms monitor) */
int         net_link_speed(void); /* +100/+10 = full duplex, -100/-10 = half, 0 = down */
int         net_rx_count(void); /* frames received from the MAC (0 = RX path dead) */
int         net_tx_drops(void); /* frames refused by eth_tx (TX descriptor ring full) */
int         net_rx_missed(void);   /* MAC: frames dropped, no free RX descriptor (ETH_DMAMFBOCR.MFC) */
int         net_rx_fifo_ovf(void); /* MAC: frames dropped, RX FIFO overflow (ETH_DMAMFBOCR.MFA) */
int         net_rx_crc_errors(void);   /* MAC MMC: received frames with CRC error */
int         net_rx_align_errors(void); /* MAC MMC: received frames with alignment error */
unsigned int net_uid32(void);      /* 32-bit hash of the STM32 96-bit UID (box identity / RTP SSRC) */
const char  *net_uid96_hex(void);  /* the full 96-bit STM32 UID as 24 lowercase hex chars (PKI identity CN=uid:<hex>) */
const char *net_mac_str(void);  /* derived MAC, "aa:bb:cc:dd:ee:ff" */
unsigned int net_millis(void);  /* elapsed ms since boot (1kHz SysTick) */
unsigned long long net_micros(void); /* elapsed us since boot: SysTick ms + STK_CVR sub-ms
                                      * (216 ticks/us @ 216 MHz AHB) — ~5 ns resolution,
                                      * no debug session needed (unlike DWT_CYCCNT). */

/* Wall clock (SNTP → vendor/time/wallclock.c). Started automatically once DHCP is bound; servers = DHCP option
 * 42 (or `net_time_set_server`), then fixed fallbacks. Diagnostics for the `cfg`/`time` control lines. */
int         net_time_set_server(const char *ip);   /* manual NTP server, IP or name (idx 0, DHCP ignored); NULL/"" = back to auto. 1 ok / 0 bad */
const char *net_dns_server_str(unsigned char i);   /* DNS server i (0-1): DHCP option 6 or the fixed fallbacks */
unsigned    net_dns_fallbacks(void);               /* how many DNS slots the fallbacks filled (0 = DHCP gave both) */
void        net_time_set_hint(const char *ip);     /* an NTP server the box already trusts by IP (the ctrl server runs chrony): slot 1, after DHCP/manual */
void        net_time_stop(void);                   /* bench: stop SNTP (volatile) */
void        net_time_restart(void);                /* re-arm SNTP now (after stop / server change) */
int         net_time_mode(void);                   /* 0 = auto (DHCP + fallbacks), 1 = manual, 2 = stopped */
const char *net_time_server_str(void);             /* server currently polled ("0.0.0.0" = none) */
unsigned    net_time_reach(void);                  /* lwIP reachability shift register of that server (bit0 = last poll) */
unsigned    net_time_sntp_started(void);           /* 1 once sntp_init ran (DHCP bound) */

#endif /* MCNET_NET_MCU_H */
