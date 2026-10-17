/* Psyight pixel node on the Nucleo-F767ZI (SPEC section 12). NODE_STAGE selects the bring-up
 * step (SPEC 12.4):
 *   1  F1: a test pattern on the strips, no network. Check the waveform on a scope.
 *   2  F2: network only. Prints each universe's packets, sequence and size once a second.
 *   3  F3: the node: Art-Net and sACN to the strips, sync, watchdog, flash config, signal-loss
 *      policy and fallback show, ArtPollReply, the UDP config protocol on port 6455.
 * Status LEDs: green blinks at 1 Hz (alive); blue on while data arrives; red on without a link
 * (stages 2 and 3) or after a fault. */
#include "board.h"
#include "config_store.h"
#include "net.h"
#include "node_core.h"
#include "ws2812.h"

#include <stdio.h>
#include <string.h>

#ifndef NODE_STAGE
#define NODE_STAGE 3
#endif

#define MIN_FRAME_MS 16u /* Animated output (fallback, fade) at about 60 fps. */
#define WATCHDOG_MS 4000u
#define STATS_MS 5000u

static node_config s_config;
static uint8_t s_mac[6];
#if NODE_STAGE != 2
static uint8_t s_lanes[NODE_LANES][NODE_LANE_BYTES];
#endif
#if NODE_STAGE == 3
static node_state s_node;
#endif

static void heartbeat(uint32_t now) {
    static uint32_t last;
    if (now - last >= 500u) {
        last = now;
        board_led_toggle(LED_GREEN);
    }
}

#if NODE_STAGE == 1

/* A different colour per lane, chasing along it, about 10% brightness. */
static void stage1_loop(void) {
    const node_settings *s = &s_config.node;
    const uint16_t pixels = node_max_lane_pixels(s);
    uint32_t last_print = 0;
    uint32_t sent_since = 0;
    uint32_t frame = 0;
    for (;;) {
        const uint32_t now = HAL_GetTick();
        heartbeat(now);
        if (ws2812_ready()) {
            for (int l = 0; l < NODE_LANES; ++l) {
                memset(s_lanes[l], 0, NODE_LANE_BYTES);
                for (uint16_t i = 0; i < s->lanes[l].pixels; ++i) {
                    const uint32_t lit = (i + frame) % 30u < 3u;
                    s_lanes[l][3 * i + 0] = (uint8_t)((l & 1) ? 25 * lit : 0);           /* G */
                    s_lanes[l][3 * i + 1] = (uint8_t)((l & 2) ? 25 * lit : 0);           /* R */
                    s_lanes[l][3 * i + 2] = (uint8_t)((l & 4 || l == 0) ? 25 * lit : 0); /* B */
                }
            }
            if (ws2812_send(s_lanes, pixels)) {
                ++frame;
                ++sent_since;
            }
        }
        if (now - last_print >= 1000u) {
            printf("F1: %lu frames/s, %u pixels per lane, transpose %lu us\r\n",
                   (unsigned long)sent_since, pixels, (unsigned long)ws2812_last_transpose_us());
            sent_since = 0;
            last_print = now;
        }
    }
}

#elif NODE_STAGE == 2

typedef struct {
    uint16_t universe;
    uint32_t packets;
    uint8_t last_sequence;
    uint16_t slots;
    bool art_net;
} universe_stats;

static universe_stats s_seen[16];
static uint32_t s_syncs;

static void stage2_packet(const psy_packet *p, bool art_net, uint32_t now_ms) {
    (void)now_ms;
    if (p->kind == PSY_PACKET_SYNC) {
        ++s_syncs;
        return;
    }
    for (int i = 0; i < 16; ++i) {
        if (s_seen[i].packets == 0 || s_seen[i].universe == p->universe) {
            s_seen[i].universe = p->universe;
            ++s_seen[i].packets;
            s_seen[i].last_sequence = p->sequence;
            s_seen[i].slots = p->slot_count;
            s_seen[i].art_net = art_net;
            return;
        }
    }
}

