/* The node's stored configuration and its text protocol (SPEC 12.2: "config stored in flash and
 * set over the network"). Portable: the app's tests compile it; flash storage is in
 * board/config_store.c.
 *
 * Protocol: one command per UDP datagram to port 6455, answered with text:
 *   get                      every setting, one "key value" line each
 *   set <key> <value>        change a setting in RAM (most take effect after save and reboot)
 *   save                     write to flash (the node pauses 1 to 2 s while the sector erases)
 *   defaults                 reset to defaults in RAM
 *   reboot                   restart
 * Keys: ip, mask, gateway (a.b.c.d), dhcp (0 or 1), name, long_name, universe (first),
 * universes (count), pixels_per_universe, lane0..lane7 ("first_pixel count"), active (lane bit
 * mask, decimal), loss_ms, policy (hold, fade, fallback, blackout), fade_ms, fallback_brightness
 * and fallback_speed (thousandths), protocols (1 Art-Net, 2 sACN, 3 both).
 */
#ifndef PSY_NODE_CONFIG_H
#define PSY_NODE_CONFIG_H

#include "node_core.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODE_CONFIG_MAGIC 0x50535931u /* "PSY1" */
#define NODE_CONFIG_VERSION 1u
#define NODE_CONFIG_PORT 6455
#define NODE_PROTOCOL_ART_NET 1u
#define NODE_PROTOCOL_SACN 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gateway[4];
    uint8_t dhcp;
    uint8_t protocols;
    uint8_t active_mask;
    uint8_t reserved;
    char short_name[18];
    char long_name[64];
    node_settings node;
    uint32_t crc; /* CRC-32 of everything before it. */
} node_config;

typedef enum { NODE_CMD_NONE, NODE_CMD_SAVE, NODE_CMD_REBOOT } node_command_action;

void node_config_defaults(node_config *c, const uint8_t mac[6]);
uint32_t node_config_crc(const node_config *c);
int node_config_valid(const node_config *c); /* Magic, version and CRC. */

/* Handles one command; writes the reply (null-terminated) and returns what the caller must do
 * next (save to flash, reboot) beyond what the reply says. */
node_command_action node_config_command(node_config *c, const uint8_t mac[6], const char *command,
                                        size_t length, char *reply, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
