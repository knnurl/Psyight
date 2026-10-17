/* The node configuration in the flash's last sector. The F767's 2 MB flash is single-bank by
 * default (option bit nDBANK set: last sector 11, 256 KB at 0x081C0000) or dual-bank (sector 23,
 * 128 KB at 0x081E0000); the bank mode is read at run time. The linker keeps code out of the last
 * 256 KB either way. Erasing stalls instruction fetches from the bank (1 to 2 s for 256 KB):
 * LEDs and network pause during a save, which the 4 s watchdog allows. A config failing its
 * magic, version or CRC is replaced by defaults, so a power cut mid-save cannot brick the node. */
#ifndef PSY_CONFIG_STORE_H
#define PSY_CONFIG_STORE_H

#include "node_config.h"

#include <stdbool.h>

/* Returns false (and fills defaults) when the stored config is missing or invalid. */
bool config_load(node_config *config, const uint8_t mac[6]);
bool config_save(const node_config *config);

#endif
