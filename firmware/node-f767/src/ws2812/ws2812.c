#include "ws2812.h"

#include "board.h"

#define TICKS_PER_BIT 270u /* 1.25 us at 216 MHz. */
#define TICK_T0H 86u       /* 398 ns. */
#define TICK_T1H 173u      /* 801 ns. */
#define LATCH_US 300u
#define CYCLES_PER_US 216u
#define DMA_FLAGS 0x3Du /* FE, DME, TE, HT and TC of one stream (bit 1 is reserved). */

static uint32_t s_words[NODE_MAX_PIXELS_PER_LANE * 24u]; /* 65 KB: reset masks per bit. */
static uint32_t s_set_mask;                              /* BSRR: active lanes high. */
static uint32_t s_reset_mask;                            /* BSRR: active lanes low. */
static volatile bool s_busy;
static volatile uint32_t s_done_cycles;
static volatile uint32_t s_frames;
static uint32_t s_transpose_us;

/* Streams 0-3 report in LISR/LIFCR, 4-7 in HISR/HIFCR, at bit offsets 0, 6, 16, 22. */
static void clear_flags(uint32_t stream) {
    static const uint8_t shift[4] = {0, 6, 16, 22};
    const uint32_t bits = DMA_FLAGS << shift[stream & 3u];
    if (stream < 4u) {
        DMA2->LIFCR = bits;
    } else {
        DMA2->HIFCR = bits;
    }
}

static void stream_setup(DMA_Stream_TypeDef *s, uint32_t stream, const volatile uint32_t *source,
                         bool increment, uint32_t priority, uint32_t count, bool interrupt) {
    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN) {
    }
    clear_flags(stream);
    s->PAR = (uint32_t)&GPIOE->BSRR;
    s->M0AR = (uint32_t)source;
    s->NDTR = count;
    s->FCR = 0;                                       /* Direct mode: one word per request. */
    s->CR = DMA_CHANNEL_6 | priority | DMA_SxCR_DIR_0 /* Memory to peripheral. */
            | DMA_SxCR_MSIZE_1 | DMA_SxCR_PSIZE_1     /* 32-bit both sides. */
            | (increment ? DMA_SxCR_MINC : 0u) | (interrupt ? DMA_SxCR_TCIE : 0u);
}

void ws2812_init(uint8_t active_mask) {
    s_set_mask = active_mask;
    s_reset_mask = (uint32_t)active_mask << 16;
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    GPIOE->BSRR = s_reset_mask; /* Low before switching to output: no glitch on the strips. */
    GPIO_InitTypeDef g = {0};
    g.Pin = active_mask;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOE, &g);

    TIM1->CR1 = 0;
    TIM1->PSC = 0;
    TIM1->ARR = TICKS_PER_BIT - 1u;
    TIM1->CCR1 = TICK_T0H;
    TIM1->CCR2 = TICK_T1H;
    TIM1->CCMR1 = 0; /* Output compare "frozen": no pin, but the compare events and DMA requests. */
    TIM1->CR1 = TIM_CR1_URS; /* Update requests only from overflow. */

    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
    s_done_cycles = board_cycles();
}

bool ws2812_ready(void) {
    return !s_busy && board_cycles() - s_done_cycles >= LATCH_US * CYCLES_PER_US;
}

bool ws2812_send(const uint8_t lanes[NODE_LANES][NODE_LANE_BYTES], uint16_t pixels) {
    if (!ws2812_ready() || pixels == 0) {
        return false;
    }
    if (pixels > NODE_MAX_PIXELS_PER_LANE) {
        pixels = NODE_MAX_PIXELS_PER_LANE;
    }
    const uint32_t start = board_cycles();
    node_transpose(lanes, pixels, (uint8_t)s_set_mask, s_words);
    s_transpose_us = (board_cycles() - start) / CYCLES_PER_US;

    const uint32_t bits = (uint32_t)pixels * 24u;
    stream_setup(DMA2_Stream5, 5, &s_set_mask, false, DMA_SxCR_PL, bits, false); /* Very high. */
    stream_setup(DMA2_Stream1, 1, s_words, true, DMA_SxCR_PL_1, bits, false);    /* High. */
    stream_setup(DMA2_Stream2, 2, &s_reset_mask, false, DMA_SxCR_PL_1, bits, true);
    DMA2_Stream5->CR |= DMA_SxCR_EN;
    DMA2_Stream1->CR |= DMA_SxCR_EN;
    DMA2_Stream2->CR |= DMA_SxCR_EN;

    s_busy = true;
    TIM1->SR = 0;
    TIM1->CNT = TICKS_PER_BIT - 1u; /* The first tick overflows: bit 0 starts with "high". */
    TIM1->DIER = TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_CC2DE;
    TIM1->CR1 |= TIM_CR1_CEN;
    return true;
}

void ws2812_dma_irq(void) {
    if (DMA2->LISR & DMA_LISR_TCIF2) {
        TIM1->CR1 &= ~TIM_CR1_CEN;
        TIM1->DIER = 0;
        clear_flags(2);
        GPIOE->BSRR = s_reset_mask; /* Already low; make sure. */
        s_done_cycles = board_cycles();
        ++s_frames;
        s_busy = false;
    } else {
        clear_flags(2); /* Transfer error: drop the frame, try again next time. */
        TIM1->CR1 &= ~TIM_CR1_CEN;
        TIM1->DIER = 0;
        s_done_cycles = board_cycles();
        s_busy = false;
    }
}

uint32_t ws2812_frames(void) { return s_frames; }

uint32_t ws2812_last_transpose_us(void) { return s_transpose_us; }
