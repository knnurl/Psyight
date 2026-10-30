/* WS2812 output on one GPIO through the RMT peripheral (ESP-IDF's RMT TX driver and its bytes
 * encoder): at 10 MHz a 0 bit is high 0.4 us then low 0.8 us, a 1 bit high 0.8 us then low
 * 0.4 us, most significant bit first, with at least 300 us low between frames.
 *
 * The classic ESP32's RMT has no DMA: it refills its memory from an interrupt, and a late refill
 * glitches the strip. So the channel takes 256 symbols of RMT memory (fewer refills) and must be
 * created from a task on the core the WiFi does not use, where its interrupt then runs. */
#ifndef PSY_LED_H
#define PSY_LED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool led_init(int gpio);
/* Idle and past the latch gap. */
bool led_ready(void);
/* Starts sending `length` bytes (copied: the caller's buffer may change at once). */
bool led_send(const uint8_t *bytes, size_t length);
uint32_t led_frames(void);

#endif
