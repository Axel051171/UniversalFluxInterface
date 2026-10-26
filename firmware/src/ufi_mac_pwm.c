/**
 * UFI Flux Engine - Macintosh 400K drive speed PWM (board v1.0, J23 pin 20)
 *
 * TIM1_CH1 on PA8 (AF1) through U27 (AHCT244, FDD_5V) and the solder jumper JP5 (open by
 * default) to J23 pin 20.  Only a single-sided 400K drive takes its spindle speed from this
 * line; 800K and FDHD drives drive pin 20 themselves (!CSTOUT), so ufi_drive.c enables the
 * output for a detected 400K drive only.  The same pin comes back through U26 on PB1 (the
 * !CSTOUT read-back), which tells whether JP5 is closed.
 *
 * 22.25 kHz carrier (the duty is what the drive's speed loop integrates); duty in per mille.
 */

#include "ufi_firmware.h"
#include "ufi_mac.h"

#define MAC_PWM_HZ          22250u

/* The running state is the timer's own enable bit (reads 0 while the TIM1 clock is off),
 * so this module keeps no RAM of its own. */
void ufi_mac_pwm_set(uint16_t permille)
{
    if (permille > 1000u) {
        permille = 1000u;
    }
    if (!permille) {
        TIM1->CR1 = 0;
        TIM1->CCER = 0;
        HAL_GPIO_WritePin(PIN_MAC_PWM.port, PIN_MAC_PWM.pin, GPIO_PIN_RESET);
        GPIO_InitTypeDef g = {0};
        g.Pin = PIN_MAC_PWM.pin;
        g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(PIN_MAC_PWM.port, &g);
        return;
    }
    const uint32_t arr = FLUX_TIMER_FREQ / MAC_PWM_HZ - 1u;   /* APB2 timer clock = 275 MHz */
    if (!(TIM1->CR1 & TIM_CR1_CEN)) {
        __HAL_RCC_TIM1_CLK_ENABLE();
        TIM1->CR1 = 0;
        TIM1->PSC = 0;
        TIM1->ARR = arr;
        TIM1->CCR1 = 0;
        TIM1->CCMR1 = TIM_CCMR1_OC1PE | (6u << TIM_CCMR1_OC1M_Pos);   /* PWM mode 1, preload */
        TIM1->CCER = TIM_CCER_CC1E;
        TIM1->BDTR = TIM_BDTR_MOE;
        TIM1->EGR = TIM_EGR_UG;
        GPIO_InitTypeDef g = {0};
        g.Pin = PIN_MAC_PWM.pin;
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_MEDIUM;
        g.Alternate = GPIO_AF1_TIM1;
        HAL_GPIO_Init(PIN_MAC_PWM.port, &g);
        TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    }
    TIM1->CCR1 = (uint32_t)(((uint64_t)(arr + 1u) * permille) / 1000u);
}

/* JP5 closed: the PWM appears on J23 pin 20 and comes back on PB1.  Sample PB1 while a
 * 50 % duty runs for 2 ms; both levels seen = closed.  Leaves the PWM at the given duty. */
bool ufi_mac_pwm_loopback(uint16_t restore_permille)
{
    ufi_mac_pwm_set(500u);
    bool hi = false, lo = false;
    const uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < 3u && !(hi && lo)) {
        if (HAL_GPIO_ReadPin(PIN_MAC_CSTOUT.port, PIN_MAC_CSTOUT.pin) == GPIO_PIN_SET) hi = true;
        else lo = true;
    }
    ufi_mac_pwm_set(restore_permille);
    return hi && lo;
}
