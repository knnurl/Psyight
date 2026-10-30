/* Psyight pixel node on an ESP32 DevKit (WROOM-32) over WiFi: the STM32 node's core
 * (firmware/node-f767/src/core: Art-Net and sACN parsing, sync latching, signal-loss policies and
 * the fallback show, the text configuration protocol) with one WS2812 output, lane 0, on GPIO2
 * (D2) through a 74HCT245.
 *
 * Tasks: the network on core 0 with the WiFi (net.c), the LED output on core 1 (led.c: its RMT
 * interrupt then runs away from the WiFi's), the console on UART0. The core's state is shared
 * under one mutex. Configuration: the same text protocol as the STM32 node on UDP 6455, so the
 * app's Preflight > Node setup works unchanged, plus set wifi_ssid / set wifi_password (then save
 * and reboot); the same commands work typed on the USB serial console (115200 8N1). */
#include "config_store.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "led.h"
#include "net.h"
#include "node_core.h"
#include "nvs_flash.h"
#include "wifi.h"

#include <stdio.h>
#include <string.h>

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h" /* Gitignored: this machine's network as the default. */
#endif

#define LED_GPIO 2
#define LED_CORE 1
#define MIN_FRAME_MS 16u /* Animated output (fallback, fade) at about 60 fps. */
#define STATS_MS 5000u

static const char *TAG = "node";
static node_config s_config;
static wifi_credentials s_wifi;
static uint8_t s_mac[6];
static node_state s_node;
static uint8_t s_lanes[NODE_LANES][NODE_LANE_BYTES];
static SemaphoreHandle_t s_lock;
static volatile bool s_save;
static volatile bool s_reboot;
static volatile bool s_apply;

static void on_packet(const psy_packet *packet, bool art_net, uint32_t now_ms) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    node_on_packet(&s_node, packet, art_net, now_ms);
    xSemaphoreGive(s_lock);
}

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* One command, from the network or the console: the shared protocol, plus the WiFi settings. */
static void on_command(const char *command, size_t length, char *reply, size_t capacity) {
    char text[200];
    if (length >= sizeof(text)) {
        length = sizeof(text) - 1;
    }
    memcpy(text, command, length);
    text[length] = '\0';
    while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n' ||
                          text[length - 1] == ' ')) {
        text[--length] = '\0';
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (starts_with(text, "set wifi_ssid ")) {
        strncpy(s_wifi.ssid, text + strlen("set wifi_ssid "), sizeof(s_wifi.ssid) - 1);
        snprintf(reply, capacity, "ok wifi_ssid (save and reboot to use it)\n");
    } else if (starts_with(text, "set wifi_password ")) {
        strncpy(s_wifi.password, text + strlen("set wifi_password "), sizeof(s_wifi.password) - 1);
        snprintf(reply, capacity, "ok wifi_password (save and reboot to use it)\n");
    } else {
        const node_command_action action =
            node_config_command(&s_config, s_mac, text, length, reply, capacity);
        s_save |= action == NODE_CMD_SAVE;
        s_reboot |= action == NODE_CMD_REBOOT;
        s_apply |= action == NODE_CMD_APPLY;
        if (strcmp(text, "get") == 0) {
            const size_t used = strlen(reply);
            snprintf(reply + used, capacity - used,
                     "wifi_ssid %s\nwifi_password %s\nwifi %s, %d dBm\n", s_wifi.ssid,
                     s_wifi.password[0] != '\0' ? "(set)" : "(none)",
                     wifi_joined() ? "joined" : wifi_ap_open() ? "access point" : "joining",
                     wifi_rssi());
        }
    }
    xSemaphoreGive(s_lock);
}

