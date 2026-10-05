/**
 * UFI Flux Engine - Debug Commands
 *
 * Hardware-Test und Diagnose-Funktionen.  All bits report LOGICAL state:
 * 1 = line asserted on the bus (independent of the board's inverting buffers).
 */

#include "ufi_firmware.h"

extern TIM_HandleTypeDef htim2;

/* ============================================================================
 * DEBUG GPIO
 * ============================================================================ */

/* Bit order of gpio_status_t (unchanged USB protocol):
 *  fdd_inputs : 0 INDEX, 1 TRK0, 2 WPROT, 3 RDATA, 4 DKCHG, 5 READY
 *  fdd_outputs: 0 MOTOR_A, 1 MOTOR_B, 2 SEL_A, 3 SEL_B, 4 STEP, 5 DIR, 6 SIDE,
 *               7 WGATE, 8 WDATA, 9 DENSITY
 *  iec_signals: 0 ATN, 1 CLK, 2 DATA, 3 SRQ, 4 RESET (bus state, read back via inputs)
 *  leds       : 0 ACT, 1 FDD, 2 USB, 3 ERR */
static const gpio_pin_t* const fdd_in_pins[] = {
    &PIN_FDD_INDEX, &PIN_FDD_TRACK0, &PIN_FDD_WPROT, &PIN_FDD_RDATA, &PIN_FDD_DKCHG,
    &PIN_FDD_READY,
};
static const gpio_pin_t* const fdd_out_pins[] = {
    &PIN_FDD_MOTOR_A, &PIN_FDD_MOTOR_B, &PIN_FDD_DRV_SEL_A, &PIN_FDD_DRV_SEL_B,
    &PIN_FDD_STEP, &PIN_FDD_DIR, &PIN_FDD_SIDE_SEL, &PIN_FDD_WGATE, &PIN_FDD_WDATA,
    &PIN_FDD_DENSITY,
};
static const gpio_pin_t* const iec_in_pins[] = {
    &PIN_IEC_ATN_IN, &PIN_IEC_CLK_IN, &PIN_IEC_DATA_IN, &PIN_IEC_SRQ_IN, &PIN_IEC_RESET_IN,
};
static const gpio_pin_t* const iec_out_pins[] = {
    &PIN_IEC_ATN_OUT, &PIN_IEC_CLK_OUT, &PIN_IEC_DATA_OUT, &PIN_IEC_SRQ_OUT, &PIN_IEC_RESET_OUT,
};
static const gpio_pin_t* const led_pins[] = {
    &PIN_LED_ACT, &PIN_LED_FDD, &PIN_LED_USB, &PIN_LED_ERR,
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

gpio_status_t ufi_debug_gpio_read(void)
{
    gpio_status_t status = {0};

    for (unsigned i = 0; i < COUNT(fdd_in_pins); i++) {
        if (bus_in(fdd_in_pins[i])) status.fdd_inputs |= (uint16_t)(1u << i);
    }
    for (unsigned i = 0; i < COUNT(fdd_out_pins); i++) {
        if (bus_out_state(fdd_out_pins[i])) status.fdd_outputs |= (uint16_t)(1u << i);
    }
    for (unsigned i = 0; i < COUNT(iec_in_pins); i++) {
        if (bus_in(iec_in_pins[i])) status.iec_signals |= (uint16_t)(1u << i);
    }
    for (unsigned i = 0; i < COUNT(led_pins); i++) {
        if (HAL_GPIO_ReadPin(led_pins[i]->port, led_pins[i]->pin) == GPIO_PIN_SET) {
            status.leds |= (uint16_t)(1u << i);
        }
    }
    return status;
}

/**
 * Einzelnes GPIO setzen (für Tests)
 * @param gpio_id   0-9 = FDD Outputs, 16-20 = IEC Outputs, 32-35 = LEDs
 * @param state     0 = Inaktiv, 1 = Aktiv (asserted / LED an)
 */
int ufi_debug_gpio_set(uint8_t gpio_id, uint8_t state)
{
    if (gpio_id < COUNT(fdd_out_pins)) {
        bus_out(fdd_out_pins[gpio_id], state != 0);
    } else if (gpio_id >= 16 && gpio_id < 16 + COUNT(iec_out_pins)) {
        bus_out(iec_out_pins[gpio_id - 16], state != 0);
    } else if (gpio_id >= 32 && gpio_id < 32 + COUNT(led_pins)) {
        led_set(led_pins[gpio_id - 32], state != 0);
    } else {
        return UFI_ERR_NOT_IMPL;
    }
    return UFI_OK;
}

/* ============================================================================
 * DEBUG TIMER
 * ============================================================================ */

timer_status_t ufi_debug_timer_read(void)
{
    timer_status_t status = {0};

    status.tim2_counter = __HAL_TIM_GET_COUNTER(&htim2);
    status.tim2_prescaler = htim2.Instance->PSC;
    status.tim2_period = htim2.Instance->ARR;
    status.sysclk_freq = HAL_RCC_GetSysClockFreq();
    status.hclk_freq = HAL_RCC_GetHCLKFreq();
    status.pclk1_freq = HAL_RCC_GetPCLK1Freq();
    status.uptime_ms = HAL_GetTick();

    return status;
}

/**
 * Index-Puls Timing messen (Zeit zwischen zwei Index-Pulsen = 1 Umdrehung)
 * @return Zeit in Timer-Ticks, 0 bei Timeout
 */
uint32_t ufi_debug_measure_index(void)
{
    const uint32_t start_ms = HAL_GetTick();

    /* wait for the start of an index pulse, twice */
    uint32_t edge[2];
    for (int n = 0; n < 2; n++) {
        while (bus_in(&PIN_FDD_INDEX)) {
            if (HAL_GetTick() - start_ms > 1000) return 0;
        }
        while (!bus_in(&PIN_FDD_INDEX)) {
            if (HAL_GetTick() - start_ms > 1000) return 0;
        }
        edge[n] = __HAL_TIM_GET_COUNTER(&htim2);
    }
    return edge[1] - edge[0];
}

/**
 * RPM aus Index-Timing berechnen
 */
uint16_t ufi_debug_measure_rpm(void)
{
    uint32_t ticks = ufi_debug_measure_index();
    if (ticks == 0) return 0;

    uint64_t rpm = (60ULL * FLUX_TIMER_FREQ) / ticks;
    return (uint16_t)rpm;
}

/* ============================================================================
 * DEBUG MEMORY
 * ============================================================================ */

memory_info_t ufi_debug_memory_read(void)
{
    memory_info_t info = {0};

    info.flash_size = *(volatile uint16_t*)FLASHSIZE_BASE;
    info.unique_id[0] = *(volatile uint32_t*)(UID_BASE);
    info.unique_id[1] = *(volatile uint32_t*)(UID_BASE + 4);
    info.unique_id[2] = *(volatile uint32_t*)(UID_BASE + 8);

    uint32_t idcode = DBGMCU->IDCODE;
    info.device_id = idcode & 0xFFF;
    info.revision = (idcode >> 16) & 0xFFFF;

    info.ram_size = 564;  // KB total (STM32H723)

    return info;
}

/* ============================================================================
 * LED TEST
 * ============================================================================ */

void ufi_debug_led_test(void)
{
    for (unsigned i = 0; i < COUNT(led_pins); i++) {
        led_set(led_pins[i], false);
    }
    HAL_Delay(200);

    /* Sequenz: ACT -> FDD -> USB -> ERR, dann alle aus (Power-LED ist fest an) */
    for (unsigned i = 0; i < COUNT(led_pins); i++) {
        led_set(led_pins[i], true);
        HAL_Delay(200);
    }
    HAL_Delay(300);
    for (unsigned i = 0; i < COUNT(led_pins); i++) {
        led_set(led_pins[i], false);
    }
}

/* ============================================================================
 * SELBSTTEST
 * ============================================================================ */

/**
 * Hardware-Selbsttest
 * @return Bitmask: Bit0=Timer OK, Bit1=DMA OK, Bit2=USB OK, Bit3=GPIO OK
 */
uint8_t ufi_debug_selftest(void)
{
    uint8_t result = 0;

    uint32_t t1 = __HAL_TIM_GET_COUNTER(&htim2);
    HAL_Delay(1);
    uint32_t t2 = __HAL_TIM_GET_COUNTER(&htim2);
    if (t2 != t1) {
        result |= (1 << 0);
    }
    if (__HAL_RCC_DMA1_IS_CLK_ENABLED()) {
        result |= (1 << 1);
    }
    if (__HAL_RCC_USB1_OTG_HS_IS_CLK_ENABLED()) {
        result |= (1 << 2);
    }
    if (__HAL_RCC_GPIOA_IS_CLK_ENABLED() && __HAL_RCC_GPIOD_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOE_IS_CLK_ENABLED() && __HAL_RCC_GPIOF_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOG_IS_CLK_ENABLED()) {
        result |= (1 << 3);
    }
    return result;
}
