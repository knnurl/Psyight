#include "node_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

node_settings node_default_settings(void) {
    node_settings s;
    memset(&s, 0, sizeof(s));
    s.first_universe = 1;
    s.universe_count = 3;
    s.pixels_per_universe = 170;
    /* The app's default 2.5 by 1.2 m test frame: bottom 150, right 72, top 150, left 72. */
    const uint16_t first[4] = {0, 150, 222, 372};
    const uint16_t count[4] = {150, 72, 150, 72};
    for (int l = 0; l < 4; ++l) {
        s.lanes[l].first_pixel = first[l];
        s.lanes[l].pixels = count[l];
    }
    s.loss_timeout_ms = 2000;
    s.loss_policy = NODE_LOSS_FALLBACK;
    s.fade_ms = 3000;
    s.fallback_brightness = 0.25f;
    s.fallback_speed_hz = 0.05f;
    /* Violet and magenta, UV-friendly (SPEC 7.5), as RGB. */
    const uint8_t palette[4][3] = {{90, 0, 255}, {200, 0, 120}, {120, 20, 255}, {255, 0, 60}};
    memcpy(s.palette, palette, sizeof(palette));
    s.palette_size = 4;
    for (int l = 0; l < NODE_LANES; ++l) {
        s.lane_order[l] = NODE_ORDER_GRB; /* WS2812-class strips. */
    }
    return s;
}

void node_order_positions(uint8_t order, uint8_t positions[3]) {
    static const uint8_t kPositions[NODE_ORDER_COUNT][3] = {
        {0, 1, 2}, /* rgb */
        {1, 0, 2}, /* grb */
        {1, 2, 0}, /* brg */
        {0, 2, 1}, /* rbg */
        {2, 0, 1}, /* gbr */
        {2, 1, 0}, /* bgr */
    };
    const uint8_t *p = kPositions[order < NODE_ORDER_COUNT ? order : NODE_ORDER_GRB];
    positions[0] = p[0];
    positions[1] = p[1];
    positions[2] = p[2];
}

void node_init(node_state *n, const node_settings *settings) {
    memset(n, 0, sizeof(*n));
    n->s = *settings;
    if (n->s.universe_count > NODE_MAX_UNIVERSES) {
        n->s.universe_count = NODE_MAX_UNIVERSES;
    }
    if (n->s.pixels_per_universe == 0 || n->s.pixels_per_universe > PSY_MAX_SLOTS / 3) {
        n->s.pixels_per_universe = 170;
    }
    for (int i = 0; i < NODE_LANES; ++i) {
        if (n->s.lanes[i].pixels > NODE_MAX_PIXELS_PER_LANE) {
            n->s.lanes[i].pixels = NODE_MAX_PIXELS_PER_LANE;
        }
    }
    for (int u = 0; u < NODE_MAX_UNIVERSES; ++u) {
        n->last_sequence[u] = -1;
    }
}

static void latch(node_state *n, int u) {
    memcpy(n->latched[u], n->received[u], PSY_MAX_SLOTS);
    n->pending[u] = false;
    n->latch_dirty = true;
}

static void latch_all(node_state *n) {
    bool any = false;
    for (int u = 0; u < n->s.universe_count; ++u) {
        if (n->pending[u]) {
            latch(n, u);
            any = true;
        }
    }
    n->latches += any ? 1u : 0u;
}

