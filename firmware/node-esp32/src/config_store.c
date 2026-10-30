#include "config_store.h"

#include "nvs.h"

#include <string.h>

#define NAMESPACE "psyight"

bool config_load(node_config *config, const uint8_t mac[6]) {
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t size = sizeof(*config);
        ok = nvs_get_blob(h, "config", config, &size) == ESP_OK && size == sizeof(*config) &&
             node_config_valid(config);
        nvs_close(h);
    }
    if (!ok) {
        node_config_defaults(config, mac);
        /* One output, GPIO2: the default rig on lane 0 alone until the app sets this one up. */
        config->active_mask = 1;
    }
    return ok;
}

bool config_save(const node_config *config) {
    node_config copy = *config;
    copy.crc = node_config_crc(&copy);
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(h, "config", &copy, sizeof(copy)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void wifi_credentials_load(wifi_credentials *wifi) {
    memset(wifi, 0, sizeof(*wifi));
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t size = sizeof(wifi->ssid);
    if (nvs_get_str(h, "wifi_ssid", wifi->ssid, &size) != ESP_OK) {
        wifi->ssid[0] = '\0';
    }
    size = sizeof(wifi->password);
    if (nvs_get_str(h, "wifi_password", wifi->password, &size) != ESP_OK) {
        wifi->password[0] = '\0';
    }
    nvs_close(h);
}

bool wifi_credentials_save(const wifi_credentials *wifi) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_str(h, "wifi_ssid", wifi->ssid) == ESP_OK &&
                    nvs_set_str(h, "wifi_password", wifi->password) == ESP_OK &&
                    nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
