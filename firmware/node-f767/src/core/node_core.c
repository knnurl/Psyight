#include "node_core.h"

#include <math.h>
#include <string.h>

/* Milliseconds from `then` to `now`, 0 when `then` is later. The main loop reads the time once
 * and then polls the network, whose packets are stamped as they arrive, so a stamp can be a
 * millisecond past the loop's `now`; plain unsigned subtraction wrapped that round to 49 days,
 * calling the signal lost for a frame (the fallback flashed) and latching universes before their
 * sync. Wraps of the tick itself (49.7 days) still work: the difference is taken mod 2^32. */
static uint32_t elapsed_ms(uint32_t now, uint32_t then) {
    const uint32_t d = now - then;
    return d > 0x80000000u ? 0u : d;
}

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
    /* The aurora's green, teal, blue and violet (the app's engine/aurora), as RGB. */
    const uint8_t palette[4][3] = {{0, 255, 89}, {0, 204, 178}, {25, 102, 255}, {140, 0, 255}};
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
    n->fresh = true;
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
    if (n->lost && !n->fresh) {
        return true;
    }
    const uint32_t since = n->have_data ? n->last_data_ms : 0u;
    return elapsed_ms(now_ms, since) > n->s.loss_timeout_ms;
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

/* The fallback show: an aurora, as the app's Ambient step (app/src/engine/aurora.cpp, WLED's
 * Aurora). Along each lane (the node knows no strips) waves of the palette's colours drift
 * slowly, each fading in and out over 8 to 25 s, brightest at its centre and fading linearly to
 * its edges; overlaps add, scaled back to keep their hue. A lane is taken as a loop, so a wave
 * leaving its end comes back at its start. fallback_speed 0.05 is the aurora's own pace (twice
 * it, twice as fast). Stateless: each wave's numbers are hashed from (lane, slot, generation). */
#define AURORA_PIXELS_PER_WAVE 12
#define AURORA_MIN_WAVES 2
#define AURORA_MAX_WAVES 12
#define AURORA_MIN_LIFE_S 8.0
#define AURORA_MAX_LIFE_S 25.0
#define AURORA_MIN_WIDTH_SHARE (1.0f / 20.0f)
#define AURORA_MAX_WIDTH_SHARE (1.0f / 6.0f)
#define AURORA_MIN_WIDTH_PIXELS 2.0f
#define AURORA_MIN_SPEED_PX_PER_S 1.5f
#define AURORA_MAX_SPEED_PX_PER_S 4.5f
#define AURORA_MIN_ALPHA 0.6f
#define AURORA_PACE_HZ 0.05f /* fallback_speed at the aurora's own pace. */

typedef struct {
    float centre_px;
    float width_px;
    float alpha;  /* Now, with the fade in and out. */
    float rgb[3]; /* 0 to 1. */
} aurora_wave;

