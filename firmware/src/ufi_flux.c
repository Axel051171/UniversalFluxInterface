/**
 * UFI Flux Engine - Flux Capture Module
 *
 * TIM2 runs free at 275 MHz (32 bit):
 *   CH1 = RDATA  -> input capture, DMA1_Stream0 writes every timestamp into the flux store
 *   CH2 = INDEX  -> input capture interrupt, records (timestamp, DMA position) per index
 * Both channels share one time base, so revolution boundaries are exact.
 *
 * The DMA writes linearly into one AXI SRAM buffer (DMA1 cannot reach DTCM); revolutions are
 * slices of that buffer, no copying.  After completion the slices are finalised in place:
 * boundaries are corrected by timestamp and samples made relative to their index pulse.
 */

#include "ufi_firmware.h"

extern capture_context_t g_capture;

/* ============================================================================
 * TIMER, DMA, BUFFER
 * ============================================================================ */

TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2;

__attribute__((section(".axi_sram"), aligned(32)))
static uint32_t flux_store[FLUX_STORE_WORDS];

static flux_revolution_t revs[REVOLUTIONS_BUFFER];
static volatile uint32_t idx_time[REVOLUTIONS_BUFFER + 1];
static volatile uint32_t idx_pos[REVOLUTIONS_BUFFER + 1];
static volatile uint8_t idx_count;      /* index pulses seen since capture start */
static volatile uint32_t end_pos;       /* samples written when the capture stopped */
static bool finalised;

#define INDEX_FILTER    0x3     /* fCK_INT, N=8: ~30 ns glitch filter, index pulses are >1 us */

static inline uint32_t dma_pos(void)
{
    return FLUX_STORE_WORDS - __HAL_DMA_GET_COUNTER(&hdma_tim2);
}

static void flux_dma_full(DMA_HandleTypeDef* hdma);
static void flux_dma_error(DMA_HandleTypeDef* hdma);

/* ============================================================================
 * FLUX TIMER INITIALISIERUNG
 * ============================================================================ */

void ufi_flux_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&htim2) != HAL_OK) {
        Error_Handler();
    }

    TIM_IC_InitTypeDef ic = {0};
    ic.ICPolarity = FLUX_EDGE;              /* rising: LVC14 inverts the bus */
    ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic.ICPrescaler = TIM_ICPSC_DIV1;
    ic.ICFilter = 0;                        /* RDATA: no filter, full resolution */
    HAL_TIM_IC_ConfigChannel(&htim2, &ic, FLUX_RDATA_CHANNEL);
    ic.ICFilter = INDEX_FILTER;
    HAL_TIM_IC_ConfigChannel(&htim2, &ic, FLUX_INDEX_CHANNEL);

    hdma_tim2.Instance = DMA1_Stream0;
    hdma_tim2.Init.Request = DMA_REQUEST_TIM2_CH1;
    hdma_tim2.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_tim2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim2.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_tim2.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_tim2.Init.Mode = DMA_NORMAL;       /* linear: stop when the store is full */
    hdma_tim2.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_tim2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_tim2) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(&htim2, hdma[TIM_DMA_ID_CC1], hdma_tim2);
    hdma_tim2.XferCpltCallback = flux_dma_full;
    hdma_tim2.XferErrorCallback = flux_dma_error;

    /* Free-running counter, both capture channels enabled; DMA/IRQ armed per capture */
    HAL_TIM_IC_Start(&htim2, FLUX_RDATA_CHANNEL);
    HAL_TIM_IC_Start(&htim2, FLUX_INDEX_CHANNEL);

    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

    g_capture.state = CAPTURE_IDLE;
}

uint32_t ufi_flux_now(void)
{
    return TIM2->CNT;
}

uint32_t* ufi_flux_store(uint32_t* words)
{
    if (words) {
        *words = FLUX_STORE_WORDS;
    }
    return flux_store;
}

/* ============================================================================
 * CAPTURE STARTEN / STOPPEN
 * ============================================================================ */

int ufi_flux_capture_start(uint8_t revolutions)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (revolutions == 0 || revolutions > REVOLUTIONS_BUFFER) {
        return UFI_ERR_BUFFER_FULL;
    }

    g_capture.revolutions_requested = revolutions;
    g_capture.revolutions_captured = 0;
    g_capture.error_code = 0;
    g_capture.buffer = revs;
    idx_count = 0;
    end_pos = 0;
    finalised = false;

    /* Dirty lines from the write path must not be evicted over fresh DMA data */
    SCB_CleanInvalidateDCache_by_Addr(flux_store, sizeof(flux_store));

    /* Arm the stream; transfers only start once CC1DE is set at the first index */
    TIM2->DIER &= ~(TIM_DIER_CC1DE | TIM_DIER_CC2IE);
    HAL_DMA_Abort(&hdma_tim2);
    if (HAL_DMA_Start_IT(&hdma_tim2, (uint32_t)&TIM2->CCR1, (uint32_t)flux_store,
                         FLUX_STORE_WORDS) != HAL_OK) {
        return UFI_ERR_DMA;
    }

    g_capture.state = CAPTURE_WAITING_INDEX;
    TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    TIM2->DIER |= TIM_DIER_CC2IE;
    return UFI_OK;
}

static void capture_halt(void)
{
    TIM2->DIER &= ~(TIM_DIER_CC1DE | TIM_DIER_CC2IE);
    end_pos = dma_pos();
    HAL_DMA_Abort_IT(&hdma_tim2);
}

int ufi_flux_capture_stop(void)
{
    capture_halt();
    g_capture.state = CAPTURE_IDLE;
    return UFI_OK;
}

/* ============================================================================
 * INTERRUPTS
 * ============================================================================ */

