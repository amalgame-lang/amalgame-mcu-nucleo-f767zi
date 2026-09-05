#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* lwIP options for MusiCall-Box on the Nucleo-F767ZI: NO_SYS raw API, UDP + DHCP
 * + ARP + ICMP, no TCP / sockets / netconn. Tuned small; bump pool sizes when the
 * audio traffic profile is measured on hardware. */

#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

/* Protocols */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    1   /* control channel: WebSocket client (ws_client.c), 2026-09-04 */
/* TLS (2026-09-05): build with -DMC_TLS=1 → altcp layer + mbedTLS glue (vendor/mbedtls-port). Without it
 * ws_client.c keeps the raw tcp_* API and no altcp code is compiled in. */
#ifdef MC_TLS
#define LWIP_ALTCP                  1
#define LWIP_ALTCP_TLS              1
#define LWIP_ALTCP_TLS_MBEDTLS      1
#define MEMP_NUM_ALTCP_PCB          4   /* one TLS connection = 2 altcp pcbs (tls over tcp) */
/* The glue defaults to VERIFY_OPTIONAL (a bad chain or name is silently accepted — measured 2026-09-05:
 * a wrong SNI still connected). REQUIRED: chain to the embedded roots + name match, or no connection. */
#define ALTCP_MBEDTLS_AUTHMODE      MBEDTLS_SSL_VERIFY_REQUIRED
#endif
#define LWIP_UDP                    1
#define LWIP_RAW                    0
#define LWIP_ARP                    1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   1
/* SNTP (2026-09-05): the wall clock TLS needs to check certificate dates. Servers: DHCP option 42 if the
 * router offers one, else fixed fallbacks (net_mcu.c) — no DNS in lwIP yet. Started once DHCP is bound. */
#define LWIP_DHCP_GET_NTP_SRV       1
#define LWIP_DHCP_MAX_NTP_SERVERS   1
#define SNTP_MAX_SERVERS            4   /* 0 = DHCP / manual (`ntp <ip>`), 1 = hint (the ctrl server, chrony on the VPS), 2-3 = fallbacks */
#define SNTP_SERVER_DNS             1   /* `ntp <name>`, last fallback pool.ntp.org */
#define SNTP_STARTUP_DELAY          0
#define SNTP_RECV_TIMEOUT           3000   /* dead server → next one after 3 s (default 15 s) */
#define SNTP_RETRY_TIMEOUT          3000
#define SNTP_RETRY_TIMEOUT_MAX      60000
#define SNTP_UPDATE_DELAY           3600000
#define SNTP_CHECK_RESPONSE         1      /* reply must come from the server we asked */
#define SNTP_SET_SYSTEM_TIME(sec)   net_time_sntp_set(sec)
void net_time_sntp_set(unsigned int sec);
#define LWIP_AUTOIP                 0
#define LWIP_DHCP_DOES_ACD_CHECK    0
/* DNS (2026-09-05): `ctrl <name> 443`, `ntp <name>`. Servers from DHCP (option 6), else 1.1.1.1 / 8.8.8.8 (net_mcu.c). */
#define LWIP_DNS                    1
#define DNS_TABLE_SIZE              4
#define DNS_MAX_NAME_LENGTH         64
#define DNS_MAX_SERVERS             2
#define LWIP_IGMP                   0

/* Memory: lwIP heap + pools (no libc malloc on bare metal). */
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#ifdef MC_TLS
#define MEM_SIZE                    (16 * 1024)   /* + altcp_tls state/config (mem_calloc) */
#else
#define MEM_SIZE                    (12 * 1024)
#endif
#define MEMP_NUM_PBUF               24
#define MEMP_NUM_UDP_PCB            8   /* 2 RTP + ctrl + dhcp + sntp + dns + margin */
#define MEMP_NUM_SYS_TIMEOUT        12  /* tcp, arp, ip reass, 2×dhcp, dns + sntp (2 timers) + margin */
/* TCP sized for ONE small control connection (WebSocket to the server): tiny
 * window and send buffer, no out-of-order queue — a few KB of RAM in total. */
#define MEMP_NUM_TCP_PCB            2
#define MEMP_NUM_TCP_PCB_LISTEN     1
#define MEMP_NUM_TCP_SEG            12
#define TCP_MSS                     536
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            8
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_QUEUE_OOSEQ             0
#define LWIP_TCP_KEEPALIVE          1
#define PBUF_POOL_SIZE              24   // was 16: full-duplex RTP (~400 pkts/s in)
                                          // + DHCP control traffic share this one
                                          // pool; too small starved DHCP renewals
                                          // (IP reverting to 0.0.0.0 under load) and
                                          // likely contributed to dropped/corrupted
                                          // audio frames under load. (Constrained by
                                          // the ~384K RAM region — see MCNET_RING.)
#define PBUF_POOL_BUFSIZE           1536

/* netif */
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0

/* Checksums in software (the F7 MAC can offload, but keep it simple/portable). */
#define LWIP_CHKSUM_ALGORITHM       3
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_ICMP         1

/* No stats / no debug at link-proof time. */
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif /* LWIPOPTS_H */
