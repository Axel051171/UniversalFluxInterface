/**
 * stm32h7xx_it.c - Interrupt Handlers
 * UFI Flux Engine
 * 
 * ZENTRALE IRQ-Datei - alle Interrupt Handler hier!
 */

#include "stm32h7xx_hal.h"
#include "ufi_firmware.h"

/* ============================================================================
 * EXTERNE HANDLES (definiert in anderen Modulen)
 * ============================================================================ */

/* Aus ufi_flux.c */
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_tim2;

/* Aus usbd_conf.c */
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* Aus ufi_main.c */
extern void ufi_flux_index_handler(void);

/* ============================================================================
 * CORTEX-M7 PROCESSOR EXCEPTIONS
 * ============================================================================ */

void NMI_Handler(void)
{
    while (1) {}
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* ============================================================================
 * STM32H7xx PERIPHERAL INTERRUPT HANDLERS
 * ============================================================================ */

/**
 * @brief  TIM2 Global Interrupt (Flux Capture Timer)
 */
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

/**
 * @brief  DMA1 Stream0 Interrupt (Flux Capture DMA)
 */
void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_tim2);
}

/**
 * @brief  EXTI Line0 Interrupt (Index Pulse - PC0)
 */
void EXTI0_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0))
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);
        ufi_flux_index_handler();
    }
}

/**
 * @brief  USB OTG HS Global Interrupt
 */
void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}

/**
 * @brief  USB OTG HS Wakeup Interrupt
 */
void OTG_HS_WKUP_IRQHandler(void)
{
    __HAL_USB_OTG_HS_WAKEUP_EXTI_CLEAR_FLAG();
}
