/* Interrupt handlers. Priorities: DMA2 stream 2 (end of an LED frame) at 1, SysTick at 15. */
#include "board.h"
#include "ws2812.h"

void NMI_Handler(void) {
    for (;;) {
    }
}

void HardFault_Handler(void) {
    /* With a debugger attached, inspect SCB->CFSR and SCB->HFSR here. The watchdog resets. */
    board_led(LED_RED, true);
    for (;;) {
    }
}

void MemManage_Handler(void) { HardFault_Handler(); }

void BusFault_Handler(void) { HardFault_Handler(); }

void UsageFault_Handler(void) { HardFault_Handler(); }

void SysTick_Handler(void) { HAL_IncTick(); }

void DMA2_Stream2_IRQHandler(void) { ws2812_dma_irq(); }
