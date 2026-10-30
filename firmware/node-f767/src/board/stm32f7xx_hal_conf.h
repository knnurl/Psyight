/* HAL configuration for the Psyight node (Nucleo-F767ZI). Only the modules the node uses.
 * HSE is the ST-LINK's 8 MHz MCO into PH0 (bypass mode): the template's 25 MHz would put every
 * derived clock out by a factor of about 3. */
#ifndef STM32F7XX_HAL_CONF_H
#define STM32F7XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_ETH_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define HSE_VALUE 8000000U
#define HSE_STARTUP_TIMEOUT 100U
#define HSI_VALUE 16000000U
#define LSI_VALUE 32000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define EXTERNAL_CLOCK_VALUE 12288000U

#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0x0FU /* SysTick lowest: the LED DMA interrupt must never wait on it. */
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define ART_ACCELERATOR_ENABLE 1U

#define USE_HAL_ETH_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U

/* Ethernet: 16 receive and four transmit DMA descriptors, 1536-byte buffers. Receive was the
 * HAL's four, one frame's worth (three universes and sync): a controller sending a little
 * unevenly, or the main loop busy with an LED frame or a status line, overflowed them and lost
 * universes (sequence gaps; parts of the rig showing a stale frame, seen as flicker). */
#define ETH_TX_DESC_CNT 4U
#define ETH_RX_DESC_CNT 16U
#define ETH_RX_BUF_SIZE 1536U
#define PHY_READ_TO 0x0000FFFFU /* MDIO read and write timeouts the ETH driver expects. */
#define PHY_WRITE_TO 0x0000FFFFU

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f7xx_hal_rcc.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32f7xx_hal_exti.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f7xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f7xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f7xx_hal_cortex.h"
#endif
#ifdef HAL_ETH_MODULE_ENABLED
#include "stm32f7xx_hal_eth.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f7xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f7xx_hal_pwr.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32f7xx_hal_iwdg.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f7xx_hal_uart.h"
#endif

#define assert_param(expr) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif
