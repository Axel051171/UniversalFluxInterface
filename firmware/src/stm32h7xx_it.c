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

/* Faults: hand the exception frame (MSP or PSP) and a kind code to ufi_crash.c, which
 * stores them in DTCM and resets; CRASH.LOG and INFO report it after the restart. */
#define UFI_FAULT_HANDLER(name, kind)                                   \
    __attribute__((naked)) void name(void)                              \
    {                                                                   \
        __asm volatile("tst lr, #4\n\t"                                 \
                       "ite eq\n\t"                                     \
                       "mrseq r0, msp\n\t"                              \
                       "mrsne r0, psp\n\t"                              \
                       "mov r1, #" #kind "\n\t"                        \
                       "b ufi_crash_fault");                            \
    }
UFI_FAULT_HANDLER(HardFault_Handler, 1)
UFI_FAULT_HANDLER(MemManage_Handler, 2)
UFI_FAULT_HANDLER(BusFault_Handler, 3)
UFI_FAULT_HANDLER(UsageFault_Handler, 4)

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
 * @brief  TIM3 Update (WDATA write timer: end of the last flux interval)
 */
void TIM3_IRQHandler(void)
{
    ufi_write_tim3_irq();
}

/**
 * @brief  DMA1 Stream1 Interrupt (WDATA intervals -> TIM3 ARR)
 */
void DMA1_Stream1_IRQHandler(void)
{
    ufi_write_dma_irq();
}

/**
 * @brief  USB OTG HS Global Interrupt
 */
void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
