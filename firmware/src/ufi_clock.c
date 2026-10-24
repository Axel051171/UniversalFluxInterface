/**
 * UFI System Clock Configuration
 * STM32H723 @ 550 MHz
 *
 * HSE (board v1.0, stage A9c): a 25 MHz HCMOS TCXO (X1, HCI TC32H4) drives OSC_IN, OSC_OUT
 * is open, so the HSE runs in bypass mode.  v0.x boards have a 25 MHz crystal (Y1) and need
 * the crystal oscillator.  Bypass is tried first: with a crystal attached the bypass input
 * sees no clock, HSERDY never comes (HSE_STARTUP_TIMEOUT, 100 ms) and the crystal oscillator
 * is started instead.  The other order would run the TCXO through the crystal amplifier,
 * which is not a specified mode.  ufi_clock_hse_bypass() tells which one is running.
 *
 * LSE / RTC (v1.0 only): 32.768 kHz crystal Y3 (Epson FC-135, CL 7 pF, ESR 70 kOhm max) on
 * PC14/PC15 and a CR2032 on VBAT.  gm_crit = 4 * ESR * (2*pi*f)^2 * (C0 + CL)^2
 * = 4 * 70k * (2*pi*32768)^2 * (1 pF + 7 pF)^2 = 0.76 uA/V, so the LSE drive must be
 * "medium high" (gm_crit_max 1.7 uA/V; "medium low" allows only 0.75).  The RTC keeps
 * running from the cell while the board is off; VBAT charging stays off (CR2032 is not
 * rechargeable).  ufi_clock_rtc_init() is called from ufi_init() once the board revision
 * is known, v0.x boards have no LSE crystal (VBAT tied to +3V3 there).
 * Host access to the calendar (set / get) follows in stage A10.
 */

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "ufi_firmware.h"

#define LSE_WAIT_MS   2000u     /* own limit instead of LSE_STARTUP_TIMEOUT (5 s) */

static bool hse_bypass;
static bool rtc_ready;

static HAL_StatusTypeDef hse_config(uint32_t state)
{
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = state;
    osc.PLL.PLLState = RCC_PLL_NONE;
    return HAL_RCC_OscConfig(&osc);
}

bool ufi_clock_hse_bypass(void)
{
    return hse_bypass;
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Power Configuration */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    /* HSE: TCXO (bypass, v1.0) first, crystal (v0.x) second.  RCC_HSE_OFF in between clears
     * HSEBYP - RCC_HSE_ON alone leaves the bypass bit set. */
    hse_bypass = (hse_config(RCC_HSE_BYPASS) == HAL_OK);
    if (!hse_bypass) {
        (void)hse_config(RCC_HSE_OFF);
        if (hse_config(RCC_HSE_ON) != HAL_OK) {
            while(1);
        }
    }

    /* PLL from HSE für 550 MHz SYSCLK, HSI48 for USB */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
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

/* ============================================================================
 * LSE + RTC (board v1.0)
 * ============================================================================ */

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static uint8_t unbcd(uint32_t v)
{
    return (uint8_t)((((v >> 4) & 0xFu) * 10u) + (v & 0xFu));
}

static bool rtc_enter_init(void)
{
    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
    RTC->ISR |= RTC_ISR_INIT;
    const uint32_t t0 = HAL_GetTick();
    while ((RTC->ISR & RTC_ISR_INITF) == 0u) {
        if (HAL_GetTick() - t0 > 10u) {
            RTC->WPR = 0xFFu;
            return false;
        }
    }
    return true;
}

static void rtc_exit_init(void)
{
    RTC->ISR &= ~RTC_ISR_INIT;
    RTC->WPR = 0xFFu;
}

void ufi_clock_rtc_init(void)
{
    rtc_ready = false;
    HAL_PWR_EnableBkUpAccess();                 /* PWR_CR1 DBP: backup domain writable */
    __HAL_RCC_RTC_CLK_ENABLE();                 /* APB4 RTCAPB: register access */

    /* RTCSEL can be written once per backup-domain reset: a stale non-LSE selection
     * (e.g. after a firmware that used LSI) needs the reset first. */
    const uint32_t sel = __HAL_RCC_GET_RTC_SOURCE();
    if (sel != 0u && sel != RCC_RTCCLKSOURCE_LSE) {
        __HAL_RCC_BACKUPRESET_FORCE();
        __HAL_RCC_BACKUPRESET_RELEASE();
    }

    if ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
        __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_MEDIUMHIGH);
        SET_BIT(RCC->BDCR, RCC_BDCR_LSEON);
        const uint32_t t0 = HAL_GetTick();
        while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
            if (HAL_GetTick() - t0 > LSE_WAIT_MS) {
                CLEAR_BIT(RCC->BDCR, RCC_BDCR_LSEON);   /* no crystal / not oscillating */
                return;
            }
        }
    }

    if (__HAL_RCC_GET_RTC_SOURCE() != RCC_RTCCLKSOURCE_LSE) {
        __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    }
    __HAL_RCC_RTC_ENABLE();

    /* INITS = calendar was set before (cell kept it): leave the time alone */
    if ((RTC->ISR & RTC_ISR_INITS) == 0u) {
        if (!rtc_enter_init()) {
            return;
        }
        RTC->PRER = (127u << RTC_PRER_PREDIV_A_Pos) | 255u;   /* 32768 / 128 / 256 = 1 Hz */
        RTC->CR &= ~RTC_CR_FMT;                                /* 24 h */
        RTC->TR = 0u;                                          /* 00:00:00 */
        RTC->DR = (0u << RTC_DR_YU_Pos) | (1u << RTC_DR_WDU_Pos) | (1u << 8) | 1u;   /* 2000-01-01, Monday */
        rtc_exit_init();
    }
    rtc_ready = true;
}

bool ufi_clock_rtc_ready(void)
{
    return rtc_ready;
}

bool ufi_clock_rtc_get(uint16_t* year, uint8_t* mon, uint8_t* day, uint8_t* hour, uint8_t* min, uint8_t* sec)
{
    if (!rtc_ready) {
        return false;
    }
    const uint32_t tr = RTC->TR;        /* TR first: locks DR in the shadow registers */
    const uint32_t dr = RTC->DR;
    *hour = unbcd(tr >> 16);
    *min  = unbcd(tr >> 8);
    *sec  = unbcd(tr);
    *year = (uint16_t)(2000u + unbcd(dr >> 16));
    *mon  = unbcd((dr >> 8) & 0x1Fu);
    *day  = unbcd(dr);
    return true;
}

bool ufi_clock_rtc_set(uint16_t year, uint8_t mon, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec)
{
    if (!rtc_ready || year < 2000u || year > 2099u || mon < 1u || mon > 12u || day < 1u || day > 31u ||
        hour > 23u || min > 59u || sec > 59u) {
        return false;
    }
    if (!rtc_enter_init()) {
        return false;
    }
    RTC->TR = ((uint32_t)bcd(hour) << 16) | ((uint32_t)bcd(min) << 8) | bcd(sec);
    RTC->DR = ((uint32_t)bcd((uint8_t)(year - 2000u)) << 16) | (1u << RTC_DR_WDU_Pos) |
              ((uint32_t)bcd(mon) << 8) | bcd(day);       /* weekday not tracked: always "Monday" */
    rtc_exit_init();
    return true;
}

void Error_Handler(void) {
    __disable_irq();
    while(1) {}
}