void node_on_packet(node_state *n, const psy_packet *p, bool art_net, uint32_t now_ms) {
    ++n->packets;
    if (p->kind == PSY_PACKET_POLL) {
        return; /* Answered by the network layer. */
    }
    if (p->kind == PSY_PACKET_SYNC) {
        if (art_net) {
            n->art_sync_mode = true;
            n->art_sync_ms = now_ms;
            latch_all(n);
        } else if (p->sync_universe != 0 && p->sync_universe == n->sacn_sync_universe) {
            latch_all(n);
        }
        return;
    }
    const int u = (int)p->universe - (int)n->s.first_universe;
    if (u < 0 || u >= n->s.universe_count) {
        ++n->ignored;
        return;
    }
    /* Sequence: +1 per frame, wrapping; Art-Net skips 0, which means "unsequenced". */
    if (!(art_net && p->sequence == 0)) {
        int expected = (n->last_sequence[u] + 1) & 0xff;
        if (art_net && expected == 0) {
            expected = 1;
        }
        if (n->last_sequence[u] >= 0 && p->sequence != expected) {
            ++n->sequence_gaps;
        }
        n->last_sequence[u] = p->sequence;
    }
    uint16_t count = p->slot_count > PSY_MAX_SLOTS ? PSY_MAX_SLOTS : p->slot_count;
    memcpy(n->received[u], p->slots, count);
    n->have_data = true;
    n->last_data_ms = now_ms;
    if (!art_net) {
        n->sacn_sync_universe = p->sync_universe;
    }
    const bool held = art_net ? n->art_sync_mode : p->sync_universe != 0;
    if (held) {
        if (!n->pending[u]) {
            n->pending[u] = true;
            n->pending_since_ms[u] = now_ms;
        }
    } else {
        latch(n, u);
        ++n->latches;
    }
}

bool node_signal_lost(const node_state *n, uint32_t now_ms) {
    const uint32_t since = n->have_data ? n->last_data_ms : 0u;
    return now_ms - since > n->s.loss_timeout_ms;
}

static void fill_from_latched(const node_state *n, float scale,
                              uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]) {
    const uint32_t ppu = n->s.pixels_per_universe;
    for (int l = 0; l < NODE_LANES; ++l) {
        uint8_t *out = lanes[l];
        memset(out, 0, NODE_LANE_BYTES);
        for (uint32_t i = 0; i < n->s.lanes[l].pixels; ++i) {
            const uint32_t g = n->s.lanes[l].first_pixel + i;
            const uint32_t u = g / ppu;
            if (u >= n->s.universe_count) {
                break;
            }
            const uint8_t *src = &n->latched[u][(g % ppu) * 3u];
            for (int c = 0; c < 3; ++c) {
                out[i * 3u + (uint32_t)c] =
                    (uint8_t)(scale >= 1.0f ? src[c] : (uint8_t)((float)src[c] * scale + 0.5f));
            }
        }
    }
}

/* Slow plasma along each lane (60 pixels per metre assumed: the node knows no geometry),
 * drifting through the palette. Dim, smooth, no flashes. */
static void fill_fallback(const node_state *n, uint32_t now_ms,
                          uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]) {
    const node_settings *s = &n->s;
    const float t = (float)now_ms * 0.001f * s->fallback_speed_hz;
    const int colours = s->palette_size > 0 ? s->palette_size : 1;
    for (int l = 0; l < NODE_LANES; ++l) {
        uint8_t *out = lanes[l];
        uint8_t at[3];
        node_order_positions(s->lane_order[l], at);
        memset(out, 0, NODE_LANE_BYTES);
        for (uint32_t i = 0; i < s->lanes[l].pixels; ++i) {
            const float x = (float)i / 60.0f;
            const float a = sinf(2.0f * PI_F * (x / 1.4f + t));
            const float b = sinf(2.0f * PI_F * (x / 0.9f - 1.7f * t) + (float)l);
            const float v = 0.5f + 0.25f * (a + b); /* 0 to 1. */
            float position = (0.5f * v + 0.1f * t) * (float)colours;
            position -= floorf(position / (float)colours) * (float)colours;
            const int c0 = (int)position % colours;
            const int c1 = (c0 + 1) % colours;
            const float f = position - floorf(position);
            const float level = s->fallback_brightness * (0.4f + 0.6f * v);
            const float drive = powf(level, 2.2f); /* Perceptual level to LED drive. */
            for (int c = 0; c < 3; ++c) { /* R, G, B, each to its place in the lane's order. */
                const float colour =
                    (1.0f - f) * (float)s->palette[c0][c] + f * (float)s->palette[c1][c];
                out[i * 3u + at[c]] = (uint8_t)(colour * drive + 0.5f);
            }
        }
    }
}

