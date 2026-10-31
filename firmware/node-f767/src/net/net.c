#include "net.h"

#include "ethernetif.h"
#include "lwip/dhcp.h"
#include "lwip/igmp.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"
#include "stm32f7xx_hal.h"

#include <stdio.h>
#include <string.h>

#define LINK_POLL_MS 100u
#define DHCP_FALLBACK_MS 10000u

static struct netif s_netif;
static node_config *s_config;
static uint8_t s_mac[6];
static net_packet_handler s_handler;
static struct udp_pcb *s_art;
static struct udp_pcb *s_sacn;
static struct udp_pcb *s_cfg;
static uint32_t s_last_link_poll;
static uint32_t s_dhcp_started;
static bool s_dhcp_active;
static bool s_link_was_up;
static bool s_save;
static bool s_reboot;
static bool s_apply;
static uint16_t s_sync_joined;
static uint16_t s_joined_first; /* The data universes whose groups are joined. */
static uint16_t s_joined_count;
static uint8_t s_packet[1500]; /* For the rare chained pbuf (a full DDP packet: 1450). */

u32_t sys_now(void) { /* lwIP's clock for its timeouts. */ return HAL_GetTick(); }

static const uint8_t *contiguous(struct pbuf *p, uint16_t *length) {
    *length = p->tot_len;
    if (p->len == p->tot_len) {
        return (const uint8_t *)p->payload; /* The usual case: one receive buffer. */
    }
    if (p->tot_len > sizeof(s_packet)) {
        *length = 0;
        return NULL;
    }
    pbuf_copy_partial(p, s_packet, p->tot_len, 0);
    return s_packet;
}

static void join_group(uint16_t universe) {
    uint8_t g[4];
    psy_sacn_group(universe, g);
    ip4_addr_t group;
    IP4_ADDR(&group, g[0], g[1], g[2], g[3]);
    igmp_joingroup(IP4_ADDR_ANY4, &group);
}

static void leave_group(uint16_t universe) {
    uint8_t g[4];
    psy_sacn_group(universe, g);
    ip4_addr_t group;
    IP4_ADDR(&group, g[0], g[1], g[2], g[3]);
    igmp_leavegroup(IP4_ADDR_ANY4, &group);
}

void net_set_universes(uint16_t first, uint16_t count) {
    for (uint16_t u = 0; u < s_joined_count; ++u) {
        const uint16_t universe = (uint16_t)(s_joined_first + u);
        if (universe != s_sync_joined) { /* The sync group stays. */
            leave_group(universe);
        }
    }
    for (uint16_t u = 0; u < count; ++u) {
        join_group((uint16_t)(first + u));
    }
    s_joined_first = first;
    s_joined_count = count;
}

void net_join_sync_universe(uint16_t universe) {
    if (universe != 0 && universe != s_sync_joined) {
        join_group(universe);
        s_sync_joined = universe;
    }
}

static uint32_t s_polls;

uint32_t net_polls(void) { return s_polls; }

static void send_poll_replies(void) {
    /* One reply per up to four universes sharing a Sub-Net, numbered by bind index. Sent to the
     * subnet's broadcast address (as Art-Net 3 nodes did), not to the poller: a unicast reply waits
     * for ARP to resolve the controller, and on a direct cable that took seconds, past the app's
     * discovery window. The controller listens on port 6454 for any reply. */
    ip_addr_t to;
    ip_addr_set_ip4_u32(&to, ip4_addr_get_u32(netif_ip4_addr(&s_netif)) |
                                 ~ip4_addr_get_u32(netif_ip4_netmask(&s_netif)));
    const node_settings *s = &s_config->node;
    uint8_t buffer[PSY_ART_POLL_REPLY_BYTES];
    psy_poll_reply r;
    memset(&r, 0, sizeof(r));
    net_address(r.ip);
    memcpy(r.mac, s_mac, 6);
    r.short_name = s_config->short_name;
    r.long_name = s_config->long_name;
    r.report = "#0001 [0000] Psyight node running";
    uint8_t bind = 1;
    for (uint16_t i = 0; i < s->universe_count;) {
        r.universe_count = 0;
        const uint16_t first = (uint16_t)(s->first_universe + i);
        while (i < s->universe_count && r.universe_count < PSY_ART_POLL_REPLY_PORTS &&
               (((uint16_t)(s->first_universe + i - 1u)) & 0x7ff0u) ==
                   ((uint16_t)(first - 1u) & 0x7ff0u)) {
            r.universes[r.universe_count++] = (uint16_t)(s->first_universe + i);
            ++i;
        }
        r.bind_index = bind++;
        const size_t n = psy_build_art_poll_reply(&r, buffer, sizeof(buffer));
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
        if (p != NULL) {
            memcpy(p->payload, buffer, n);
            udp_sendto(s_art, p, &to, PSY_ARTNET_PORT);
            pbuf_free(p);
        }
    }
}

