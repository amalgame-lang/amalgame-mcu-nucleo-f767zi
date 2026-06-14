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

void net_init(void)
{
    lwip_init();

    ip4_addr_t any;
    ip4_addr_set_zero(&any);
    netif_add(&mcnet_netif, &any, &any, &any, NULL, ethernetif_init, netif_input);
    netif_set_default(&mcnet_netif);
    netif_set_up(&mcnet_netif);
    dhcp_start(&mcnet_netif);
}

void net_poll(void)
{
    ethernetif_poll(&mcnet_netif);
    sys_check_timeouts();
}

/* Diagnostics for bring-up: the acquired IPv4 ("0.0.0.0" until DHCP completes) and
 * the PHY link state. */
const char *net_ip_str(void)
{
    return ip4addr_ntoa(netif_ip4_addr(&mcnet_netif));
}

int net_link_up(void)
{
    return phy_link_isup(0) ? 1 : 0;
}

int net_rx_count(void)
{
    return (int) mcnet_rx_frames;
}
