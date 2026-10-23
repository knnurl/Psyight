/* The node's stored configuration and its text protocol (SPEC 12.2: "config stored in flash and
 * set over the network"). Portable: the app's tests compile it; flash storage is in
 * board/config_store.c.
 *
 * Protocol (version NODE_PROTOCOL_VERSION): one command per UDP datagram to port 6455, answered
 * with text:
 *   get                      "protocol N", then every setting, one "key value" line each
 *   set <key> <value>        change a setting in RAM
 *   apply                    use the rig settings now: universes, lanes, outputs, signal loss,
 *                            fallback show (no flash write, no restart; save keeps them)
 *   save                     write to flash (the node pauses 1 to 2 s while the sector erases)
 *   defaults                 reset to defaults in RAM
 *   reboot                   restart
 * Keys: ip, mask, gateway (a.b.c.d): the fixed address, used with dhcp 0; dhcp (0 or 1): with 1,
 * the node asks for an address and, without a lease after 10 s, takes a link-local 169.254.x.y/16
 * (node_link_local_address), the range Windows gives a direct cable; all after save and reboot; name, long_name,
 * protocols (1 Art-Net, 2 sACN, 3 both): at once; the rig settings, on apply (or after a
 * restart): universe (first), universes (count), pixels_per_universe, lane0..lane7
 * ("first_pixel count"), active (lane bit mask, decimal), loss_ms, policy (hold, fade, fallback,
 * blackout), fade_ms, fallback_brightness and fallback_speed (thousandths), palette (the fallback
 * show's colours: one to four RRGGBB, space separated), order0..order7 (each lane's colour order:
 * rgb, grb, brg, rbg, gbr or bgr, for the fallback show; live data comes ordered by the app).
 * Version 1 had no apply, no palette and no protocol line: its rig settings took effect after
 * save and reboot. Version 2 had no lane orders (its fallback show assumed GRB).
 *
 * Stored layout (NODE_CONFIG_VERSION): version 1 lacked lane_order and kept the palette as GRB;
 * node_config_upgrade reads it, so a node keeps its settings across the firmware update.
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
#define NODE_CONFIG_VERSION 2u
#define NODE_CONFIG_PORT 6455
#define NODE_PROTOCOL_VERSION 3u
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

typedef enum { NODE_CMD_NONE, NODE_CMD_SAVE, NODE_CMD_REBOOT, NODE_CMD_APPLY } node_command_action;

void node_config_defaults(node_config *c, const uint8_t mac[6]);
uint32_t node_config_crc(const node_config *c);
int node_config_valid(const node_config *c); /* Magic, version and CRC. */

/* A valid version 1 config at `stored` (at least node_config_v1_size() bytes) as the current
 * one: the palette from GRB to RGB, every lane GRB. Returns 0 (out untouched) when it is not a
 * valid version 1 config. */
int node_config_upgrade(const void *stored, node_config *out);
size_t node_config_v1_size(void);

/* The node's link-local address (RFC 3927 range 169.254.1.0 to 169.254.254.255) from its MAC: the
 * DHCP fallback. */
void node_link_local_address(const uint8_t mac[6], uint8_t out[4]);

/* Handles one command; writes the reply (null-terminated) and returns what the caller must do
 * next (save to flash, reboot) beyond what the reply says. */
node_command_action node_config_command(node_config *c, const uint8_t mac[6], const char *command,
                                        size_t length, char *reply, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
