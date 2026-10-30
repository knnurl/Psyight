/* The node's settings in NVS (namespace "psyight"): the shared node_config (as on the STM32
 * node, checked by its magic, version and CRC, else defaults) and the WiFi credentials, which only
 * this node has. */
#ifndef PSY_CONFIG_STORE_H
#define PSY_CONFIG_STORE_H

#include "node_config.h"

#include <stdbool.h>

typedef struct {
    char ssid[33];
    char password[65];
} wifi_credentials;

/* Returns false (and fills defaults) when the stored config is missing or invalid. */
bool config_load(node_config *config, const uint8_t mac[6]);
bool config_save(const node_config *config);
void wifi_credentials_load(wifi_credentials *wifi); /* Empty when none are stored. */
bool wifi_credentials_save(const wifi_credentials *wifi);

#endif
