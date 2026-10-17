/* lwIP 2.2 configuration for the node: no RTOS (raw API, polled from the main loop), UDP only
 * (Art-Net, sACN, DHCP, the config protocol), IGMP for sACN multicast, checksums in the MAC. */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_TIMERS 1

#define MEM_ALIGNMENT 4
#define MEM_SIZE (16 * 1024)
#define MEMP_NUM_PBUF 16
#define MEMP_NUM_UDP_PCB 6
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 2)
#define MEMP_NUM_IGMP_GROUP (24 + 2) /* One per sACN universe, plus all-systems. */
#define PBUF_POOL_SIZE 8
#define PBUF_POOL_BUFSIZE 1536

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 0
#define LWIP_RAW 0
#define LWIP_DNS 0
#define LWIP_IGMP 1
#define LWIP_DHCP 1
#define LWIP_DHCP_DOES_ACD_CHECK 0 /* No address conflict detection (would need LWIP_ACD). */
#define LWIP_AUTOIP 0
#define IP_REASSEMBLY 0 /* The largest packet (sACN, 638 bytes) is never fragmented. */
#define IP_FRAG 0
#define LWIP_SINGLE_NETIF 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_STATS 0

/* The MAC inserts and checks IPv4, UDP and ICMP checksums. */
#define CHECKSUM_GEN_IP 0
#define CHECKSUM_GEN_UDP 0
#define CHECKSUM_GEN_ICMP 0
#define CHECKSUM_CHECK_IP 0
#define CHECKSUM_CHECK_UDP 0
#define CHECKSUM_CHECK_ICMP 0

#define ETH_PAD_SIZE 0
#define LWIP_SUPPORT_CUSTOM_PBUF 1 /* Zero-copy receive into the MAC's DMA buffers. */
#define LWIP_DEBUG 0

#endif
