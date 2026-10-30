/* WiFi: joins the stored network (DHCP, or the config's fixed address with dhcp 0); when there
 * are no credentials, or it has not joined within WIFI_JOIN_TIMEOUT_MS, it also opens its own
 * access point, "Psyight-node-XXXX" (the MAC's last two bytes; WPA2, password
 * WIFI_AP_PASSWORD, the node at 192.168.4.1), and keeps trying to join; once joined, the access
 * point closes. Power saving is off: the radio would otherwise sleep between beacons and sACN at
 * 60 fps would arrive late, in bursts. */
#ifndef PSY_WIFI_H
#define PSY_WIFI_H

#include "config_store.h"

#include <stdbool.h>
#include <stdint.h>

#define WIFI_JOIN_TIMEOUT_MS 20000
#define WIFI_AP_PASSWORD "psyight-node"

/* on_address runs (on the event task) whenever the node has a new address to serve on. */
void wifi_start(const node_config *config, const wifi_credentials *wifi, const uint8_t mac[6],
                void (*on_address)(void));
bool wifi_joined(void);   /* Joined the stored network, with an address. */
bool wifi_ap_open(void);  /* The fallback access point is up. */
/* The address to serve on: the joined network's, else the access point's. */
void wifi_address(uint8_t ip[4], uint8_t mask[4]);
int wifi_rssi(void);      /* dBm, 0 when not joined. */

#endif
