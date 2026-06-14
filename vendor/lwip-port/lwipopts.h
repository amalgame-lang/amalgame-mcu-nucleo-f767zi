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
#define LWIP_TCP                    0
#define LWIP_UDP                    1
#define LWIP_RAW                    0
#define LWIP_ARP                    1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_DNS                    0
#define LWIP_IGMP                   0

/* Memory: lwIP heap + pools (no libc malloc on bare metal). */
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (8 * 1024)
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            6
#define MEMP_NUM_SYS_TIMEOUT        8
#define PBUF_POOL_SIZE              16
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
