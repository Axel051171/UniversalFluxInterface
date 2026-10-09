/**
 * UFI Flux Engine - drive diagnostics and index simulation (firmware 1.13)
 *
 * - ufi_diag_rpm():  revolution time min/avg/max and index pulse width over n revolutions
 *                    of the selected drive (TIM2 time base, polled index input)
 * - ufi_diag_scan(): which drives answer: TRK0 found by a recalibration, index pulses
 *                    (= disk inserted and spinning), write protect
 * - index simulation: open-drain index pulses on EXP_IO2 (J9 pin 6, PE1) at 300 / 360 rpm,
 *                    wired to the index sensor output of a drive (flippy disks: the back
 *                    side has no index hole, PC drives refuse to write without index).
 *                    PE1 is also the SD position of the mode switch on J13: the simulation
 *                    is refused while that input is high and the switch reads it as low.
 *                    TIM7, 10 us per tick, interrupt toggles the pin.
 */

#include <string.h>
#include "ufi_firmware.h"

#define INDEX_WAIT_MS       600u        /* > 1 revolution at 300 rpm with margin */
#define SIM_TICK_HZ         100000u     /* TIM7: 10 us per tick */
#define SIM_PULSE_US_DEF    2000u

extern TIM_HandleTypeDef htim2;

/* ============================================================================
 * REVOLUTION TIME
 * ============================================================================ */

/* wait for the level of the index input (or the internal simulation); false on timeout */
static bool wait_index(bool asserted, uint32_t t0_ms)
{
    while (ufi_flux_index_asserted() != asserted) {
        if (HAL_GetTick() - t0_ms > INDEX_WAIT_MS) {
            return false;
        }
    }
    return true;
}

int ufi_diag_rpm(uint8_t revs, diag_rpm_t* r)
{
    memset(r, 0, sizeof(*r));
    const drive_type_t d = ufi_drive_get_current();
    if (d == DRIVE_NONE || d == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    if (ufi_drive_is_apple() && !ufi_config_apple_sync()) {
        return UFI_ERR_NO_INDEX;                /* Disk II: no index without sync sensor */
    }
    if (revs == 0) revs = 5;
    if (revs > 50) revs = 50;
    if (!ufi_drive_get_status().motor_on) {
        ufi_drive_motor(true);                  /* waits the spin-up time */
    }

    uint64_t sum = 0, width_sum = 0;
    uint32_t start = 0;
    r->period_min = 0xFFFFFFFFu;
    for (uint8_t n = 0; n <= revs; n++) {       /* revs + 1 pulse starts */
        if (!wait_index(false, HAL_GetTick()) || !wait_index(true, HAL_GetTick())) {
            return n < 2u ? UFI_ERR_NO_INDEX : UFI_ERR_TIMEOUT;
        }
        const uint32_t t = __HAL_TIM_GET_COUNTER(&htim2);
        if (!wait_index(false, HAL_GetTick())) {
            return UFI_ERR_TIMEOUT;             /* index stuck asserted */
        }
        width_sum += __HAL_TIM_GET_COUNTER(&htim2) - t;
        if (n > 0) {
            const uint32_t p = t - start;
            sum += p;
            if (p < r->period_min) r->period_min = p;
            if (p > r->period_max) r->period_max = p;
            r->revs = n;
        }
        start = t;
    }
    r->period_avg = (uint32_t)(sum / r->revs);
    r->pulse_avg = (uint32_t)(width_sum / (r->revs + 1u));
    r->rpm_x100 = (uint16_t)((6000ull * FLUX_TIMER_FREQ + r->period_avg / 2u) / r->period_avg);
    return UFI_OK;
}

/* ============================================================================
 * DRIVE SCAN
 * ============================================================================ */

static uint8_t probe(drive_type_t t)
{
    if (ufi_drive_select(t) != UFI_OK) {
        return 0;
    }
    uint8_t f = 0;
    ufi_drive_motor(true);
    if (ufi_drive_recalibrate() == UFI_OK) {
        f |= DIAG_TRACK0;
    }
    const uint32_t t0 = HAL_GetTick();
    if (wait_index(true, t0) && wait_index(false, t0) && wait_index(true, t0)) {
        f |= DIAG_INDEX;                        /* two pulse starts: disk spinning */
    }
    if (ufi_drive_write_protected()) {
        f |= DIAG_WPROT;
    }
    ufi_drive_motor(false);
    return f;
}

int ufi_diag_scan(bool shugart_bus, uint8_t* out, uint8_t* n)
{
    static const drive_type_t pc[] = {DRIVE_SHUGART_A, DRIVE_SHUGART_B, DRIVE_AMIGA,
                                      DRIVE_AMIGA2};
    static const drive_type_t bus[] = {DRIVE_SHUGART_DS0, DRIVE_SHUGART_DS1,
                                       DRIVE_SHUGART_DS2, DRIVE_SHUGART_DS3};
    const drive_type_t* list = shugart_bus ? bus : pc;
    const drive_type_t prev = ufi_drive_get_current();

    *n = 0;
    for (uint8_t i = 0; i < 4u; i++) {
        out[(*n)++] = (uint8_t)list[i];
        out[(*n)++] = probe(list[i]);
    }
    ufi_drive_select(DRIVE_NONE);
    ufi_drive_select(prev);
    return UFI_OK;
}

/* ============================================================================
 * INDEX SIMULATION
 * ============================================================================ */

static TIM_HandleTypeDef htim7;
static volatile uint16_t sim_rpm, sim_low_ticks, sim_high_ticks;
static volatile uint8_t sim_mode;           /* INDEX_SIM_PIN / INDEX_SIM_INTERNAL */
static volatile bool sim_pulse;

static void sim_pin(bool output)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_EXP_IO2.pin;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    if (output) {
        HAL_GPIO_WritePin(PIN_EXP_IO2.port, PIN_EXP_IO2.pin, GPIO_PIN_SET);   /* released */
        g.Mode = GPIO_MODE_OUTPUT_OD;
        g.Pull = GPIO_NOPULL;
    } else {
        g.Mode = GPIO_MODE_INPUT;               /* mode switch input again (board init) */
        g.Pull = GPIO_PULLDOWN;
    }
    HAL_GPIO_Init(PIN_EXP_IO2.port, &g);
}

