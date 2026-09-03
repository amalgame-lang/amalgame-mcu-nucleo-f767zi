#ifndef MCNET_ETHERNETIF_H
#define MCNET_ETHERNETIF_H

#include "lwip/err.h"
#include "lwip/netif.h"

/* lwIP netif init for the STM32F767 ETH MAC (RMII, LAN8742 PHY) via libopencm3. */
err_t ethernetif_init(struct netif *netif);

/* Poll the MAC for received frames and push them into lwIP (call from net_poll). */
void  ethernetif_poll(struct netif *netif);

/* Bring-up diagnostic: count of frames received from the MAC. */
extern volatile uint32_t mcnet_rx_frames;

extern volatile uint32_t mcnet_tx_drops;   /* frames refused by eth_tx (TX ring full) */
void ethernetif_link_poll(struct netif *netif);  /* call from net_poll: PHY link monitor */
int  ethernetif_link_up(void);                   /* cached link state */
int  ethernetif_link_speed(void);                /* +100/+10 full, -100/-10 half, 0 down */
#endif /* MCNET_ETHERNETIF_H */
