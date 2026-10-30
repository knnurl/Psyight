#include "wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi";
static esp_netif_t *s_sta;
static esp_netif_t *s_ap;
static esp_timer_handle_t s_join_timer;
static volatile bool s_joined;
static volatile bool s_ap_open;
static bool s_have_credentials;
static void (*s_on_address)(void);

static void open_ap(void) {
    if (!s_ap_open) {
        esp_wifi_set_mode(s_have_credentials ? WIFI_MODE_APSTA : WIFI_MODE_AP);
        s_ap_open = true;
        ESP_LOGI(TAG, "access point open (password %s), the node at 192.168.4.1", WIFI_AP_PASSWORD);
        if (s_on_address != NULL) {
            s_on_address();
        }
    }
}

static void on_join_timeout(void *arg) {
    (void)arg;
    if (!s_joined) {
        ESP_LOGW(TAG, "not joined in %d s: opening the access point, still trying",
                 WIFI_JOIN_TIMEOUT_MS / 1000);
        open_ap();
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_joined) {
            ESP_LOGW(TAG, "left the network; rejoining");
            esp_timer_stop(s_join_timer);
            esp_timer_start_once(s_join_timer, (uint64_t)WIFI_JOIN_TIMEOUT_MS * 1000u);
        }
        s_joined = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "joined: " IPSTR, IP2STR(&got->ip_info.ip));
        s_joined = true;
        esp_timer_stop(s_join_timer);
        if (s_ap_open) {
            esp_wifi_set_mode(WIFI_MODE_STA); /* Joined: the fallback closes. */
            s_ap_open = false;
        }
        if (s_on_address != NULL) {
            s_on_address();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a device joined the access point");
    }
}

void wifi_start(const node_config *config, const wifi_credentials *wifi, const uint8_t mac[6],
                void (*on_address)(void)) {
    s_on_address = on_address;
    s_have_credentials = wifi->ssid[0] != '\0';
    esp_netif_init();
    esp_event_loop_create_default();
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, config->short_name);
    if (!config->dhcp) {
        esp_netif_dhcpc_stop(s_sta);
        esp_netif_ip_info_t info;
        memset(&info, 0, sizeof(info));
        info.ip.addr = ESP_IP4TOADDR(config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
        info.netmask.addr =
            ESP_IP4TOADDR(config->mask[0], config->mask[1], config->mask[2], config->mask[3]);
        info.gw.addr = ESP_IP4TOADDR(config->gateway[0], config->gateway[1], config->gateway[2],
                                     config->gateway[3]);
        esp_netif_set_ip_info(s_sta, &info);
    }
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    wifi_config_t ap;
    memset(&ap, 0, sizeof(ap));
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Psyight-node-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = (uint8_t)strlen((const char *)ap.ap.ssid);
    strncpy((char *)ap.ap.password, WIFI_AP_PASSWORD, sizeof(ap.ap.password) - 1);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;

    const esp_timer_create_args_t timer = {.callback = on_join_timeout, .name = "wifi_join"};
    esp_timer_create(&timer, &s_join_timer);

    if (s_have_credentials) {
        wifi_config_t sta;
        memset(&sta, 0, sizeof(sta));
        strncpy((char *)sta.sta.ssid, wifi->ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, wifi->password, sizeof(sta.sta.password) - 1);
        sta.sta.threshold.authmode = wifi->password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_set_config(WIFI_IF_AP, &ap); /* Ready for the fallback. */
        ESP_LOGI(TAG, "joining \"%s\"", wifi->ssid);
        esp_timer_start_once(s_join_timer, (uint64_t)WIFI_JOIN_TIMEOUT_MS * 1000u);
    } else {
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &ap);
        ESP_LOGW(TAG, "no network stored: set wifi_ssid and wifi_password, save, reboot");
    }
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (!s_have_credentials) {
        s_ap_open = false;
        open_ap();
    }
}

bool wifi_joined(void) { return s_joined; }

bool wifi_ap_open(void) { return s_ap_open; }

void wifi_address(uint8_t ip[4], uint8_t mask[4]) {
    esp_netif_ip_info_t info;
    memset(&info, 0, sizeof(info));
    esp_netif_get_ip_info(s_joined || !s_ap_open ? s_sta : s_ap, &info);
    const uint32_t a = info.ip.addr; /* Network order: first octet in the low byte. */
    const uint32_t m = info.netmask.addr;
    for (int i = 0; i < 4; ++i) {
        ip[i] = (uint8_t)(a >> (8 * i));
        mask[i] = (uint8_t)(m >> (8 * i));
    }
}

int wifi_rssi(void) {
    wifi_ap_record_t ap;
    return s_joined && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}