static void on_art_net(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *from,
                       u16_t port) {
    (void)arg;
    (void)pcb;
    (void)port;
    uint16_t n = 0;
    const uint8_t *data = contiguous(p, &n);
    psy_packet packet;
    if (data != NULL && psy_parse_art_net(data, n, &packet)) {
        if (packet.kind == PSY_PACKET_POLL) {
            ++s_polls;
            send_poll_replies();
        } else if (s_config->protocols & NODE_PROTOCOL_ART_NET) {
            s_handler(&packet, true, HAL_GetTick());
        }
    }
    pbuf_free(p);
}

static void on_sacn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *from,
                    u16_t port) {
    (void)arg;
    (void)pcb;
    (void)from;
    (void)port;
    uint16_t n = 0;
    const uint8_t *data = contiguous(p, &n);
    psy_packet packet;
    if (data != NULL && (s_config->protocols & NODE_PROTOCOL_SACN) &&
        psy_parse_sacn(data, n, &packet)) {
        if (packet.kind == PSY_PACKET_DMX) {
            net_join_sync_universe(packet.sync_universe);
        }
        s_handler(&packet, false, HAL_GetTick());
    }
    pbuf_free(p);
}

static void on_ddp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *from,
                   u16_t port) {
    (void)arg;
    (void)pcb;
    (void)from;
    (void)port;
    uint16_t n = 0;
    const uint8_t *data = contiguous(p, &n);
    psy_packet packet;
    if (data != NULL && psy_parse_ddp(data, n, &packet)) { /* DDP is always on. */
        s_handler(&packet, false, HAL_GetTick());
    }
    pbuf_free(p);
}

static void on_config(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *from,
                      u16_t port) {
    (void)arg;
    static char reply[1400];
    uint16_t n = 0;
    const uint8_t *data = contiguous(p, &n);
    if (data != NULL) {
        const node_command_action action =
            node_config_command(s_config, s_mac, (const char *)data, n, reply, sizeof(reply));
        s_save |= action == NODE_CMD_SAVE;
        s_reboot |= action == NODE_CMD_REBOOT;
        s_apply |= action == NODE_CMD_APPLY;
        const size_t length = strlen(reply);
        struct pbuf *out = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
        if (out != NULL) {
            memcpy(out->payload, reply, length);
            udp_sendto(pcb, out, from, port);
            pbuf_free(out);
        }
    }
    pbuf_free(p);
}

static void on_status(struct netif *netif) {
    const ip4_addr_t *a = netif_ip4_addr(netif);
    printf("net: address %u.%u.%u.%u%s\r\n", ip4_addr1(a), ip4_addr2(a), ip4_addr3(a), ip4_addr4(a),
           s_dhcp_active ? " (DHCP)" : "");
}

static struct udp_pcb *listen_on(uint16_t port, udp_recv_fn fn) {
    struct udp_pcb *pcb = udp_new();
    if (pcb != NULL) {
        udp_bind(pcb, IP_ADDR_ANY, port);
        udp_recv(pcb, fn, NULL);
    }
    return pcb;
}

