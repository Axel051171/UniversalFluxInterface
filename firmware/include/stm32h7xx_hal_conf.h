/**
 * stm32h7xx_hal_conf.h - HAL Configuration
 * UFI Flux Engine - STM32H723
 */

#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * MODULE SELECTION
 * ============================================================================ */

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ============================================================================
 * OSCILLATOR VALUES
 * ============================================================================ */

#if !defined(HSE_VALUE)
#define HSE_VALUE    25000000UL  /* 25 MHz Crystal */
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT    100UL
#endif

#if !defined(CSI_VALUE)
#define CSI_VALUE    4000000UL   /* 4 MHz */
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE    64000000UL  /* 64 MHz */
#endif

#if !defined(LSE_VALUE)
#define LSE_VALUE    32768UL     /* 32.768 kHz */
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT    5000UL
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE    32000UL     /* 32 kHz */
#endif

#if !defined(EXTERNAL_CLOCK_VALUE)
#define EXTERNAL_CLOCK_VALUE    12288000UL
#endif

/* ============================================================================
 * SYSTEM CONFIGURATION
 * ============================================================================ */

#define VDD_VALUE                    3300UL  /* 3.3V */
#define TICK_INT_PRIORITY            15UL
#define USE_RTOS                     0
#define PREFETCH_ENABLE              0
#define ART_ACCELERATOR_ENABLE       0

/* ============================================================================
 * HAL CALLBACKS
 * ============================================================================ */

#define USE_HAL_ADC_REGISTER_CALLBACKS    0
#define USE_HAL_PCD_REGISTER_CALLBACKS    0
#define USE_HAL_TIM_REGISTER_CALLBACKS    0
#define USE_HAL_UART_REGISTER_CALLBACKS   0

/* ============================================================================
 * ETHERNET (not used)
 * ============================================================================ */

#define ETH_TX_DESC_CNT         4
#define ETH_RX_DESC_CNT         4
#define ETH_MAC_ADDR0           0x02
#define ETH_MAC_ADDR1           0x00
#define ETH_MAC_ADDR2           0x00
#define ETH_MAC_ADDR3           0x00
#define ETH_MAC_ADDR4           0x00
#define ETH_MAC_ADDR5           0x00

/* ============================================================================
 * SPI (for future use)
 * ============================================================================ */

#define USE_SPI_CRC              0

/* ============================================================================
 * HAL INCLUDES
 * ============================================================================ */

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32h7xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32h7xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32h7xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32h7xx_hal_cortex.h"
#endif

#ifdef HAL_ADC_MODULE_ENABLED
#include "stm32h7xx_hal_adc.h"
#endif

#ifdef HAL_EXTI_MODULE_ENABLED
#include "stm32h7xx_hal_exti.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32h7xx_hal_flash.h"
#endif

#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32h7xx_hal_iwdg.h"
#endif

#ifdef HAL_PCD_MODULE_ENABLED
#include "stm32h7xx_hal_pcd.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32h7xx_hal_pwr.h"
#endif

#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32h7xx_hal_tim.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
#include "stm32h7xx_hal_uart.h"
#endif

/* ============================================================================
 * ASSERT CONFIGURATION
 * ============================================================================ */

/* #define USE_FULL_ASSERT    1 */

#ifdef USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
void assert_failed(uint8_t *file, uint32_t line);
#else
#define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32H7XX_HAL_CONF_H */
