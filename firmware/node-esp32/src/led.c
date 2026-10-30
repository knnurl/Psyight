#include "led.h"

#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "node_core.h"

#include <string.h>

#define RESOLUTION_HZ 10000000u /* 0.1 us per tick. */
#define LATCH_US 300

static const char *TAG = "led";
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static uint8_t s_buffer[NODE_LANE_BYTES]; /* What the RMT reads while it sends. */
static volatile bool s_busy;
static volatile int64_t s_done_us;
static volatile uint32_t s_frames;

static bool IRAM_ATTR on_done(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *event,
                              void *context) {
    (void)channel;
    (void)event;
    (void)context;
    s_done_us = esp_timer_get_time();
    ++s_frames;
    s_busy = false;
    return false;
}

bool led_init(int gpio) {
    const rmt_tx_channel_config_t channel = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        .mem_block_symbols = 256, /* Four of the eight blocks: refills a quarter as often. */
        .trans_queue_depth = 2,
    };
    if (rmt_new_tx_channel(&channel, &s_channel) != ESP_OK) {
        ESP_LOGE(TAG, "no RMT channel on GPIO %d", gpio);
        return false;
    }
    const rmt_bytes_encoder_config_t bits = {
        .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8},
        .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4},
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&bits, &s_encoder) != ESP_OK) {
        return false;
    }
    const rmt_tx_event_callbacks_t callbacks = {.on_trans_done = on_done};
    rmt_tx_register_event_callbacks(s_channel, &callbacks, NULL);
    s_done_us = esp_timer_get_time();
    return rmt_enable(s_channel) == ESP_OK;
}

bool led_ready(void) { return !s_busy && esp_timer_get_time() - s_done_us >= LATCH_US; }

bool led_send(const uint8_t *bytes, size_t length) {
    if (!led_ready() || length == 0) {
        return false;
    }
    if (length > sizeof(s_buffer)) {
        length = sizeof(s_buffer);
    }
    memcpy(s_buffer, bytes, length);
    const rmt_transmit_config_t once = {.loop_count = 0};
    s_busy = true;
    if (rmt_transmit(s_channel, s_encoder, s_buffer, length, &once) != ESP_OK) {
        s_busy = false;
        return false;
    }
    return true;
}

uint32_t led_frames(void) { return s_frames; }
