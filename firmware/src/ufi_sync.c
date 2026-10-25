/**
 * UFI Flux Engine - SYNC sensor capture on TIM8_CH1 (board v1.0, stage A10c)
 *
 * The Disk II sync sensor (J19 pin 3, Hall sensor in the Applesauce manner) has its own
 * net since A11d: 10k pull-up, Schmitt inverter U26 (74LVC14), PC6 = TIM8_CH1 (AF3), high =
 * sensor pulse.  Before A11d the sensor fed the INDEX bus (wired-OR); v0.x boards keep that
 * path, this module does nothing there (ufi_sync_enable() answers NOT_IMPL).
 *
 * Time base: TIM8 is a 16-bit timer on the same 275 MHz kernel clock as TIM2 (APB2 timer
 * clock = APB1 timer clock, both prescalers 2 -> HCLK), so one TIM8 tick is one TIM2 tick.
 * Two time stamps per pulse:
 *  - last_t32: 32-bit TIM8 time = overflow counter (TIM8_UP interrupt every 238 us) << 16 |
 *    CCR1.  A capture that lands just after an overflow whose interrupt has not run yet is
 *    recognised by UIF pending + CCR1 in the lower half and gets the incremented high word.
 *  - last_t2: the same pulse in the TIM2 flux time base, which is what the capture, the
 *    write start and the evidence record use: the capture ISR reads TIM2->CNT and TIM8->CNT
 *    back to back and subtracts the 16-bit age of CCR1 (ufi_sync_map_t2()).  Valid while the
 *    interrupt latency stays below one TIM8 period (238 us); the ISRs at preemption level 0
 *    (flux index / index simulation / this one, which do not nest) are a few microseconds.
 *    Error budget: the two counter reads are a handful of APB cycles apart (<= 40 ns, a
 *    constant offset, not jitter), U26 adds about 5 ns, the input filter (fCK_INT, N=8) about
 *    30 ns of delay - all well below one 1.8 us Apple bit cell.  The pulse itself is the
 *    sensor's own edge; its repeatability per revolution is the bench measurement
 *    (Bringup list), not something the firmware can state.
 *
 * Index source: with the Apple drive selected and UFI.CFG apple_sync=1, SYNC replaces the
 * INDEX line on v1.0 (ufi_flux_index_source_sync() switches the TIM2 CC2 capture channel off):
 * the capture ISR feeds ufi_flux_index_event() with last_t2.  ufi_sync_service()
 * follows the drive selection in the main loop, ufi_sync_select() does the same at capture
 * start.  Counting can also be switched on by the host (op 0x46) without changing the index
 * source, e.g. to check the sensor on the bench.
 */

#include <string.h>

#include "ufi_firmware.h"

#define SYNC_FILTER     0x3u        /* fCK_INT, N=8: ~30 ns, like the INDEX capture */

typedef struct {
    TIM_HandleTypeDef tim;
    volatile uint32_t ovf;          /* TIM8 overflows since the start */
    volatile uint32_t count, last_t2, last_t32, period;
    bool v10, active, source;
} sync_state_t;

__attribute__((section(".dtcm"), aligned(4))) static sync_state_t ss;

void ufi_sync_init(void)
{
    memset(&ss, 0, sizeof(ss));
    ss.v10 = ufi_board_rev() >= 10u;
}

static int sync_start(void)
{
    if (!ss.v10) {
        return UFI_ERR_NOT_IMPL;
    }
    if (ss.active) {
        return UFI_OK;
    }
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_SYNC.pin;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;                   /* driven by U26 */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF3_TIM8;
    HAL_GPIO_Init(PIN_SYNC.port, &g);

    ss.tim.Instance = TIM8;
    ss.tim.Init.Prescaler = 0;              /* 275 MHz, one tick = one TIM2 tick */
    ss.tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    ss.tim.Init.Period = 0xFFFFu;
    ss.tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    ss.tim.Init.RepetitionCounter = 0;
    ss.tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;    /* start of the pulse (U26 output high) */
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter = SYNC_FILTER;
    if (HAL_TIM_IC_Init(&ss.tim) != HAL_OK ||
        HAL_TIM_IC_ConfigChannel(&ss.tim, &ic, TIM_CHANNEL_1) != HAL_OK) {
        return UFI_ERR_NOT_IMPL;
    }
    ss.ovf = 0;
    ss.count = 0;
    ss.period = 0;
    TIM8->SR = 0;                           /* the init's update event left UIF set */
    __HAL_TIM_ENABLE_IT(&ss.tim, TIM_IT_UPDATE);
    /* same preemption level as the TIM2 index capture and the index simulation */
    HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 0, 3);
    HAL_NVIC_SetPriority(TIM8_CC_IRQn, 0, 3);
    HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
    HAL_NVIC_EnableIRQ(TIM8_CC_IRQn);
    if (HAL_TIM_IC_Start_IT(&ss.tim, TIM_CHANNEL_1) != HAL_OK) {
        HAL_NVIC_DisableIRQ(TIM8_CC_IRQn);
        HAL_NVIC_DisableIRQ(TIM8_UP_TIM13_IRQn);
        return UFI_ERR_NOT_IMPL;
    }
    ss.active = true;
    return UFI_OK;
}