void ufi_flux_tim2_irq(void)
{
    if (!(TIM2->SR & TIM_SR_CC2IF)) {
        return;
    }
    const uint32_t t = TIM2->CCR2;          /* reading CCR2 clears CC2IF */
    TIM2->SR = ~TIM_SR_CC2OF;

    write_state_t ws = ufi_write_get_state();
    if (ws == WRITE_WAITING_INDEX || ws == WRITE_ACTIVE) {
        ufi_write_index_handler(t);
        return;
    }

    if (g_capture.state == CAPTURE_WAITING_INDEX) {
        idx_time[0] = t;
        idx_pos[0] = 0;
        idx_count = 1;
        TIM2->DIER |= TIM_DIER_CC1DE;       /* start streaming RDATA timestamps */
        g_capture.state = CAPTURE_RUNNING;
    } else if (g_capture.state == CAPTURE_RUNNING) {
        idx_time[idx_count] = t;
        idx_pos[idx_count] = dma_pos();
        idx_count++;
        if (idx_count > g_capture.revolutions_requested) {
            capture_halt();
            g_capture.state = CAPTURE_COMPLETE;
        }
    } else {
        TIM2->DIER &= ~TIM_DIER_CC2IE;      /* stray index while idle */
    }
}

void ufi_flux_dma_irq(void)
{
    HAL_DMA_IRQHandler(&hdma_tim2);
}

/* Flux store full before all revolutions were captured: keep the complete ones */
static void flux_dma_full(DMA_HandleTypeDef* hdma)
{
    (void)hdma;
    if (g_capture.state != CAPTURE_RUNNING) {
        return;
    }
    TIM2->DIER &= ~(TIM_DIER_CC1DE | TIM_DIER_CC2IE);
    end_pos = FLUX_STORE_WORDS;
    g_capture.error_code = 1;               /* overflow: fewer revolutions than requested */
    g_capture.state = (idx_count >= 2) ? CAPTURE_COMPLETE : CAPTURE_ERROR;
}

static void flux_dma_error(DMA_HandleTypeDef* hdma)
{
    (void)hdma;
    TIM2->DIER &= ~(TIM_DIER_CC1DE | TIM_DIER_CC2IE);
    g_capture.error_code = 2;
    g_capture.state = CAPTURE_ERROR;
}

/* ============================================================================
 * FINALISIEREN (Thread-Kontext)
 * ============================================================================ */

/* Move a DMA-position boundary so it splits exactly at index time t */
static uint32_t align_boundary(uint32_t pos, uint32_t t, uint32_t lo, uint32_t hi)
{
    while (pos > lo && (int32_t)(flux_store[pos - 1] - t) >= 0) {
        pos--;
    }
    while (pos < hi && (int32_t)(flux_store[pos] - t) < 0) {
        pos++;
    }
    return pos;
}

capture_state_t ufi_flux_poll(void)
{
    if (g_capture.state != CAPTURE_COMPLETE || finalised) {
        return g_capture.state;
    }

    const uint32_t used = end_pos;
    SCB_InvalidateDCache_by_Addr(flux_store, (int32_t)(((used * 4u) + 31u) & ~31u));

    uint8_t n = idx_count - 1;              /* full revolutions */
    if (n > g_capture.revolutions_requested) {
        n = g_capture.revolutions_requested;
    }

    uint32_t b[REVOLUTIONS_BUFFER + 1];
    b[0] = 0;
    for (uint8_t r = 1; r <= n; r++) {
        b[r] = align_boundary(idx_pos[r], idx_time[r], b[r - 1], used);
    }
    for (uint8_t r = 0; r < n; r++) {
        for (uint32_t i = b[r]; i < b[r + 1]; i++) {
            flux_store[i] -= idx_time[r];   /* ticks since this revolution's index */
        }
        revs[r].samples = (flux_sample_t*)&flux_store[b[r]];
        revs[r].count = b[r + 1] - b[r];
        revs[r].index_time = idx_time[r + 1] - idx_time[r];
        revs[r].revolution = r;
    }

    g_capture.revolutions_captured = n;
    finalised = true;
    return g_capture.state;
}

/* ============================================================================
 * FLUX-DATEN AUSLESEN
 * ============================================================================ */

flux_revolution_t* ufi_flux_get_revolution(uint8_t index)
{
    ufi_flux_poll();
    if (!finalised || index >= g_capture.revolutions_captured) {
        return NULL;
    }
    return &revs[index];
}

uint8_t ufi_flux_get_revolution_count(void)
{
    ufi_flux_poll();
    return finalised ? g_capture.revolutions_captured : 0;
}

/* ============================================================================
 * FLUX-STATISTIKEN
 * ============================================================================ */

typedef struct {
    uint32_t min_delta;
    uint32_t max_delta;
    uint32_t avg_delta;
    uint32_t total_flux;
    uint32_t duration_ticks;
    float rpm;
} flux_stats_t;

flux_stats_t ufi_flux_calculate_stats(const flux_revolution_t* rev)
{
    flux_stats_t stats = {0};

    if (!rev || rev->count < 2) {
        return stats;
    }

    stats.min_delta = 0xFFFFFFFF;
    uint64_t sum = 0;
    for (uint32_t i = 1; i < rev->count; i++) {
        uint32_t delta = rev->samples[i].timestamp - rev->samples[i - 1].timestamp;
        if (delta < stats.min_delta) stats.min_delta = delta;
        if (delta > stats.max_delta) stats.max_delta = delta;
        sum += delta;
    }

    stats.total_flux = rev->count;
    stats.duration_ticks = rev->index_time;
    stats.avg_delta = (uint32_t)(sum / (rev->count - 1));
    if (stats.duration_ticks > 0) {
        stats.rpm = 60.0f * (float)FLUX_TIMER_FREQ / (float)stats.duration_ticks;
    }
    return stats;
}
