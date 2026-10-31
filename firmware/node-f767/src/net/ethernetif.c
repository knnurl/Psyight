#include "ethernetif.h"

#include "board.h"
#include "lan8742.h"
#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include <stdio.h>
#include <string.h>

#define RX_BUFFER_COUNT 24u /* Pool for in-flight receive pbufs (descriptors plus the stack's). */
#define TX_TIMEOUT_MS 20u

/* A receive buffer: lwIP's custom pbuf header followed by the frame the DMA writes. */
typedef struct {
    struct pbuf_custom pbuf_custom;
    uint8_t buffer[(ETH_RX_BUF_SIZE + 31u) & ~31u] __attribute__((aligned(32)));
} rx_buffer;

LWIP_MEMPOOL_DECLARE(RX_POOL, RX_BUFFER_COUNT, sizeof(rx_buffer), "Zero-copy RX pool");

static ETH_DMADescTypeDef s_rx_desc[ETH_RX_DESC_CNT] __attribute__((aligned(32)));
static ETH_DMADescTypeDef s_tx_desc[ETH_TX_DESC_CNT] __attribute__((aligned(32)));
static ETH_TxPacketConfigTypeDef s_tx_config;
static lan8742_Object_t s_phy;
static volatile int s_rx_starved; /* The pool ran dry; the next free re-arms the descriptors. */
static ethernetif_stats s_stats;
ETH_HandleTypeDef g_eth;

/* Nucleo-144 RMII: PA1 REF_CLK, PA2 MDIO, PA7 CRS_DV, PC1 MDC, PC4 RXD0, PC5 RXD1, PB13 TXD1,
 * PG11 TX_EN, PG13 TXD0, all on AF11. The PHY supplies the 50 MHz reference clock. */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth) {
    (void)heth;
    __HAL_RCC_SYSCFG_CLK_ENABLE(); /* HAL_ETH_Init selects RMII in SYSCFG_PMC. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF11_ETH;
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOG, &g);
    __HAL_RCC_ETH_CLK_ENABLE();
}

/* The PHY driver reaches the PHY through these (MDIO via the MAC). */
static int32_t phy_init(void) {
    HAL_ETH_SetMDIOClockRange(&g_eth);
    return 0;
}
static int32_t phy_deinit(void) { return 0; }
static int32_t phy_read(uint32_t address, uint32_t reg, uint32_t *value) {
    return HAL_ETH_ReadPHYRegister(&g_eth, address, reg, value) == HAL_OK ? 0 : -1;
}
static int32_t phy_write(uint32_t address, uint32_t reg, uint32_t value) {
    return HAL_ETH_WritePHYRegister(&g_eth, address, reg, value) == HAL_OK ? 0 : -1;
}
static int32_t phy_tick(void) { return (int32_t)HAL_GetTick(); }

static void pbuf_free_custom(struct pbuf *p) {
    rx_buffer *b = (rx_buffer *)p; /* The custom pbuf is the buffer's first member. */
    LWIP_MEMPOOL_FREE(RX_POOL, b);
    if (s_rx_starved) {
        s_rx_starved = 0; /* HAL_ETH_ReadData re-arms the descriptors on its next call. */
    }
}

/* The driver asks for a buffer to hand to a receive descriptor. */
void HAL_ETH_RxAllocateCallback(uint8_t **buffer) {
    rx_buffer *b = (rx_buffer *)LWIP_MEMPOOL_ALLOC(RX_POOL);
    if (b == NULL) {
        if (!s_rx_starved) {
            ++s_stats.starved;
        }
        s_rx_starved = 1;
        *buffer = NULL;
        return;
    }
    *buffer = b->buffer;
    b->pbuf_custom.custom_free_function = pbuf_free_custom;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, &b->pbuf_custom, b->buffer, ETH_RX_BUF_SIZE);
}

/* The driver hands back each filled buffer of a frame, in order: chain them into one pbuf. */
void HAL_ETH_RxLinkCallback(void **start, void **end, uint8_t *buffer, uint16_t length) {
    struct pbuf *p = (struct pbuf *)(buffer - offsetof(rx_buffer, buffer));
    p->next = NULL;
    p->tot_len = 0;
    p->len = length;
    if (*start == NULL) {
        *start = p;
    } else {
        ((struct pbuf *)*end)->next = p;
    }
    *end = p;
    for (struct pbuf *q = (struct pbuf *)*start; q != NULL; q = q->next) {
        q->tot_len = (u16_t)(q->tot_len + length);
    }
}

/* A transmitted frame's pbuf, referenced in low_level_output, is released here. */
void HAL_ETH_TxFreeCallback(uint32_t *buffer) { pbuf_free((struct pbuf *)buffer); }

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    ETH_BufferTypeDef buffers[ETH_TX_DESC_CNT];
    memset(buffers, 0, sizeof(buffers));
    uint32_t i = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (i >= ETH_TX_DESC_CNT) {
            return ERR_IF; /* More fragments than descriptors. */
        }
        buffers[i].buffer = q->payload;
        buffers[i].len = q->len;
        if (i > 0) {
            buffers[i - 1].next = &buffers[i];
        }
        ++i;
    }
    s_tx_config.Length = p->tot_len;
    s_tx_config.TxBuffer = buffers;
    s_tx_config.pData = p;
    pbuf_ref(p); /* Held until HAL_ETH_TxFreeCallback. */
    HAL_ETH_ReleaseTxPacket(&g_eth);
    /* The descriptors record this address for the free callback. Only HAL_ETH_Transmit_IT sets
     * it: left NULL, no sent frame was ever released, and after some minutes the heap was full
     * and the node could not answer ARP, so the computer stopped sending to it. */
    g_eth.TxDescList.CurrentPacketAddress = (uint32_t *)p;
    const uint32_t first = g_eth.TxDescList.CurTxDesc;
    if (HAL_ETH_Transmit(&g_eth, &s_tx_config, TX_TIMEOUT_MS) != HAL_OK) {
        if (g_eth.TxDescList.CurTxDesc == first) {
            pbuf_free(p); /* No descriptor took it. Otherwise (a timeout) the HAL frees it. */
        }
        return ERR_IF;
    }
    return ERR_OK;
}

