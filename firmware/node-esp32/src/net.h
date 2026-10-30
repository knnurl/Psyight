/* The ESP32 node's network side: UDP sockets on 6454 (Art-Net, with ArtPollReply broadcast to the
 * subnet), 5568 (sACN, a multicast join per universe and the sync universe) and 6455 (the text
 * configuration protocol), served by one task on the WiFi's core. Received data goes to the
 * packet handler stamped with the time it arrived (the core takes care that this may be later
 * than the frame's own time); commands go to the command handler, whose reply is sent back. */
#ifndef PSY_NET_H
#define PSY_NET_H

#include "node_config.h"
#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*net_packet_handler)(const psy_packet *packet, bool art_net, uint32_t now_ms);
typedef void (*net_command_handler)(const char *command, size_t length, char *reply,
                                    size_t capacity);

uint32_t net_now_ms(void);
void net_start(const node_config *config, const uint8_t mac[6], net_packet_handler packets,
               net_command_handler commands);
/* The sACN universes to receive: leaves the previous range's groups, joins the new ones. */
void net_set_universes(uint16_t first, uint16_t count);
/* Joins the groups again (a new address: after joining the network or opening the AP). */
void net_rejoin(void);
uint32_t net_polls(void); /* ArtPolls received. */

#endif
