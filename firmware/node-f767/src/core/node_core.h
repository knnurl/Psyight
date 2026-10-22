/* The node's hardware-free logic (SPEC section 12.2): universe store with sync latching, the
 * mapping from received universes to output lanes, signal-loss behaviour with the node-side
 * fallback show, and the bit transpose that feeds the parallel WS2812 output.
 *
 * Portable C99, no HAL: the app's unit tests compile it. Times are milliseconds from any
 * monotonic counter (HAL_GetTick on the board).
 *
 * Latching: data goes to a pending buffer per universe. sACN data naming a sync universe, and
 * all Art-Net data once an ArtSync has been seen, waits for the sync packet, which latches every
 * pending universe at once (no tearing across lanes). Art-Net leaves sync mode after 4 s without
 * an ArtSync (Art-Net 4); sACN data held 1 s without its sync is latched anyway.
 *
 * Lanes: the app patches pixels in rig order, pixels_per_universe to a universe, never split.
 * A lane takes `pixels` pixels starting at global pixel `first_pixel`, counted from the first
 * universe, so a strip may run across a universe boundary.
 */
#ifndef PSY_NODE_CORE_H
#define PSY_NODE_CORE_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODE_LANES 8
#define NODE_MAX_UNIVERSES 24
#define NODE_MAX_PIXELS_PER_LANE 680 /* 4 universes; ~48 fps at 800 kHz (SPEC 12.3). */
#define NODE_LANE_BYTES (NODE_MAX_PIXELS_PER_LANE * 3)
#define NODE_ART_SYNC_TIMEOUT_MS 4000u
#define NODE_SACN_SYNC_WAIT_MS 1000u

typedef enum {
    NODE_LOSS_HOLD,
    NODE_LOSS_FADE,
    NODE_LOSS_FALLBACK,
    NODE_LOSS_BLACKOUT
} node_loss_policy;

typedef struct {
    uint16_t first_pixel;
    uint16_t pixels;
} node_lane;

typedef struct {
    uint16_t first_universe;
    uint8_t universe_count;
    uint8_t pixels_per_universe; /* 170 for RGB. */
    node_lane lanes[NODE_LANES];
    uint32_t loss_timeout_ms;
    node_loss_policy loss_policy;
    uint32_t fade_ms;
    /* Fallback show: a slow plasma drifting through the palette, dim by default. */
    float fallback_brightness; /* 0 to 1, before gamma. */
    float fallback_speed_hz;
    uint8_t palette[4][3]; /* RGB; the fallback show writes it in each lane's order. */
    uint8_t palette_size;
    /* Each lane's wire colour order, as the app's ColourOrder: 0 rgb, 1 grb, 2 brg, 3 rbg, 4 gbr,
     * 5 bgr. Live data arrives already in it (the app orders the bytes); the node uses it only
     * for its own fallback show. */
    uint8_t lane_order[NODE_LANES];
} node_settings;

#define NODE_ORDER_COUNT 6
#define NODE_ORDER_GRB 1

/* Where R, G and B go within a pixel's three bytes for a colour order (as the app's patch). */
void node_order_positions(uint8_t order, uint8_t positions[3]);

node_settings node_default_settings(void);

typedef struct {
    node_settings s;
    uint8_t received[NODE_MAX_UNIVERSES][PSY_MAX_SLOTS];
    uint8_t latched[NODE_MAX_UNIVERSES][PSY_MAX_SLOTS];
    bool pending[NODE_MAX_UNIVERSES]; /* Data waiting for its sync. */
    uint32_t pending_since_ms[NODE_MAX_UNIVERSES];
    int16_t last_sequence[NODE_MAX_UNIVERSES]; /* -1 before the first packet. */
    uint16_t sacn_sync_universe;
    bool art_sync_mode;
    uint32_t art_sync_ms;
    bool have_data;
    uint32_t last_data_ms;
    bool latch_dirty; /* The latched data changed since the last frame. */
    bool lost;        /* In signal loss. */
    bool loss_shown;  /* Hold or blackout already output once. */
    uint32_t lost_since_ms;
    /* Statistics. */
    uint32_t packets;
    uint32_t latches;
    uint32_t sequence_gaps;
    uint32_t ignored; /* Universes outside the node's range. */
} node_state;

void node_init(node_state *n, const node_settings *settings);
void node_on_packet(node_state *n, const psy_packet *packet, bool art_net, uint32_t now_ms);

/* Fills the lanes' wire bytes (as received; the fallback show in each lane's order) for a frame
 * at now_ms. Returns false when
 * nothing changed since the last frame (no need to send). */
bool node_frame(node_state *n, uint32_t now_ms, uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]);

bool node_signal_lost(const node_state *n, uint32_t now_ms);

/* Parallel output words for GPIO BSRR, 24 per pixel (bit 7 of G first). Word = lanes whose bit
 * is 0, shifted into BSRR's reset half (bits 16 + lane): the DMA writes it 0.4 us into each bit
 * to end the short pulses of the 0 bits. Lanes outside active_mask stay low throughout. */
void node_transpose(const uint8_t lanes[NODE_LANES][NODE_LANE_BYTES], uint16_t pixels,
                    uint8_t active_mask, uint32_t *words);

/* The longest lane in pixels. */
uint16_t node_max_lane_pixels(const node_settings *s);

#ifdef __cplusplus
}
#endif

#endif
