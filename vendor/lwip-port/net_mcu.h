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
int         net_tx_drops(void); /* frames refused by eth_tx (TX descriptor ring full) */
const char *net_mac_str(void);  /* derived MAC, "aa:bb:cc:dd:ee:ff" */
unsigned int net_millis(void);  /* elapsed ms since boot (1kHz SysTick) */
unsigned long long net_micros(void); /* elapsed us since boot: SysTick ms + STK_CVR sub-ms
                                      * (216 ticks/us @ 216 MHz AHB) — ~5 ns resolution,
                                      * no debug session needed (unlike DWT_CYCCNT). */

#endif /* MCNET_NET_MCU_H */
