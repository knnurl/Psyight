/* The node's network side: lwIP (no RTOS) on the MAC, addressing (static, or DHCP falling back to
 * the Art-Net style 2.x.y.z/8 address after 10 s without a lease), UDP on 6454 (Art-Net, with
 * ArtPollReply), 5568 (sACN, IGMP joins per universe) and 6455 (configuration). Everything runs
 * from net_poll in the main loop; received packets go to the handler. */
#ifndef PSY_NET_H
#define PSY_NET_H

#include "node_config.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*net_packet_handler)(const psy_packet *packet, bool art_net, uint32_t now_ms);

void net_init(node_config *config, const uint8_t mac[6], net_packet_handler handler);
void net_poll(uint32_t now_ms);
bool net_link_up(void);
void net_address(uint8_t ip[4]);
uint32_t net_polls(void); /* ArtPolls received. */
/* Joins the multicast group of an sACN sync universe once data names it. */
void net_join_sync_universe(uint16_t universe);
/* Set when a config command asked for a save, a reboot or an apply; the main loop acts on it. */
bool net_take_save_request(void);
bool net_take_reboot_request(void);
bool net_take_apply_request(void);
/* The sACN universes to receive: leaves the groups of the previous range, joins the new ones. */
void net_set_universes(uint16_t first, uint16_t count);

#endif
