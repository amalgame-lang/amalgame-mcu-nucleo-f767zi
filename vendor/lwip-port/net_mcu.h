#ifndef MCNET_NET_MCU_H
#define MCNET_NET_MCU_H

/* Bring up lwIP + the ETH netif + DHCP (call once from setup{}). */
void net_init(void);

/* Service the MAC RX path and lwIP timeouts (call every loop{} tick). */
void net_poll(void);

/* Bring-up diagnostics. */
const char *net_ip_str(void);   /* "0.0.0.0" until DHCP completes */
int         net_link_up(void);  /* PHY link state */
int         net_rx_count(void); /* frames received from the MAC (0 = RX path dead) */

#endif /* MCNET_NET_MCU_H */
