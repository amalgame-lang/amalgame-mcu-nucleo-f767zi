/* lwIP NO_SYS system glue: init/poll, the millisecond clock lwIP polls, and the
 * DHCP randomness source. Does NOT touch AmalgameList (that lives in the amc TU via
 * net_lwip.am's @c) — this file only speaks lwIP + libopencm3. */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "ethernetif.h"
#include "net_mcu.h"
#include "lwip/apps/sntp.h"
#include "wallclock.h"

#include <libopencm3/cm3/common.h>
#include <libopencm3/ethernet/phy.h>
#include <stdint.h>

static struct netif mcnet_netif;

/* lwIP's millisecond time base. sys_tick_handler must be wired to a 1 kHz SysTick
 * at board bring-up (the blink startup does not yet configure SysTick). */
static volatile uint32_t lwip_ms = 0;
uint32_t sys_now(void) { return lwip_ms; }
void sys_tick_handler(void) { lwip_ms++; }

/* xorshift32 — enough entropy for DHCP xid / timing jitter on a link-proof. */
uint32_t lwip_rand(void)
{
    static uint32_t s = 0x2545F491u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static uint32_t mcnet_ms_now(void) { return lwip_ms; }

void net_init(void)
{
    lwip_init();
    wallclock_bind(mcnet_ms_now);

    ip4_addr_t any;
    ip4_addr_set_zero(&any);
    netif_add(&mcnet_netif, &any, &any, &any, NULL, ethernetif_init, netif_input);
    netif_set_default(&mcnet_netif);
    netif_set_up(&mcnet_netif);
    dhcp_start(&mcnet_netif);
}

/* MAC-level RX loss, read from ETH_DMAMFBOCR (read clears it): frames the DMA
 * dropped because no RX descriptor was free (MFC, host too slow / burst > ring),
 * and frames lost to RX FIFO overflow (MFA). Invisible to lwIP otherwise. */
static volatile uint32_t mcnet_rx_missed, mcnet_rx_fifo_ovf;

/* ── wall clock via SNTP (lwIP apps/sntp, poll mode). Started when DHCP has bound: by then dhcp.c has
 * already handed option 42 to sntp (dhcp_set_ntp_servers) if the router offers one; otherwise the fixed
 * fallbacks take slot 0. A manual server (`ntp <ip>`) owns slot 0 and DHCP is told to keep off. ── */
#ifndef MCNET_NTP_FALLBACK_1
#define MCNET_NTP_FALLBACK_1 "162.159.200.1"     /* time.cloudflare.com (anycast, stable) — no DNS in lwIP yet */
#endif
#ifndef MCNET_NTP_FALLBACK_2
#define MCNET_NTP_FALLBACK_2 "162.159.200.123"
#endif
static int mcnet_ntp_mode;            /* 0 auto, 1 manual, 2 stopped */
static ip_addr_t mcnet_ntp_manual;
static unsigned mcnet_sntp_started;
void net_time_sntp_set(unsigned int sec) { wallclock_set(sec); }
static void net_time_start(void)
{
    ip_addr_t a;
    sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (mcnet_ntp_mode == 1) {
        sntp_servermode_dhcp(0);
        sntp_setserver(0, &mcnet_ntp_manual);
        ipaddr_aton(MCNET_NTP_FALLBACK_1, &a); sntp_setserver(1, &a);
        ipaddr_aton(MCNET_NTP_FALLBACK_2, &a); sntp_setserver(2, &a);
    } else {
        sntp_servermode_dhcp(1);
        int dhcp_has = !ip_addr_isany(sntp_getserver(0));      /* option 42 already applied by dhcp.c */
        ipaddr_aton(MCNET_NTP_FALLBACK_1, &a); sntp_setserver(dhcp_has ? 1 : 0, &a);
        ipaddr_aton(MCNET_NTP_FALLBACK_2, &a); sntp_setserver(dhcp_has ? 2 : 1, &a);
    }
    sntp_init();
    mcnet_sntp_started++;
}
static void net_time_poll(void)
{
    if (mcnet_sntp_started || mcnet_ntp_mode == 2) return;
    if (ip4_addr_isany_val(*netif_ip4_addr(&mcnet_netif))) return;   /* wait for DHCP */
    net_time_start();
}
int net_time_set_server(const char *ip)
{
    if (!ip || !ip[0]) { mcnet_ntp_mode = 0; }
    else { if (!ipaddr_aton(ip, &mcnet_ntp_manual)) return 0; mcnet_ntp_mode = 1; }
    if (mcnet_sntp_started) net_time_start();
    return 1;
}
void net_time_stop(void) { sntp_stop(); mcnet_ntp_mode = 2; }
void net_time_restart(void) { if (mcnet_ntp_mode == 2) mcnet_ntp_mode = 0; if (!ip4_addr_isany_val(*netif_ip4_addr(&mcnet_netif))) net_time_start(); }
int net_time_mode(void) { return mcnet_ntp_mode; }
static u8_t net_time_cur_idx(void)
{   /* the slot lwIP is polling = the one whose reachability register moved last; good enough for diagnostics */
    for (u8_t i = 0; i < SNTP_MAX_SERVERS; i++) if (sntp_getreachability(i) & 1) return i;
    for (u8_t i = 0; i < SNTP_MAX_SERVERS; i++) if (sntp_getreachability(i)) return i;
    return 0;
}
const char *net_time_server_str(void) { return ipaddr_ntoa(sntp_getserver(net_time_cur_idx())); }
unsigned net_time_reach(void) { return sntp_getreachability(net_time_cur_idx()); }
unsigned net_time_sntp_started(void) { return mcnet_sntp_started; }
#include <libopencm3/ethernet/mac_stm32fxx7.h>
#ifndef ETH_DMAMFBOCR_MFA_SHIFT
#define ETH_DMAMFBOCR_MFA_SHIFT 17   /* RM0410: MFA = bits 27:17 (missing from libopencm3's header) */
#endif
void net_poll(void)
{
    ethernetif_poll(&mcnet_netif);
    ethernetif_link_poll(&mcnet_netif);
    sys_check_timeouts();
    net_time_poll();
    uint32_t m = ETH_DMAMFBOCR;
    mcnet_rx_missed   += m & ETH_DMAMFBOCR_MFC;
    mcnet_rx_fifo_ovf += (m & ETH_DMAMFBOCR_MFA) >> ETH_DMAMFBOCR_MFA_SHIFT;
}
int net_rx_missed(void)   { return (int) mcnet_rx_missed; }
/* MMC counters (RM0410): received frames with CRC error / alignment error — frames the
 * MAC silently drops. Distinguishes 'the switch never sent it' from 'it arrived corrupt'. */
int net_rx_crc_errors(void)   { return (int) *(volatile uint32_t *) (0x40028000u + 0x194u); }
int net_rx_align_errors(void) { return (int) *(volatile uint32_t *) (0x40028000u + 0x198u); }

/* Same UID words the MAC is derived from (ethernetif.c), folded to 32 bits. Never
 * 0 (0 = 'free slot' in the jitter's SSRC table). */
unsigned int net_uid32(void)
{
    const volatile uint32_t *uid = (const volatile uint32_t *) 0x1FF0F420u;
    uint32_t h = (uid[0] ^ (uid[1] * 2654435761u) ^ (uid[2] * 40503u)) & 0x7fffffffu;
    /* 31 bits: AM List<int> elements are void* = 32-bit on cortex-m7, so any value
     * >= 2^31 read back negative and an SSRC would never match its own table entry
     * (measured 2026-09-03: streams=4, all silent). */
    return h ? h : 1u;
}
int net_rx_fifo_ovf(void) { return (int) mcnet_rx_fifo_ovf; }

/* Diagnostics for bring-up: the acquired IPv4 ("0.0.0.0" until DHCP completes) and
 * the PHY link state. */
const char *net_ip_str(void)
{
    return ip4addr_ntoa(netif_ip4_addr(&mcnet_netif));
}

int net_link_up(void)
{
    return ethernetif_link_up();
}

int net_link_speed(void)
{
    return ethernetif_link_speed();
}

int net_rx_count(void)
{
    return (int) mcnet_rx_frames;
}

int net_tx_drops(void)
{
    return (int) mcnet_tx_drops;
}

/* Diagnostic: the derived MAC (see ethernetif.c mac_addr_init) as "aa:bb:cc:dd:ee:ff" —
 * lets two boards on the same LAN confirm they didn't (by 1-in-16M coincidence)
 * derive the same address from their UIDs. */
/* Diagnostic: elapsed ms since boot (1kHz SysTick) — for throughput measurements
 * that don't need DWT-cycle precision (DWT_CYCCNT needs an active debug session to
 * count on this core; useless once the board runs standalone after a plain reset). */
unsigned int net_millis(void)
{
    return lwip_ms;
}

/* Microsecond clock = SysTick ms counter + the SysTick down-counter's sub-ms
 * position (reload 215999 @ 216 MHz AHB → 216 ticks/us, see boards/clock.c). The
 * ms is read before and after CVR and retried if a tick landed in between. */
#define MCNET_STK_CVR (*(volatile uint32_t *) 0xE000E018u)
unsigned long long net_micros(void)
{
    uint32_t ms1, ms2, cvr;
    do { ms1 = lwip_ms; cvr = MCNET_STK_CVR; ms2 = lwip_ms; } while (ms1 != ms2);
    return (unsigned long long) ms1 * 1000ull + (unsigned long long) ((215999u - cvr) / 216u);
}

const char *net_mac_str(void)
{
    static char buf[18];
    const uint8_t *h = mcnet_netif.hwaddr;
    static const char hex[] = "0123456789abcdef";
    int p = 0;
    for (int i = 0; i < 6; i++) {
        buf[p++] = hex[(h[i] >> 4) & 0xF];
        buf[p++] = hex[h[i] & 0xF];
        if (i < 5) { buf[p++] = ':'; }
    }
    buf[p] = 0;
    return buf;
}
