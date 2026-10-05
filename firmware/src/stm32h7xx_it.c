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

/* Aus usbd_conf.c */
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

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
 * @brief  TIM2 Global Interrupt: INDEX capture on CH2 (PA1)
 */
void TIM2_IRQHandler(void)
{
    ufi_flux_tim2_irq();
}

/**
 * @brief  DMA1 Stream0 Interrupt (Flux Capture DMA, store full / error)
 */
void DMA1_Stream0_IRQHandler(void)
{
    ufi_flux_dma_irq();
}

/**
 * @brief  USB OTG HS Global Interrupt
 */
void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
