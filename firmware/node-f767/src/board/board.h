/* Nucleo-F767ZI board support for the Psyight node: clocks, LEDs, the ST-LINK virtual COM port
 * (USART3 on PD8/PD9, 115200 8N1, for printf), the independent watchdog, and a stable MAC address
 * from the chip's unique ID.
 *
 * Clocks: 8 MHz HSE bypass (ST-LINK MCO) -> PLL (M 4, N 216, P 2) -> 216 MHz SYSCLK, voltage
 * over-drive, 7 flash wait states, ART accelerator and I-cache on. AHB 216 MHz, APB1 54 MHz,
 * APB2 108 MHz (timers on APB2 run at 216 MHz). The D-cache stays off: the Ethernet and LED DMA
 * buffers then need no cache maintenance or MPU setup, and the node's work is far below what
 * the core can do uncached (SPEC 12.2's DTCM placement is the alternative if it is ever needed).
 */
#ifndef PSY_BOARD_H
#define PSY_BOARD_H

#include "stm32f7xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { LED_GREEN, LED_BLUE, LED_RED } board_led_id; /* LD1 PB0, LD2 PB7, LD3 PB14. */

void board_init(void); /* HAL, clocks, caches, LEDs, UART, cycle counter. */
void board_led(board_led_id led, bool on);
void board_led_toggle(board_led_id led);
void board_watchdog_start(uint32_t timeout_ms);
void board_watchdog_kick(void);
bool board_reset_by_watchdog(void); /* Read once at start-up. */
void board_mac_address(uint8_t mac[6]);
uint32_t board_cycles(void);        /* DWT cycle counter, 216 per microsecond. */
void board_error(const char *what); /* Logs, lights red, and lets the watchdog reset. */

#endif