static void led_task(void *arg) {
    (void)arg;
    if (!led_init(LED_GPIO)) {
        ESP_LOGE(TAG, "LED output did not start");
        vTaskDelete(NULL);
    }
    esp_task_wdt_add(NULL);
    uint16_t pixels = s_config.node.lanes[0].pixels;
    uint32_t last_frame = 0;
    uint32_t last_stats = net_now_ms();
    uint32_t frames_since = 0;
    for (;;) {
        esp_task_wdt_reset();
        const uint32_t now = net_now_ms();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool lost = node_signal_lost(&s_node, now);
        const bool due = led_ready() && (!lost || now - last_frame >= MIN_FRAME_MS);
        const bool changed = due && node_frame(&s_node, now, s_lanes);
        xSemaphoreGive(s_lock);
        /* Only this task writes s_lanes, so it reads them unlocked. */
        if (changed && led_send(s_lanes[0], (size_t)pixels * 3u)) {
            last_frame = now;
            ++frames_since;
        }
        if (s_apply && led_ready()) { /* Between two frames. */
            s_apply = false;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            node_init(&s_node, &s_config.node);
            pixels = s_config.node.lanes[0].pixels;
            if (pixels > NODE_MAX_PIXELS_PER_LANE) {
                pixels = NODE_MAX_PIXELS_PER_LANE;
            }
            xSemaphoreGive(s_lock);
            net_set_universes(s_config.node.first_universe, s_config.node.universe_count);
            ESP_LOGI(TAG, "config applied: %u universes from %u, %u pixels on GPIO%d",
                     s_config.node.universe_count, s_config.node.first_universe, pixels, LED_GPIO);
        }
        if (s_save) {
            s_save = false;
            const bool ok = config_save(&s_config) && wifi_credentials_save(&s_wifi);
            ESP_LOGI(TAG, "%s", ok ? "config saved" : "config save failed");
        }
        if (s_reboot) {
            vTaskDelay(pdMS_TO_TICKS(100)); /* Let the reply leave. */
            esp_restart();
        }
        if (now - last_stats >= STATS_MS) {
            uint8_t ip[4];
            uint8_t mask[4];
            wifi_address(ip, mask);
            printf("node %u.%u.%u.%u (%s, %d dBm): %lu fps out, %lu packets, %lu latches, "
                   "%lu sequence gaps, %lu losses, %lu ignored, %lu polls%s\n",
                   ip[0], ip[1], ip[2], ip[3],
                   wifi_joined() ? "joined" : wifi_ap_open() ? "access point" : "joining",
                   wifi_rssi(), (unsigned long)(frames_since * 1000u / (now - last_stats)),
                   (unsigned long)s_node.packets, (unsigned long)s_node.latches,
                   (unsigned long)s_node.sequence_gaps, (unsigned long)s_node.losses,
                   (unsigned long)s_node.ignored, (unsigned long)net_polls(),
                   lost ? ", signal lost" : "");
            frames_since = 0;
            last_stats = now;
        }
        vTaskDelay(1);
    }
}

/* Commands typed on the USB serial console, one per line, answered there. */
static void console_task(void *arg) {
    (void)arg;
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    static char line[200];
    static char reply[1400];
    size_t used = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (used > 0) {
                on_command(line, used, reply, sizeof(reply));
                printf("%s", reply);
                used = 0;
            }
        } else if (used < sizeof(line) - 1) {
            line[used++] = (char)c;
        }
    }
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
    const bool stored = config_load(&s_config, s_mac);
    wifi_credentials_load(&s_wifi);
#ifdef WIFI_DEFAULT_SSID
    if (s_wifi.ssid[0] == '\0') {
        strncpy(s_wifi.ssid, WIFI_DEFAULT_SSID, sizeof(s_wifi.ssid) - 1);
        strncpy(s_wifi.password, WIFI_DEFAULT_PASSWORD, sizeof(s_wifi.password) - 1);
    }
#endif
    printf("\nPsyight ESP32 node, MAC %02x:%02x:%02x:%02x:%02x:%02x, %s config, LEDs on GPIO%d\n",
           s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
           stored ? "stored" : "default", LED_GPIO);
    s_lock = xSemaphoreCreateMutex();
    node_init(&s_node, &s_config.node);
    xTaskCreatePinnedToCore(led_task, "led", 4096, NULL, 10, NULL, LED_CORE);
    wifi_start(&s_config, &s_wifi, s_mac, net_rejoin);
    net_start(&s_config, s_mac, on_packet, on_command);
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
}
