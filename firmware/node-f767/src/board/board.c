#include "board.h"

#include <stdio.h>

UART_HandleTypeDef g_uart3;
static IWDG_HandleTypeDef s_iwdg;
static bool s_watchdog_reset;

static void clock_init(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_BYPASS; /* 8 MHz from the ST-LINK's MCO, not a crystal. */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;             /* 2 MHz into the VCO. */
    osc.PLL.PLLN = 216;           /* 432 MHz VCO. */
    osc.PLL.PLLP = RCC_PLLP_DIV2; /* 216 MHz SYSCLK. */
    osc.PLL.PLLQ = 9;             /* 48 MHz. */
    osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        board_error("oscillator");
    }
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) { /* Needed above 180 MHz. */
        board_error("over-drive");
    }
    clk.ClockType =
        RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4; /* 54 MHz. */
    clk.APB2CLKDivider = RCC_HCLK_DIV2; /* 108 MHz; its timers run at 216 MHz. */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK) {
        board_error("clock");
    }
}

static void led_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_0 | GPIO_PIN_7 | GPIO_PIN_14;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
}

static void uart_init(void) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOD, &g);
    g_uart3.Instance = USART3;
    g_uart3.Init.BaudRate = 115200;
    g_uart3.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart3.Init.StopBits = UART_STOPBITS_1;
    g_uart3.Init.Parity = UART_PARITY_NONE;
    g_uart3.Init.Mode = UART_MODE_TX_RX;
    g_uart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart3);
}

void board_init(void) {
    SCB_EnableICache(); /* D-cache deliberately off (see board.h). */
    HAL_Init();         /* SysTick at 1 kHz, NVIC priority grouping, flash prefetch and ART. */
    clock_init();
    led_init();
    uart_init();
    /* DWT cycle counter for microsecond timing (the WS2812 reset gap). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55; /* Unlock the DWT on the M7. */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_watchdog_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0;
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void board_led(board_led_id led, bool on) {
    static const uint16_t pins[] = {GPIO_PIN_0, GPIO_PIN_7, GPIO_PIN_14};
    HAL_GPIO_WritePin(GPIOB, pins[led], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_led_toggle(board_led_id led) {
    static const uint16_t pins[] = {GPIO_PIN_0, GPIO_PIN_7, GPIO_PIN_14};
    HAL_GPIO_TogglePin(GPIOB, pins[led]);
}

void board_watchdog_start(uint32_t timeout_ms) {
    /* LSI ~32 kHz / 64 = 500 Hz: 2 ms per count, 12-bit reload (up to ~8 s). LSI varies by
     * about +-10%, so keep a margin over the longest main-loop stall. */
    s_iwdg.Instance = IWDG;
    s_iwdg.Init.Prescaler = IWDG_PRESCALER_64;
    uint32_t reload = timeout_ms / 2u;
    s_iwdg.Init.Reload = reload > 0xFFFu ? 0xFFFu : reload;
    s_iwdg.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&s_iwdg);
}

void board_watchdog_kick(void) {
    if (s_iwdg.Instance != NULL) {
        HAL_IWDG_Refresh(&s_iwdg);
    }
}

bool board_reset_by_watchdog(void) { return s_watchdog_reset; }

void board_mac_address(uint8_t mac[6]) {
    /* Locally administered unicast (02:...), the rest a hash of the 96-bit unique ID, so each
     * board keeps the same address across firmware updates. */
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    uint32_t h = 2166136261u; /* FNV-1a. */
    for (int i = 0; i < 3; ++i) {
        for (int b = 0; b < 4; ++b) {
            h = (h ^ ((uid[i] >> (8 * b)) & 0xFFu)) * 16777619u;
        }
    }
    mac[0] = 0x02;
    mac[1] = 0x50; /* 'P' */
    mac[2] = (uint8_t)(h >> 24);
    mac[3] = (uint8_t)(h >> 16);
    mac[4] = (uint8_t)(h >> 8);
    mac[5] = (uint8_t)h;
}

uint32_t board_cycles(void) { return DWT->CYCCNT; }

void board_error(const char *what) {
    printf("fatal: %s\r\n", what);
    __disable_irq();
    board_led(LED_RED, true);
    for (;;) {
        /* The watchdog, if started, resets the node. */
    }
}