bool node_frame(node_state *n, uint32_t now_ms, uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]) {
    if (n->art_sync_mode && now_ms - n->art_sync_ms > NODE_ART_SYNC_TIMEOUT_MS) {
        n->art_sync_mode = false; /* Art-Net 4: back to latching on data. */
        latch_all(n);
    }
    for (int u = 0; u < n->s.universe_count; ++u) {
        if (n->pending[u] && now_ms - n->pending_since_ms[u] > NODE_SACN_SYNC_WAIT_MS &&
            !n->art_sync_mode) {
            latch(n, u); /* Its sync never came. */
            ++n->latches;
        }
    }
    if (node_signal_lost(n, now_ms)) {
        if (!n->lost) {
            n->lost = true;
            n->lost_since_ms = now_ms;
            n->loss_shown = false;
        }
        switch (n->s.loss_policy) {
        case NODE_LOSS_HOLD:
            if (n->loss_shown) {
                return false;
            }
            fill_from_latched(n, 1.0f, lanes);
            n->loss_shown = true;
            return true;
        case NODE_LOSS_BLACKOUT:
            if (n->loss_shown) {
                return false;
            }
            fill_from_latched(n, 0.0f, lanes);
            n->loss_shown = true;
            return true;
        case NODE_LOSS_FADE: {
            const float gone =
                n->s.fade_ms == 0 ? 1.0f : (float)(now_ms - n->lost_since_ms) / (float)n->s.fade_ms;
            if (gone >= 1.0f) {
                if (n->loss_shown) {
                    return false;
                }
                n->loss_shown = true;
            }
            fill_from_latched(n, gone >= 1.0f ? 0.0f : 1.0f - gone, lanes);
            return true;
        }
        case NODE_LOSS_FALLBACK:
        default:
            fill_fallback(n, now_ms, lanes);
            return true;
        }
    }
    if (n->lost) {
        n->lost = false;
        n->latch_dirty = true; /* Back from loss: show the live data again. */
    }
    if (!n->latch_dirty) {
        return false;
    }
    fill_from_latched(n, 1.0f, lanes);
    n->latch_dirty = false;
    return true;
}

/* 8 x 8 bit transpose (Hacker's Delight, 7-3): byte r of x (r = 0 the most significant) is row
 * r; afterwards byte i holds column i, its bit (7 - r) taken from row r's bit (7 - i). */
static uint64_t transpose8(uint64_t x) {
    x = (x & 0xAA55AA55AA55AA55ull) | ((x & 0x00AA00AA00AA00AAull) << 7) |
        ((x >> 7) & 0x00AA00AA00AA00AAull);
    x = (x & 0xCCCC3333CCCC3333ull) | ((x & 0x0000CCCC0000CCCCull) << 14) |
        ((x >> 14) & 0x0000CCCC0000CCCCull);
    x = (x & 0xF0F0F0F00F0F0F0Full) | ((x & 0x00000000F0F0F0F0ull) << 28) |
        ((x >> 28) & 0x00000000F0F0F0F0ull);
    return x;
}

void node_transpose(const uint8_t lanes[NODE_LANES][NODE_LANE_BYTES], uint16_t pixels,
                    uint8_t active_mask, uint32_t *words) {
    for (uint32_t byte = 0; byte < (uint32_t)pixels * 3u; ++byte) {
        /* Row r = lane 7 - r, so that bit k of each column byte is lane k. */
        uint64_t x = 0;
        for (int r = 0; r < 8; ++r) {
            x = (x << 8) | lanes[7 - r][byte];
        }
        x = transpose8(x);
        for (int i = 0; i < 8; ++i) { /* Column i: bit 7 - i of every lane, MSB first. */
            const uint8_t ones = (uint8_t)(x >> (56 - 8 * i));
            *words++ = (uint32_t)((uint8_t)~ones & active_mask) << 16;
        }
    }
}

uint16_t node_max_lane_pixels(const node_settings *s) {
    uint16_t most = 0;
    for (int l = 0; l < NODE_LANES; ++l) {
        if (s->lanes[l].pixels > most) {
            most = s->lanes[l].pixels;
        }
    }
    return most;
}
