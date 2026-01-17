/**
 * system_stm32h7xx.c - System Initialization
 * STM32H723 @ 550 MHz
 */

#include "stm32h7xx.h"

/* Vector Table Offset */
#define VECT_TAB_OFFSET  0x00000000UL

/* System Clock Frequency */
uint32_t SystemCoreClock = 64000000UL;  /* Initial HSI, updated by SystemClock_Config */

const uint8_t D1CorePrescTable[16] = {0, 0, 0, 0, 1, 2, 3, 4, 1, 2, 3, 4, 6, 7, 8, 9};

/**
 * @brief  Setup the microcontroller system.
 * @retval None
 */
void SystemInit(void)
{
    /* FPU settings */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << (10*2)) | (3UL << (11*2)));  /* CP10, CP11 Full Access */
#endif

    /* Reset the RCC clock configuration to the default reset state */
    /* Set HSION bit */
    RCC->CR |= RCC_CR_HSION;

    /* Reset CFGR register */
    RCC->CFGR = 0x00000000;

    /* Reset HSEON, CSSON , CSION, RC48ON, CSIKERON, PLL1ON, PLL2ON and PLL3ON bits */
    RCC->CR &= 0xEAF6ED7FU;

    /* Decreasing the number of wait states because of lower CPU frequency */
    MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_7WS);

    /* Reset D1CFGR register */
    RCC->D1CFGR = 0x00000000;

    /* Reset D2CFGR register */
    RCC->D2CFGR = 0x00000000;

    /* Reset D3CFGR register */
    RCC->D3CFGR = 0x00000000;

    /* Reset PLLCKSELR register */
    RCC->PLLCKSELR = 0x02020200;

    /* Reset PLLCFGR register */
    RCC->PLLCFGR = 0x01FF0000;

    /* Reset PLL1DIVR register */
    RCC->PLL1DIVR = 0x01010280;

    /* Reset PLL1FRACR register */
    RCC->PLL1FRACR = 0x00000000;

    /* Reset PLL2DIVR register */
    RCC->PLL2DIVR = 0x01010280;

    /* Reset PLL2FRACR register */
    RCC->PLL2FRACR = 0x00000000;

    /* Reset PLL3DIVR register */
    RCC->PLL3DIVR = 0x01010280;

    /* Reset PLL3FRACR register */
    RCC->PLL3FRACR = 0x00000000;

    /* Reset HSEBYP bit */
    RCC->CR &= 0xFFFBFFFFU;

    /* Disable all interrupts */
    RCC->CIER = 0x00000000;

    /* Configure the Vector Table location */
#if defined(VECT_TAB_SRAM)
    SCB->VTOR = D1_AXISRAM_BASE | VECT_TAB_OFFSET;
#else
    SCB->VTOR = FLASH_BANK1_BASE | VECT_TAB_OFFSET;
#endif

    /* Enable I-Cache */
    SCB_EnableICache();

    /* Enable D-Cache */
    SCB_EnableDCache();
}

/**
 * @brief  Update SystemCoreClock variable
 * @retval None
 */
void SystemCoreClockUpdate(void)
{
    uint32_t pllp, pllsource, pllm, pllfracen, hsivalue, tmp;
    uint32_t common_system_clock;
    float_t fracn1, pllvco;

    /* Get SYSCLK source */
    switch (RCC->CFGR & RCC_CFGR_SWS)
    {
        case RCC_CFGR_SWS_HSI:  /* HSI */
            common_system_clock = (uint32_t)(HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
            break;

        case RCC_CFGR_SWS_CSI:  /* CSI */
            common_system_clock = CSI_VALUE;
            break;

        case RCC_CFGR_SWS_HSE:  /* HSE */
            common_system_clock = HSE_VALUE;
            break;

        case RCC_CFGR_SWS_PLL1:  /* PLL1 */
            pllsource = (RCC->PLLCKSELR & RCC_PLLCKSELR_PLLSRC);
            pllm = ((RCC->PLLCKSELR & RCC_PLLCKSELR_DIVM1) >> 4);
            pllfracen = ((RCC->PLLCFGR & RCC_PLLCFGR_PLL1FRACEN) >> RCC_PLLCFGR_PLL1FRACEN_Pos);
            fracn1 = (float_t)(uint32_t)(pllfracen * ((RCC->PLL1FRACR & RCC_PLL1FRACR_FRACN1) >> 3));

            if (pllm != 0U)
            {
                switch (pllsource)
                {
                    case RCC_PLLCKSELR_PLLSRC_HSI:
                        hsivalue = (HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> 3));
                        pllvco = ((float_t)hsivalue / (float_t)pllm) * 
                                 ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                        break;

                    case RCC_PLLCKSELR_PLLSRC_CSI:
                        pllvco = ((float_t)CSI_VALUE / (float_t)pllm) * 
                                 ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                        break;

                    case RCC_PLLCKSELR_PLLSRC_HSE:
                        pllvco = ((float_t)HSE_VALUE / (float_t)pllm) * 
                                 ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                        break;

                    default:
                        pllvco = ((float_t)HSI_VALUE / (float_t)pllm) * 
                                 ((float_t)(uint32_t)(RCC->PLL1DIVR & RCC_PLL1DIVR_N1) + (fracn1 / (float_t)0x2000) + (float_t)1);
                        break;
                }
                pllp = (((RCC->PLL1DIVR & RCC_PLL1DIVR_P1) >> 9) + 1U);
                common_system_clock = (uint32_t)(float_t)(pllvco / (float_t)pllp);
            }
            else
            {
                common_system_clock = 0U;
            }
            break;

        default:
            common_system_clock = HSI_VALUE;
            break;
    }

    /* System clock frequency */
    tmp = D1CorePrescTable[(RCC->D1CFGR & RCC_D1CFGR_D1CPRE) >> RCC_D1CFGR_D1CPRE_Pos];
    SystemCoreClock = common_system_clock >> tmp;
}