static void sync_stop(void)
{
    if (!ss.active) {
        return;
    }
    HAL_TIM_IC_Stop_IT(&ss.tim, TIM_CHANNEL_1);
    __HAL_TIM_DISABLE_IT(&ss.tim, TIM_IT_UPDATE);
    __HAL_TIM_DISABLE(&ss.tim);
    HAL_NVIC_DisableIRQ(TIM8_CC_IRQn);
    HAL_NVIC_DisableIRQ(TIM8_UP_TIM13_IRQn);
    GPIO_InitTypeDef g = {0};               /* back to the plain input of board_gpio_init() */
    g.Pin = PIN_SYNC.pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIN_SYNC.port, &g);
    ss.active = false;
}

/* overflow counter: high word of the 32-bit TIM8 time */
void TIM8_UP_TIM13_IRQHandler(void)
{
    if (TIM8->SR & TIM_SR_UIF) {
        TIM8->SR = ~TIM_SR_UIF;
        ss.ovf++;
    }
}

void TIM8_CC_IRQHandler(void)
{
    if (!(TIM8->SR & TIM_SR_CC1IF)) {
        TIM8->SR = ~(TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_CC1OF);
        return;
    }
    const uint16_t ccr = (uint16_t)TIM8->CCR1;      /* reading CCR1 clears CC1IF */
    const uint32_t t2 = TIM2->CNT;                  /* back to back with the TIM8 counter */
    const uint16_t t8 = (uint16_t)TIM8->CNT;
    TIM8->SR = ~TIM_SR_CC1OF;
    uint32_t hi = ss.ovf;
    if ((TIM8->SR & TIM_SR_UIF) && ccr < 0x8000u) {
        hi++;                                       /* captured after an overflow whose ISR is still pending */
    }
    const uint32_t t32 = (hi << 16) | ccr;
    const uint32_t t = ufi_sync_map_t2(t2, t8, ccr);
    if (ss.count) {
        ss.period = t32 - ss.last_t32;
    }
    ss.last_t32 = t32;
    ss.last_t2 = t;
    ss.count++;
    if (ss.source) {
        ufi_flux_index_event(t);                    /* counts as the drive's index pulse */
    }
}

static void set_source(bool on)
{
    if (on == ss.source) {
        return;
    }
    if (on && sync_start() != UFI_OK) {
        return;                                     /* v0.x: the wired-OR path on INDEX stays */
    }
    ss.source = on;
    ufi_flux_index_source_sync(on);
}

void ufi_sync_service(void)
{
    if (!ss.v10) {
        return;
    }
    set_source(ufi_config_apple_sync() && ufi_drive_is_apple());
}

void ufi_sync_select(bool apple)
{
    if (!ss.v10) {
        return;
    }
    set_source(apple && ufi_config_apple_sync());
}

int ufi_sync_enable(bool on)
{
    if (!ss.v10) {
        return UFI_ERR_NOT_IMPL;
    }
    if (on) {
        return sync_start();
    }
    if (ss.source) {
        return UFI_ERR_BUSY;                        /* the capture is the index source right now */
    }
    sync_stop();
    return UFI_OK;
}

void ufi_sync_status(ufi_sync_status_t* s)
{
    memset(s, 0, sizeof(*s));
    s->active = ss.active ? 1u : 0u;
    s->source = ss.source ? 1u : 0u;
    s->level = ufi_sync_asserted() ? 1u : 0u;
    s->count = ss.count;
    s->last_t2 = ss.last_t2;
    s->last_t32 = ss.last_t32;
    s->period = ss.period;
}

bool ufi_sync_asserted(void)
{
    return ss.v10 && HAL_GPIO_ReadPin(PIN_SYNC.port, PIN_SYNC.pin) == GPIO_PIN_SET;
}