static uint32_t mix32(uint32_t x) { /* A 32-bit integer hash (lowbias32). */
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* 0 to 1, from the wave's identity and which of its numbers this is. */
static float aurora_unit(int lane, int slot, int64_t generation, int which) {
    uint32_t h = mix32(0x9e3779b9u ^ (uint32_t)lane);
    h = mix32(h ^ (uint32_t)slot * 0x85ebca6bu);
    h = mix32(h ^ (uint32_t)generation ^ (uint32_t)(generation >> 32) * 0xc2b2ae35u);
    h = mix32(h ^ (uint32_t)which * 0x27d4eb2fu);
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static float aurora_between(float lo, float hi, float u) { return lo + (hi - lo) * u; }

static void fill_fallback(const node_state *n, uint32_t now_ms,
                          uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]) {
    const node_settings *s = &n->s;
    /* Seconds at the aurora's pace; double, so hours of uptime keep their milliseconds. */
    const double t = (double)now_ms * 0.001 * (double)(s->fallback_speed_hz / AURORA_PACE_HZ);
    const int colours = s->palette_size > 0 ? (s->palette_size < 4 ? s->palette_size : 4) : 1;
    for (int l = 0; l < NODE_LANES; ++l) {
        uint8_t *out = lanes[l];
        uint8_t at[3];
        node_order_positions(s->lane_order[l], at);
        memset(out, 0, NODE_LANE_BYTES);
        const int count = s->lanes[l].pixels;
        if (count == 0) {
            continue;
        }
        int slots = count / AURORA_PIXELS_PER_WAVE;
        slots = slots < AURORA_MIN_WAVES   ? AURORA_MIN_WAVES
                : slots > AURORA_MAX_WAVES ? AURORA_MAX_WAVES
                                           : slots;
        aurora_wave waves[AURORA_MAX_WAVES];
        for (int k = 0; k < slots; ++k) {
            /* Each slot's waves follow one another, the slots out of step. */
            const double life_s = AURORA_MIN_LIFE_S + (AURORA_MAX_LIFE_S - AURORA_MIN_LIFE_S) *
                                                          (double)aurora_unit(l, k, -1, 0);
            const double shifted = t + life_s * (double)aurora_unit(l, k, -1, 1);
            const int64_t generation = (int64_t)floor(shifted / life_s);
            const float age = (float)(shifted / life_s - (double)generation); /* 0 to 1. */
            aurora_wave *w = &waves[k];
            const float speed = aurora_between(AURORA_MIN_SPEED_PX_PER_S, AURORA_MAX_SPEED_PX_PER_S,
                                               aurora_unit(l, k, generation, 0));
            const float direction = aurora_unit(l, k, generation, 1) < 0.5f ? -1.0f : 1.0f;
            w->centre_px = aurora_unit(l, k, generation, 2) * (float)count +
                           direction * speed * age * (float)life_s;
            w->width_px =
                (float)count * aurora_between(AURORA_MIN_WIDTH_SHARE, AURORA_MAX_WIDTH_SHARE,
                                              aurora_unit(l, k, generation, 3));
            if (w->width_px < AURORA_MIN_WIDTH_PIXELS) {
                w->width_px = AURORA_MIN_WIDTH_PIXELS;
            }
            /* Up over the first half of its life, down over the second. */
            w->alpha = aurora_between(AURORA_MIN_ALPHA, 1.0f, aurora_unit(l, k, generation, 4)) *
                       (1.0f - fabsf(2.0f * age - 1.0f));
            /* A colour around the palette (cyclic, blended). */
            const float position = aurora_unit(l, k, generation, 5) * (float)colours;
            const int c0 = (int)position % colours;
            const int c1 = (c0 + 1) % colours;
            const float f = position - floorf(position);
            for (int c = 0; c < 3; ++c) {
                w->rgb[c] =
                    ((1.0f - f) * (float)s->palette[c0][c] + f * (float)s->palette[c1][c]) / 255.0f;
            }
        }
        for (int i = 0; i < count; ++i) {
            float sum[3] = {0.0f, 0.0f, 0.0f};
            for (int k = 0; k < slots; ++k) {
                const aurora_wave *w = &waves[k];
                float along = fmodf(fabsf((float)i - w->centre_px), (float)count);
                const float off = along < (float)count - along ? along : (float)count - along;
                if (off >= w->width_px) {
                    continue;
                }
                const float a = w->alpha * (1.0f - off / w->width_px);
                for (int c = 0; c < 3; ++c) {
                    sum[c] += a * w->rgb[c];
                }
            }
            /* Overlaps keep their hue: scaled down, not clipped towards white. */
            float peak = sum[0] > sum[1] ? sum[0] : sum[1];
            peak = peak > sum[2] ? peak : sum[2];
            const float scale = s->fallback_brightness / (peak > 1.0f ? peak : 1.0f);
            for (int c = 0; c < 3; ++c) { /* R, G, B, each to its place in the lane's order. */
                const float drive = powf(sum[c] * scale, 2.2f); /* Perceptual to LED drive. */
                out[(uint32_t)i * 3u + at[c]] = (uint8_t)(drive * 255.0f + 0.5f);
            }
        }
    }
}

bool node_frame(node_state *n, uint32_t now_ms, uint8_t lanes[NODE_LANES][NODE_LANE_BYTES]) {
    if (n->art_sync_mode && elapsed_ms(now_ms, n->art_sync_ms) > NODE_ART_SYNC_TIMEOUT_MS) {
        n->art_sync_mode = false; /* Art-Net 4: back to latching on data. */
        latch_all(n);
    }
    for (int u = 0; u < n->s.universe_count; ++u) {
        if (n->pending[u] && elapsed_ms(now_ms, n->pending_since_ms[u]) > NODE_SACN_SYNC_WAIT_MS &&
            !n->art_sync_mode) {
            latch(n, u); /* Its sync never came. */
            ++n->latches;
        }
    }
    if (node_signal_lost(n, now_ms)) {
        if (!n->lost) {
            n->lost = true;
            n->fresh = false;
            ++n->losses;
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
            const float gone = n->s.fade_ms == 0 ? 1.0f
                                                 : (float)elapsed_ms(now_ms, n->lost_since_ms) /
                                                       (float)n->s.fade_ms;
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
