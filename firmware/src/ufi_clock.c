/**
 * UFI System Clock Configuration
 * STM32H723 @ 550 MHz
 */

#include "stm32h7xx_hal.h"

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Power Configuration */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    /* HSE + PLL für 550 MHz SYSCLK */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
    osc.HSEState = RCC_HSE_ON;
    osc.HSI48State = RCC_HSI48_ON;  // Für USB
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;      // 25MHz / 5 = 5MHz VCO Input
    osc.PLL.PLLN = 220;    // 5MHz * 220 = 1100MHz VCO
    osc.PLL.PLLP = 2;      // 1100MHz / 2 = 550MHz SYSCLK
    osc.PLL.PLLQ = 11;     // 100MHz (nicht verwendet)
    osc.PLL.PLLR = 2;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN = 0;
    
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while(1);
    }

    /* Bus Clocks */
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                  | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;     // 550 MHz
    clk.AHBCLKDivider = RCC_HCLK_DIV2;       // 275 MHz (Timer!)
    clk.APB3CLKDivider = RCC_APB3_DIV2;      // 137.5 MHz
    clk.APB1CLKDivider = RCC_APB1_DIV2;      // 137.5 MHz
    clk.APB2CLKDivider = RCC_APB2_DIV2;      // 137.5 MHz
    clk.APB4CLKDivider = RCC_APB4_DIV2;      // 137.5 MHz

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        while(1);
    }

    /* USB Clock aus HSI48 */
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    pclk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    HAL_RCCEx_PeriphCLKConfig(&pclk);
}

void Error_Handler(void) {
    __disable_irq();
    while(1) {}
}