static void stage2_loop(void) {
    uint32_t last_print = 0;
    for (;;) {
        const uint32_t now = HAL_GetTick();
        heartbeat(now);
        net_poll(now);
        board_led(LED_RED, !net_link_up());
        if (now - last_print >= 1000u) {
            last_print = now;
            uint8_t ip[4];
            net_address(ip);
            printf("F2: %u.%u.%u.%u, %lu syncs/s\r\n", ip[0], ip[1], ip[2], ip[3],
                   (unsigned long)s_syncs);
            s_syncs = 0;
            for (int i = 0; i < 16 && s_seen[i].packets > 0; ++i) {
                printf("  universe %u (%s): %lu packets/s, last sequence %u, %u slots\r\n",
                       s_seen[i].universe, s_seen[i].art_net ? "Art-Net" : "sACN",
                       (unsigned long)s_seen[i].packets, s_seen[i].last_sequence, s_seen[i].slots);
                s_seen[i].packets = 0;
            }
        }
    }
}

#else

static void stage3_packet(const psy_packet *p, bool art_net, uint32_t now_ms) {
    node_on_packet(&s_node, p, art_net, now_ms);
}

static void stage3_loop(void) {
    const uint16_t pixels = node_max_lane_pixels(&s_node.s);
    uint32_t last_frame = 0;
    uint32_t last_stats = 0;
    uint32_t frames_since = 0;
    for (;;) {
        const uint32_t now = HAL_GetTick();
        board_watchdog_kick();
        heartbeat(now);
        net_poll(now);
        board_led(LED_RED, !net_link_up());
        const bool lost = node_signal_lost(&s_node, now);
        board_led(LED_BLUE, !lost);
        /* Live data goes out as soon as it latches; animated loss output is paced. */
        if (ws2812_ready() && (!lost || now - last_frame >= MIN_FRAME_MS) &&
            node_frame(&s_node, now, s_lanes) && ws2812_send(s_lanes, pixels)) {
            last_frame = now;
            ++frames_since;
        }
        if (net_take_save_request()) {
            printf("config: saving (output pauses)\r\n");
            printf(config_save(&s_config) ? "config: saved\r\n" : "config: save failed\r\n");
        }
        if (net_take_reboot_request()) {
            HAL_Delay(50); /* Let the reply leave. */
            NVIC_SystemReset();
        }
        if (now - last_stats >= STATS_MS) {
            uint8_t ip[4];
            net_address(ip);
            printf("node %u.%u.%u.%u: %lu fps out, %lu packets, %lu latches, %lu sequence gaps, "
                   "%lu ignored%s\r\n",
                   ip[0], ip[1], ip[2], ip[3],
                   (unsigned long)(frames_since * 1000u / (now - last_stats)),
                   (unsigned long)s_node.packets, (unsigned long)s_node.latches,
                   (unsigned long)s_node.sequence_gaps, (unsigned long)s_node.ignored,
                   lost ? ", signal lost" : "");
            frames_since = 0;
            last_stats = now;
        }
    }
}

#endif

int main(void) {
    board_init();
    board_mac_address(s_mac);
    const bool stored = config_load(&s_config, s_mac);
    printf("\r\nPsyight node, stage F%d, MAC %02x:%02x:%02x:%02x:%02x:%02x, %s config%s\r\n",
           NODE_STAGE, s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
           stored ? "stored" : "default",
           board_reset_by_watchdog() ? ", after a watchdog reset" : "");
#if NODE_STAGE == 1
    ws2812_init(s_config.active_mask);
    stage1_loop();
#elif NODE_STAGE == 2
    net_init(&s_config, s_mac, stage2_packet);
    stage2_loop();
#else
    board_watchdog_start(WATCHDOG_MS);
    node_init(&s_node, &s_config.node);
    ws2812_init(s_config.active_mask);
    net_init(&s_config, s_mac, stage3_packet);
    stage3_loop();
#endif
    return 0;
}
