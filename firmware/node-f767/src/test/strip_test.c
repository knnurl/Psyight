/* Strip test: a bare WS2812 driver with nothing else, to check strips and wiring apart from the
 * node (no DMA engine, network or stored config). Every lane (PD0 to PD7) gets the same data,
 * bit-banged on the cycle counter with interrupts off for each frame:
 * - STRIP_TEST_PIXELS pixels a frame (more than any one rig line), about 30 frames a second;
 * - every pixel dim, the colour stepping red, green, blue, white once a second;
 * - one bright pixel running along the line at 40 pixels a second, then starting again, so the
 *   last pixel that gets data is easy to see.
 * Timing at 216 MHz: a bit is 270 cycles (1.25 us), high for 86 (0.40 us) for a 0 and 173
 * (0.80 us) for a 1; at least 300 us low between frames. Green LED: alive. The console prints
 * the pixel count and the frame rate. */
#include "board.h"

#include <stdio.h>

#define STRIP_TEST_PIXELS 1000u
#define LANES_MASK 0xFFu
#define BIT_CYCLES 270u
#define T0H_CYCLES 86u
#define T1H_CYCLES 173u
#define LATCH_US 300u
#define DIM 24u
#define BRIGHT 200u

static inline uint32_t cycles(void) { return DWT->CYCCNT; }

/* The only interrupt the test needs (the node's stm32f7xx_it.c would bring in its DMA engine). */
void SysTick_Handler(void);
void SysTick_Handler(void) { HAL_IncTick(); }

static void lanes_init(void) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIOD->BSRR = LANES_MASK << 16; /* Low before they become outputs. */
    GPIO_InitTypeDef g = {0};
    g.Pin = LANES_MASK;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOD, &g);
}

/* One byte, most significant bit first, on every lane. */
static void send_byte(uint8_t v) {
    for (int b = 7; b >= 0; --b) {
        const uint32_t start = cycles();
        GPIOD->BSRR = LANES_MASK;
        const uint32_t high = (v >> b) & 1u ? T1H_CYCLES : T0H_CYCLES;
        while (cycles() - start < high) {
        }
        GPIOD->BSRR = LANES_MASK << 16;
        while (cycles() - start < BIT_CYCLES) {
        }
    }
}

int main(void) {
    board_init();
    lanes_init();
    printf("\r\nPsyight strip test: %u pixels on PD0 to PD7, dim colour each second, a bright "
           "runner\r\n",
           (unsigned)STRIP_TEST_PIXELS);
    uint32_t frames = 0;
    uint32_t last_print = 0;
    uint32_t last_blink = 0;
    /* Time from the cycle counter: interrupts are off through each frame, so the 1 ms tick
     * would lose all but one of a frame's ticks. */
    uint64_t elapsed = 0;
    uint32_t before = cycles();
    for (;;) {
        const uint32_t at = cycles();
        elapsed += at - before;
        before = at;
        const uint32_t now = (uint32_t)(elapsed / 216000u); /* Milliseconds. */
        const uint32_t colour = (now / 1000u) % 4u; /* Red, green, blue, white. */
        const uint32_t runner = (now / 25u) % STRIP_TEST_PIXELS;
        __disable_irq();
        for (uint32_t i = 0; i < STRIP_TEST_PIXELS; ++i) {
            const uint8_t v = i == runner ? BRIGHT : DIM;
            const uint8_t r = colour == 0 || colour == 3 ? v : 0;
            const uint8_t g = colour == 1 || colour == 3 ? v : 0;
            const uint8_t b = colour == 2 || colour == 3 ? v : 0;
            send_byte(g); /* WS2812 order: green, red, blue. */
            send_byte(r);
            send_byte(b);
        }
        __enable_irq();
        const uint32_t latch = cycles();
        while (cycles() - latch < LATCH_US * 216u) {
        }
        ++frames;
        if (now - last_blink >= 500u) {
            last_blink = now;
            board_led_toggle(LED_GREEN);
        }
        if (now - last_print >= 5000u) {
            printf("strip test: %lu frames a second\r\n", (unsigned long)(frames / 5u));
            frames = 0;
            last_print = now;
        }
    }
}
