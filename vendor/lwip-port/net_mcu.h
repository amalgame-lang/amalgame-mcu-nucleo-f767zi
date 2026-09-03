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
unsigned int net_uid32(void);      /* 32-bit hash of the STM32 96-bit UID (box identity / RTP SSRC) */
const char *net_mac_str(void);  /* derived MAC, "aa:bb:cc:dd:ee:ff" */
unsigned int net_millis(void);  /* elapsed ms since boot (1kHz SysTick) */
unsigned long long net_micros(void); /* elapsed us since boot: SysTick ms + STK_CVR sub-ms
                                      * (216 ticks/us @ 216 MHz AHB) — ~5 ns resolution,
                                      * no debug session needed (unlike DWT_CYCCNT). */

#endif /* MCNET_NET_MCU_H */
