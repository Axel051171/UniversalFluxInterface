/**
 * UFI Flux Engine - USB-C source current detection (board v0.3)
 *
 * The sink's Rd (5.1k) on CC1/CC2 forms a divider with the source's Rp; the voltage on
 * the active CC line advertises the allowed current (USB Type-C spec, vRd ranges):
 *   0.25 - 0.61 V  default USB (500 mA / 900 mA)
 *   0.70 - 1.16 V  1.5 A
 *   1.31 - 2.04 V  3.0 A
 * CC1 = PA0 (ADC1_INP16), CC2 = PA3 (ADC1_INP15).  ADC clock: per_ck (HSI 64 MHz) / 2.
 * v0.5: the same ADC reads the drive supply currents (PC0/PC1) and the board ID (PA4),
 * see ufi_board.c.
 */

#include "ufi_firmware.h"

static ADC_HandleTypeDef hadc1;
static bool adc_ready;

static void adc_init(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    pclk.AdcClockSelection = RCC_ADCCLKSOURCE_CLKP;     /* per_ck = HSI 64 MHz */
    HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_0 | GPIO_PIN_3 | GPIO_PIN_4;        /* CC1, CC2, BOARD_ID */
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;                     /* I_FDD5, I_FDD12 */
    HAL_GPIO_Init(GPIOC, &g);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
    hadc1.Init.Resolution = ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        return;
    }
    adc_ready = true;
}

static uint16_t read_mv(uint32_t channel)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = channel;
    c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;     /* CC source impedance ~5k */
    c.SingleDiff = ADC_SINGLE_ENDED;
    c.OffsetNumber = ADC_OFFSET_NONE;
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK || HAL_ADC_Start(&hadc1) != HAL_OK ||
        HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        return 0;
    }
    return (uint16_t)((HAL_ADC_GetValue(&hadc1) * 3300UL) / 65535UL);
}

uint16_t ufi_adc_mv(uint32_t channel)
{
    if (!adc_ready) {
        adc_init();
        if (!adc_ready) {
            return 0;
        }
    }
    return read_mv(channel);
}

int ufi_usb_power(usb_power_t* p)
{
    if (!adc_ready) {
        adc_init();
        if (!adc_ready) {
            return UFI_ERR_NOT_IMPL;
        }
    }
    p->cc1_mv = read_mv(ADC_CHANNEL_16);
    p->cc2_mv = read_mv(ADC_CHANNEL_15);
    const uint16_t v = (p->cc1_mv > p->cc2_mv) ? p->cc1_mv : p->cc2_mv;
    p->current_ma = (v >= 1310) ? 3000 : (v >= 700) ? 1500 : (v >= 250) ? 500 : 0;
    return UFI_OK;
}