static void start_with_link(struct netif *netif, int32_t state) {
    ETH_MACConfigTypeDef mac;
    HAL_ETH_GetMACConfig(&g_eth, &mac);
    mac.Speed =
        (state == LAN8742_STATUS_100MBITS_FULLDUPLEX || state == LAN8742_STATUS_100MBITS_HALFDUPLEX)
            ? ETH_SPEED_100M
            : ETH_SPEED_10M;
    mac.DuplexMode =
        (state == LAN8742_STATUS_100MBITS_FULLDUPLEX || state == LAN8742_STATUS_10MBITS_FULLDUPLEX)
            ? ETH_FULLDUPLEX_MODE
            : ETH_HALFDUPLEX_MODE;
    mac.ChecksumOffload = ENABLE;
    HAL_ETH_SetMACConfig(&g_eth, &mac);
    HAL_ETH_Start(&g_eth);
    netif_set_up(netif);
    netif_set_link_up(netif);
}

static err_t low_level_init(struct netif *netif) {
    g_eth.Instance = ETH;
    g_eth.Init.MACAddr = netif->hwaddr;
    g_eth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    g_eth.Init.TxDesc = s_tx_desc;
    g_eth.Init.RxDesc = s_rx_desc;
    g_eth.Init.RxBuffLen = ETH_RX_BUF_SIZE;
    if (HAL_ETH_Init(&g_eth) != HAL_OK) {
        return ERR_IF;
    }
    memset(&s_tx_config, 0, sizeof(s_tx_config));
    s_tx_config.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    s_tx_config.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    s_tx_config.CRCPadCtrl = ETH_CRC_PAD_INSERT;
    LWIP_MEMPOOL_INIT(RX_POOL);

    /* sACN universes are multicast groups: pass all multicast (IGMP joins still go out, so
     * snooping switches forward the groups to this port). */
    ETH_MACFilterConfigTypeDef filter;
    HAL_ETH_GetMACFilterConfig(&g_eth, &filter);
    filter.PassAllMulticast = ENABLE;
    HAL_ETH_SetMACFilterConfig(&g_eth, &filter);

    lan8742_IOCtx_t io = {phy_init, phy_deinit, phy_write, phy_read, phy_tick};
    LAN8742_RegisterBusIO(&s_phy, &io);
    if (LAN8742_Init(&s_phy) != LAN8742_STATUS_OK) {
        return ERR_IF;
    }
    const int32_t state = LAN8742_GetLinkState(&s_phy);
    if (state >= LAN8742_STATUS_100MBITS_FULLDUPLEX && state <= LAN8742_STATUS_10MBITS_HALFDUPLEX) {
        start_with_link(netif, state);
    } else {
        netif_set_link_down(netif);
    }
    return ERR_OK;
}

err_t ethernetif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 't';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
    return low_level_init(netif);
}

void ethernetif_input(struct netif *netif) {
    struct pbuf *p = NULL;
    while (HAL_ETH_ReadData(&g_eth, (void **)&p) == HAL_OK) {
        if (p != NULL) {
            ++s_stats.frames;
            if (netif->input(p, netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
        p = NULL;
    }
    /* The receive DMA suspends when it finds no free descriptor (RM0410: "receive buffer
     * unavailable") and stays suspended until a poll demand, so frames arriving after a burst
     * would wait for the next one. Resume it now that HAL_ETH_ReadData has re-armed them. */
    if (g_eth.Instance->DMASR & ETH_DMASR_RBUS) {
        g_eth.Instance->DMASR = ETH_DMASR_RBUS; /* Write 1 to clear. */
        g_eth.Instance->DMARPDR = 0u;
        ++s_stats.suspended;
    }
}

ethernetif_stats ethernetif_get_stats(void) {
    ethernetif_stats s = s_stats;
    s.tx_in_use = g_eth.TxDescList.BuffersInUse;
    return s;
}

void ethernetif_link_poll(struct netif *netif) {
    const int32_t state = LAN8742_GetLinkState(&s_phy);
    const int up =
        state >= LAN8742_STATUS_100MBITS_FULLDUPLEX && state <= LAN8742_STATUS_10MBITS_HALFDUPLEX;
    if (up && !netif_is_link_up(netif)) {
        printf("net: link up (PHY state %ld)\r\n", (long)state);
        start_with_link(netif, state);
    } else if (!up && netif_is_link_up(netif)) {
        printf("net: link down (PHY state %ld)\r\n", (long)state);
        HAL_ETH_Stop(&g_eth);
        netif_set_down(netif);
        netif_set_link_down(netif);
    }
}
