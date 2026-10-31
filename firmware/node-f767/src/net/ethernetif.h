/* lwIP network interface on the STM32F7 MAC (new HAL ETH driver) and the Nucleo's LAN8742 PHY
 * over RMII. Zero-copy receive: the MAC's DMA writes frames into pbufs from a pool. Polled: call
 * ethernetif_input from the main loop, ethernetif_link_poll every ~100 ms. */
#ifndef PSY_ETHERNETIF_H
#define PSY_ETHERNETIF_H

#include "lwip/err.h"
#include "lwip/netif.h"

err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
void ethernetif_link_poll(struct netif *netif);

/* Statistics for the console: frames handed to the stack, times the buffer pool ran dry, times
 * the receive DMA suspended for want of a descriptor (resumed at once), and transmit descriptors
 * still holding a frame (at most ETH_TX_DESC_CNT; more would mean frames are not released). */
typedef struct {
    uint32_t frames;
    uint32_t starved;
    uint32_t suspended;
    uint32_t tx_in_use;
} ethernetif_stats;
ethernetif_stats ethernetif_get_stats(void);

#endif