void net_init(node_config *config, const uint8_t mac[6], net_packet_handler handler) {
    s_config = config;
    s_handler = handler;
    memcpy(s_mac, mac, 6);
    lwip_init();
    ip4_addr_t ip, mask, gw;
    if (config->dhcp) {
        ip4_addr_set_zero(&ip);
        ip4_addr_set_zero(&mask);
        ip4_addr_set_zero(&gw);
    } else {
        IP4_ADDR(&ip, config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
        IP4_ADDR(&mask, config->mask[0], config->mask[1], config->mask[2], config->mask[3]);
        IP4_ADDR(&gw, config->gateway[0], config->gateway[1], config->gateway[2],
                 config->gateway[3]);
    }
    memcpy(s_netif.hwaddr, mac, 6);
    if (netif_add(&s_netif, &ip, &mask, &gw, NULL, ethernetif_init, ethernet_input) == NULL) {
        printf("net: the MAC or PHY did not start\r\n");
        return;
    }
    netif_set_default(&s_netif);
    netif_set_status_callback(&s_netif, on_status);
    if (!netif_is_link_up(&s_netif)) {
        printf("net: no link yet\r\n");
    }
    if (config->dhcp) {
        netif_set_up(&s_netif); /* DHCP only runs on an interface that is up. */
        dhcp_start(&s_netif);
        s_dhcp_active = true;
        s_dhcp_started = HAL_GetTick();
    } else {
        netif_set_up(&s_netif);
    }
    s_art = listen_on(PSY_ARTNET_PORT, on_art_net);
    s_sacn = listen_on(PSY_SACN_PORT, on_sacn);
    (void)listen_on(PSY_DDP_PORT, on_ddp);
    s_cfg = listen_on(NODE_CONFIG_PORT, on_config);
    net_set_universes(config->node.first_universe, config->node.universe_count);
}

void net_poll(uint32_t now_ms) {
    if (s_netif.input == NULL) {
        return; /* net_init failed. */
    }
    ethernetif_input(&s_netif);
    sys_check_timeouts();
    if (now_ms - s_last_link_poll >= LINK_POLL_MS) {
        s_last_link_poll = now_ms;
        ethernetif_link_poll(&s_netif);
    }
    const bool link_up = netif_is_link_up(&s_netif);
    if (!link_up) {
        s_dhcp_started = now_ms; /* The DHCP wait counts from the link coming up. */
    } else if (!s_link_was_up && s_config->dhcp && !s_dhcp_active) {
        /* A cable plugged in after the link-local fallback (say, moved from the PC to a router):
         * ask for a lease again; with none in DHCP_FALLBACK_MS, the link-local address again. */
        ip4_addr_t zero;
        ip4_addr_set_zero(&zero);
        netif_set_addr(&s_netif, &zero, &zero, &zero);
        dhcp_start(&s_netif);
        s_dhcp_active = true;
        s_dhcp_started = now_ms;
        printf("net: link up, asking for a DHCP lease again\r\n");
    }
    s_link_was_up = link_up;
    if (s_dhcp_active && !dhcp_supplied_address(&s_netif) &&
        now_ms - s_dhcp_started > DHCP_FALLBACK_MS) {
        /* No DHCP server (a direct cable): a link-local address from the MAC, the range the PC
         * takes on the same cable, so discovery and output reach the node with no settings. */
        dhcp_stop(&s_netif);
        s_dhcp_active = false;
        uint8_t a[4];
        node_link_local_address(s_mac, a);
        ip4_addr_t ip, mask, gw;
        IP4_ADDR(&ip, a[0], a[1], a[2], a[3]);
        IP4_ADDR(&mask, 255, 255, 0, 0);
        ip4_addr_set_zero(&gw);
        netif_set_addr(&s_netif, &ip, &mask, &gw);
        netif_set_up(&s_netif);
        printf("net: no DHCP lease in %u s, using the link-local address\r\n",
               (unsigned)(DHCP_FALLBACK_MS / 1000u));
    }
}

bool net_link_up(void) { return netif_is_link_up(&s_netif); }

void net_address(uint8_t ip[4]) {
    const ip4_addr_t *a = netif_ip4_addr(&s_netif);
    ip[0] = ip4_addr1(a);
    ip[1] = ip4_addr2(a);
    ip[2] = ip4_addr3(a);
    ip[3] = ip4_addr4(a);
}

bool net_take_save_request(void) {
    const bool r = s_save;
    s_save = false;
    return r;
}

bool net_take_reboot_request(void) {
    const bool r = s_reboot;
    s_reboot = false;
    return r;
}

bool net_take_apply_request(void) {
    const bool r = s_apply;
    s_apply = false;
    return r;
}