void TIM7_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim7, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
        sim_pulse = !sim_pulse;                 /* pulse = output low (index asserted) */
        if (sim_mode & INDEX_SIM_PIN) {
            HAL_GPIO_WritePin(PIN_EXP_IO2.port, PIN_EXP_IO2.pin,
                              sim_pulse ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        __HAL_TIM_SET_AUTORELOAD(&htim7, (sim_pulse ? sim_low_ticks : sim_high_ticks) - 1u);
        if (sim_pulse && (sim_mode & INDEX_SIM_INTERNAL)) {
            ufi_flux_index_event(TIM2->CNT);    /* counts as the drive's index pulse */
        }
    }
}

int ufi_index_sim_set(uint16_t rpm, uint16_t pulse_us, uint8_t mode)
{
    if (rpm == 0) {
        if (sim_rpm) {
            HAL_TIM_Base_Stop_IT(&htim7);
            HAL_NVIC_DisableIRQ(TIM7_IRQn);
            ufi_flux_index_source(false);
            if (sim_mode & INDEX_SIM_PIN) {
                sim_pin(false);
            }
            sim_rpm = 0;
            sim_mode = 0;
            sim_pulse = false;
        }
        return UFI_OK;
    }
    if (mode == 0) mode = INDEX_SIM_PIN;
    if (mode & ~(INDEX_SIM_PIN | INDEX_SIM_INTERNAL)) {
        return UFI_ERR_BAD_ARGS;
    }
    if (rpm < 200u || rpm > 400u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (pulse_us == 0) pulse_us = SIM_PULSE_US_DEF;
    const uint32_t period = 60u * SIM_TICK_HZ / rpm;        /* ticks per revolution */
    const uint32_t low = pulse_us / (1000000u / SIM_TICK_HZ);
    if (low < 1u || low >= period / 2u) {
        return UFI_ERR_BAD_ARGS;
    }
    const bool pin_was = (sim_mode & INDEX_SIM_PIN) != 0, pin_now = (mode & INDEX_SIM_PIN) != 0;
    if (pin_now && !pin_was && HAL_GPIO_ReadPin(PIN_EXP_IO2.port, PIN_EXP_IO2.pin) == GPIO_PIN_SET) {
        return UFI_ERR_BUSY;                    /* mode switch on J13 in the SD position */
    }

    if (sim_rpm) {
        HAL_TIM_Base_Stop_IT(&htim7);           /* re-program a running simulation */
    }
    sim_low_ticks = (uint16_t)low;
    sim_high_ticks = (uint16_t)(period - low);
    sim_pulse = false;
    if (pin_now != pin_was) {
        sim_pin(pin_now);
    }
    sim_mode = mode;
    ufi_flux_index_source((mode & INDEX_SIM_INTERNAL) != 0);
    __HAL_RCC_TIM7_CLK_ENABLE();
    htim7.Instance = TIM7;                      /* APB1 timer clock = TIM2 clock */
    htim7.Init.Prescaler = FLUX_TIMER_FREQ / SIM_TICK_HZ - 1u;
    htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim7.Init.Period = sim_high_ticks - 1u;
    htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim7) != HAL_OK) {
        sim_pin(false);
        return UFI_ERR_NOT_IMPL;
    }
    __HAL_TIM_CLEAR_FLAG(&htim7, TIM_FLAG_UPDATE);
    /* same preemption level as the TIM2 index capture: above the flux DMA ISR */
    HAL_NVIC_SetPriority(TIM7_IRQn, 0, 3);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    sim_rpm = rpm;
    HAL_TIM_Base_Start_IT(&htim7);
    return UFI_OK;
}

uint16_t ufi_index_sim_rpm(void)
{
    return sim_rpm;
}

uint8_t ufi_index_sim_mode(void)
{
    return sim_mode;
}

bool ufi_index_sim_pulse(void)
{
    return sim_rpm && sim_pulse;
}
