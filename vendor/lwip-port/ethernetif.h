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

#endif /* MCNET_ETHERNETIF_H */
