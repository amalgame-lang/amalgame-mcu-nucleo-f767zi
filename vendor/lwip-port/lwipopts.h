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
/* DOUBLE PILE (2026-09-06). But : une adresse globale des deux côtés = connexion directe SANS NAT
 * (feuille de route §3). IPv4 reste indispensable — beaucoup de réseaux n'ont pas d'IPv6, et le
 * boîtier doit y fonctionner comme avant ; c'est la cascade de connectivité qui choisit.
 * Tailles réduites par rapport aux défauts lwIP : un boîtier voit une poignée de voisins (le routeur,
 * les autres boîtiers du local), pas un réseau d'entreprise. Coût RAM mesuré plus bas. */
#define LWIP_IPV6                   1
#define LWIP_IPV6_AUTOCONFIG        1    /* SLAAC : adresse globale depuis les RA du routeur */
#define LWIP_IPV6_NUM_ADDRESSES     3    /* lien-local + globale + une de rab (renumérotation) */
#define LWIP_ND6_NUM_NEIGHBORS      6    /* routeur + pairs du local (défaut 10) */
#define LWIP_ND6_NUM_DESTINATIONS   6
#define LWIP_ND6_NUM_PREFIXES       2
#define LWIP_ND6_NUM_ROUTERS        2
#define MEMP_NUM_ND6_QUEUE          4    /* paquets en attente de résolution d'adresse (défaut 20) */
#define LWIP_IPV6_DHCP6             0    /* Orange/Free : SLAAC suffit ; DHCPv6 si un réseau l'impose */

/* NOM SUR LE RÉSEAU (2026-09-06, demandé par l'utilisateur). Deux mécanismes, complémentaires :
 *  - option 12 du DHCP : le boîtier annonce son nom au routeur, qui l'affiche dans sa liste de baux.
 *    C'est ce qui remplace « un appareil inconnu à l'adresse 192.168.1.42 ».
 *  - mDNS : le boîtier répond à <nom>.local, donc on le joint SANS connaître son adresse — ce qui
 *    compte surtout quand la carte est chez quelqu'un d'autre, sur un réseau qu'on ne maîtrise pas.
 * MEMP_NUM_UDP_PCB +1 (le répondeur mDNS ouvre sa propre socket) et une donnée de netif pour lui. */
/* mDNS DÉSACTIVÉ (2026-09-06). Tentative faite, trois pièges levés — LWIP_DEBUG défini à 0 rendait
 * `#ifdef LWIP_DEBUG` vrai et tirait snprintf/malloc/_sbrk ; NETIF_FLAG_IGMP manquait sur l'interface ;
 * le répondeur doit être prévenu des changements d'adresse (LWIP_NETIF_EXT_STATUS_CALLBACK). Résultat :
 * la carte répond UNE fois, pendant sa fenêtre d'annonce, puis plus du tout aux interrogations
 * (0 résolution sur 6). Cause non trouvée. Coût mesuré : +13 Ko de flash, +1,2 Ko de RAM, et du code
 * multicast à côté du chemin audio — trop cher pour une fonction à moitié faite sur une carte qui part
 * chez un tiers. Le NOM reste annoncé par l'option 12 du DHCP (netif_set_hostname, net_mcu.c), ce qui
 * couvre le besoin d'origine : voir le boîtier par son nom dans la box. À reprendre à froid.
 * Pour réessayer : remettre à 1 ici, les trois correctifs ci-dessus sont conservés. */
#define LWIP_MDNS_RESPONDER         0
/* Le répondeur doit être PRÉVENU quand l'adresse change : le nom est annoncé au démarrage, alors que
 * l'adresse n'arrive qu'avec le DHCP quelques secondes plus tard. Sans ce rappel, la carte ne répond
 * jamais à son nom (constaté 2026-09-06). MDNS_RESP_USENETIF_EXTCALLBACK suit cette option. */
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1
#define LWIP_NUM_NETIF_CLIENT_DATA  1
#define MDNS_MAX_SERVICES           1
/* LWIP_IGMP et LWIP_NETIF_HOSTNAME sont définis plus bas avec les autres options du même domaine —
 * les redéfinir ici ne servait à rien : la seconde définition gagne (piège vécu, IGMP restait à 0). */
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
#define LWIP_IGMP                   0   /* requis seulement par le répondeur mDNS (désactivé) */

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
#define MEMP_NUM_UDP_PCB            9   /* RTP + ctrl + dhcp + sntp + dns + mdns + marge */
#define MEMP_NUM_SYS_TIMEOUT        12  /* tcp, arp, ip reass, 2×dhcp, dns + sntp (2 timers) + margin */
/* TCP sized for ONE small control connection (WebSocket to the server): tiny
 * window and send buffer, no out-of-order queue — a few KB of RAM in total. */
/* 3 et non 2 : le client wss vers le serveur en occupe un EN PERMANENCE, et le client HTTP d'UPnP
 * (description du routeur puis SOAP) en demande un second. À 2 il ne restait aucune marge — un pcb
 * qui s'attarde en fermeture et la connexion suivante échoue sans rien dire. ~200 o de plus. */
#define MEMP_NUM_TCP_PCB            3
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
/* NE PAS définir LWIP_DEBUG hors débogage, MÊME À ZÉRO : plusieurs fichiers de lwIP testent
 * `#ifdef LWIP_DEBUG`, qui est VRAI pour une définition à 0. Le code de trace était donc compilé, et
 * son snprintf tirait malloc, donc _sbrk et les appels système de la newlib — que ce firmware n'a
 * pas (constaté 2026-09-06 en activant mDNS). Pour déboguer : -DLWIP_DEBUG=LWIP_DBG_ON en CFLAGS. */
/* #define LWIP_DEBUG               0 */

#endif /* LWIPOPTS_H */
