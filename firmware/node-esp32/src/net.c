#include "net.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "wifi.h"

#include <string.h>

#define NET_CORE 0
#define NET_STACK 6144

static const char *TAG = "net";
static const node_config *s_config;
static uint8_t s_mac[6];
static net_packet_handler s_packets;
static net_command_handler s_commands;
static int s_art = -1;
static int s_sacn = -1;
static int s_cfg = -1;
static int s_ddp = -1;
static uint16_t s_first;
static uint16_t s_count;
static uint16_t s_sync; /* The sync universe joined; 0: none. */
static volatile uint32_t s_polls;

uint32_t net_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static int listen_on(uint16_t port) {
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        return -1;
    }
    const int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static void membership(uint16_t universe, bool join) {
    if (s_sacn < 0 || universe == 0) {
        return;
    }
    uint8_t g[4];
    psy_sacn_group(universe, g);
    struct ip_mreq m;
    memset(&m, 0, sizeof(m));
    m.imr_multiaddr.s_addr =
        htonl(((uint32_t)g[0] << 24) | ((uint32_t)g[1] << 16) | ((uint32_t)g[2] << 8) | g[3]);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s_sacn, IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, &m, sizeof(m));
}

void net_set_universes(uint16_t first, uint16_t count) {
    for (uint16_t u = 0; u < s_count; ++u) {
        if ((uint16_t)(s_first + u) != s_sync) {
            membership((uint16_t)(s_first + u), false);
        }
    }
    s_first = first;
    s_count = count;
    for (uint16_t u = 0; u < count; ++u) {
        membership((uint16_t)(first + u), true);
    }
}

void net_rejoin(void) {
    const uint16_t sync = s_sync;
    s_sync = 0;
    net_set_universes(s_first, s_count);
    if (sync != 0) {
        membership(sync, true);
        s_sync = sync;
    }
}

uint32_t net_polls(void) { return s_polls; }

static void send_poll_replies(void) {
    /* As the STM32 node: one reply per up to four universes sharing a Sub-Net, to the subnet's
     * broadcast address (the controller listens on 6454 for any reply). */
    uint8_t ip[4];
    uint8_t mask[4];
    wifi_address(ip, mask);
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PSY_ARTNET_PORT);
    to.sin_addr.s_addr =
        htonl(((uint32_t)(ip[0] | (uint8_t)~mask[0]) << 24) |
              ((uint32_t)(ip[1] | (uint8_t)~mask[1]) << 16) |
              ((uint32_t)(ip[2] | (uint8_t)~mask[2]) << 8) | (uint8_t)(ip[3] | (uint8_t)~mask[3]));
    const node_settings *s = &s_config->node;
    uint8_t buffer[PSY_ART_POLL_REPLY_BYTES];
    psy_poll_reply r;
    memset(&r, 0, sizeof(r));
    memcpy(r.ip, ip, 4);
    memcpy(r.mac, s_mac, 6);
    r.short_name = s_config->short_name;
    r.long_name = s_config->long_name;
    r.report = "#0001 [0000] Psyight ESP32 node running";
    uint8_t bind_index = 1;
    for (uint16_t i = 0; i < s->universe_count;) {
        r.universe_count = 0;
        const uint16_t first = (uint16_t)(s->first_universe + i);
        while (i < s->universe_count && r.universe_count < PSY_ART_POLL_REPLY_PORTS &&
               (((uint16_t)(s->first_universe + i - 1u)) & 0x7ff0u) ==
                   ((uint16_t)(first - 1u) & 0x7ff0u)) {
            r.universes[r.universe_count++] = (uint16_t)(s->first_universe + i);
            ++i;
        }
        r.bind_index = bind_index++;
        const size_t n = psy_build_art_poll_reply(&r, buffer, sizeof(buffer));
        if (n > 0) {
            sendto(s_art, buffer, n, 0, (struct sockaddr *)&to, sizeof(to));
        }
    }
}

static void net_task(void *arg) {
    (void)arg;
    static uint8_t buffer[1500];
    static char reply[1400];
    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(s_art, &ready);
        FD_SET(s_sacn, &ready);
        FD_SET(s_cfg, &ready);
        FD_SET(s_ddp, &ready);
        int top = s_art > s_sacn ? s_art : s_sacn;
        top = top > s_cfg ? top : s_cfg;
        top = top > s_ddp ? top : s_ddp;
        struct timeval wait = {.tv_sec = 0, .tv_usec = 100000};
        if (select(top + 1, &ready, NULL, NULL, &wait) <= 0) {
            continue;
        }
        psy_packet packet;
        if (FD_ISSET(s_sacn, &ready)) {
            const int n = recv(s_sacn, buffer, sizeof(buffer), 0);
            if (n > 0 && (s_config->protocols & NODE_PROTOCOL_SACN) &&
                psy_parse_sacn(buffer, (size_t)n, &packet)) {
                if (packet.kind == PSY_PACKET_DMX && packet.sync_universe != 0 &&
                    packet.sync_universe != s_sync) {
                    membership(packet.sync_universe, true);
                    s_sync = packet.sync_universe;
                }
                s_packets(&packet, false, net_now_ms());
            }
        }
        if (FD_ISSET(s_ddp, &ready)) { /* DDP is always on. */
            const int n = recv(s_ddp, buffer, sizeof(buffer), 0);
            if (n > 0 && psy_parse_ddp(buffer, (size_t)n, &packet)) {
                s_packets(&packet, false, net_now_ms());
            }
        }
        if (FD_ISSET(s_art, &ready)) {
            const int n = recv(s_art, buffer, sizeof(buffer), 0);
            if (n > 0 && psy_parse_art_net(buffer, (size_t)n, &packet)) {
                if (packet.kind == PSY_PACKET_POLL) {
                    ++s_polls;
                    send_poll_replies();
                } else if (s_config->protocols & NODE_PROTOCOL_ART_NET) {
                    s_packets(&packet, true, net_now_ms());
                }
            }
        }
        if (FD_ISSET(s_cfg, &ready)) {
            struct sockaddr_in from;
            socklen_t from_size = sizeof(from);
            const int n = recvfrom(s_cfg, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&from,
                                   &from_size);
            if (n > 0) {
                s_commands((const char *)buffer, (size_t)n, reply, sizeof(reply));
                sendto(s_cfg, reply, strlen(reply), 0, (struct sockaddr *)&from, from_size);
            }
        }
    }
}

void net_start(const node_config *config, const uint8_t mac[6], net_packet_handler packets,
               net_command_handler commands) {
    s_config = config;
    memcpy(s_mac, mac, 6);
    s_packets = packets;
    s_commands = commands;
    s_art = listen_on(PSY_ARTNET_PORT);
    s_sacn = listen_on(PSY_SACN_PORT);
    s_cfg = listen_on(NODE_CONFIG_PORT);
    s_ddp = listen_on(PSY_DDP_PORT);
    if (s_art < 0 || s_sacn < 0 || s_cfg < 0 || s_ddp < 0) {
        ESP_LOGE(TAG, "could not open the UDP ports");
        return;
    }
    net_set_universes(config->node.first_universe, config->node.universe_count);
    xTaskCreatePinnedToCore(net_task, "net", NET_STACK, NULL, 5, NULL, NET_CORE);
}
