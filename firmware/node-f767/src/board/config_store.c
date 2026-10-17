#include "config_store.h"

#include "board.h"

#include <string.h>

static void last_sector(uint32_t *sector, uint32_t *address) {
    if (FLASH->OPTCR & FLASH_OPTCR_nDBANK) { /* Single bank: 4x32K, 128K, 7x256K. */
        *sector = FLASH_SECTOR_11;
        *address = 0x081C0000u;
    } else { /* Dual bank: the second bank's last 128K sector. */
        *sector = FLASH_SECTOR_23;
        *address = 0x081E0000u;
    }
}

bool config_load(node_config *config, const uint8_t mac[6]) {
    uint32_t sector, address;
    last_sector(&sector, &address);
    memcpy(config, (const void *)address, sizeof(*config));
    if (node_config_valid(config)) {
        return true;
    }
    node_config_defaults(config, mac);
    return false;
}

bool config_save(const node_config *config) {
    uint32_t sector, address;
    last_sector(&sector, &address);
    node_config copy = *config;
    copy.crc = node_config_crc(&copy);
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = sector;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7 to 3.6 V: 32-bit programming. */
    uint32_t bad_sector = 0;
    bool ok = HAL_FLASHEx_Erase(&erase, &bad_sector) == HAL_OK;
    const uint32_t *words = (const uint32_t *)&copy;
    for (size_t i = 0; ok && i < (sizeof(copy) + 3u) / 4u; ++i) {
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + 4u * i, words[i]) == HAL_OK;
    }
    HAL_FLASH_Lock();
    return ok && memcmp((const void *)address, &copy, sizeof(copy)) == 0;
}
