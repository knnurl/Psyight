/* Parallel WS2812 output (SPEC 12.2): up to 8 strips on GPIOD pins 0 to 7, driven by TIM1 and
 * three DMA2 streams writing the port's BSRR register, with no CPU work during a frame.
 *
 * Per bit (TIM1 at 216 MHz, 270 ticks = 1.25 us, 800 kHz):
 *   update (tick 0)    DMA2 stream 5: all active lanes high
 *   CC1 (tick 86)      DMA2 stream 1: lanes whose bit is 0 low (398 ns: T0H)
 *   CC2 (tick 173)     DMA2 stream 2: all lanes low (801 ns: T1H)
 * Only DMA2 reaches AHB1 GPIO. Request map (DMA2, channel 6): TIM1_UP stream 5, TIM1_CH1
 * stream 1, TIM1_CH2 stream 2, as in CubeMX 6.15's STM32F7 DMA database (RM0410 table "DMA2
 * request mapping"). PD0 to PD7 have no Nucleo-F767ZI board function (CubeMX board file) and
 * all sit on the Zio headers: PD7 to PD3 on CN9 pins 2 to 10, PD0 and PD1 on CN9 pins 25 and 27,
 * PD2 on CN8 pin 12 (UM1974 table 13). Until 2026-10-23 the lines were PE0 to PE7.
 * The counter starts at its top so the first event is
 * the update (else bit 0 would begin with its low writes). Stream 2's transfer-complete
 * interrupt stops the timer; a 300 us low gap then latches the strips (WS2812B needs > 280 us).
 * Bench check (F1): T0H 0.40 us, T1H 0.80 us, period 1.25 us, each within +-150 ns, also under
 * Ethernet load (both DMAs share the bus matrix).
 */
#ifndef PSY_WS2812_H
#define PSY_WS2812_H

#include "node_core.h"

#include <stdbool.h>
#include <stdint.h>

void ws2812_init(uint8_t active_mask); /* Which of PD0..PD7 carry strips. */
bool ws2812_ready(void);               /* Idle and past the latch gap. */
/* Transposes and starts a frame of `pixels` per lane; false (nothing sent) when busy. */
bool ws2812_send(const uint8_t lanes[NODE_LANES][NODE_LANE_BYTES], uint16_t pixels);
uint32_t ws2812_frames(void);
uint32_t ws2812_last_transpose_us(void);
void ws2812_dma_irq(void); /* From DMA2_Stream2_IRQHandler. */

#endif
