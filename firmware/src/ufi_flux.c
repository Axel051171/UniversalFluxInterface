/**
 * UFI Flux Engine - Flux Capture Module
 *
 * TIM2 runs free at 275 MHz (32 bit):
 *   CH1 = RDATA  -> input capture, DMA1_Stream0 writes every timestamp into the flux store
 *   CH2 = INDEX  -> input capture interrupt, records (timestamp, DMA position) per index
 * Both channels share one time base, so revolution boundaries are exact.
 *
 * Flux store: 8 MB QSPI PSRAM (memory-mapped) or a 224 KB AXI SRAM fallback; DMA1 cannot
 * reach DTCM.  A DMA stream counts at most 65535 items, so the store is filled in chunks
 * by the stream's double-buffer mode: while one chunk fills, the ISR points the idle
 * memory register at the next one.  Chunks are contiguous, so the store stays one linear
 * array; revolutions are slices of it.  After completion the slices are finalised in
 * place: boundaries corrected by timestamp, samples made relative to their index pulse.
 */

#include "ufi_firmware.h"

extern capture_context_t g_capture;

/* ============================================================================
 * TIMER, DMA, BUFFER
 * ============================================================================ */

TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2;

__attribute__((section(".axi_sram"), aligned(32)))
static uint32_t flux_fallback[FLUX_STORE_FALLBACK_WORDS];

/* overrun target after the last chunk, until the DMA ISR stops the stream */
__attribute__((section(".axi_sram"), aligned(32)))
static uint32_t dma_scratch[256];

static uint32_t* flux_store = flux_fallback;
static uint32_t store_words = FLUX_STORE_FALLBACK_WORDS;
static bool store_psram;

#define CHUNK_MAX_WORDS 32768u      /* 128 KB per DMA buffer (NDTR is 16 bit) */
static uint32_t chunk_words;
static uint32_t n_chunks;
static volatile uint32_t chunks_done;   /* completed DMA buffers */
static volatile uint32_t next_chunk;    /* next chunk to hand to the idle memory register */

static flux_revolution_t revs[REVOLUTIONS_BUFFER];
static volatile uint32_t idx_time[REVOLUTIONS_STREAM + 1];
static volatile uint32_t idx_pos[REVOLUTIONS_STREAM + 1];
static volatile uint8_t idx_count;      /* index pulses seen since capture start */
static volatile uint32_t end_pos;       /* samples written when the capture stopped */
static bool finalised;
static volatile bool streaming;         /* samples are read live, store stays untouched */
static volatile uint32_t last_index_ms;  /* HAL tick of capture start / last index pulse */

/* Streaming turns the store into a ring: logical sample n lives at n % store_words, and
 * a chunk may be reused once the streamer has read everything it held. */
static volatile uint32_t stream_consumed;
static volatile bool stream_scratch;    /* no free chunk: the DMA runs into dma_scratch */

/* Index-less capture (no index hole/sensor, flippy side, hard-sectored): revolution
 * boundaries every `period` ticks from the capture start instead of index pulses */
static uint32_t period;

/* Flux input: RDATA on TIM2_CH1 (34-pin / Amiga) or Apple RDDATA on TIM2_CH3 (v0.7);
 * both share the counter, only the DMA request and source register change */
static uint32_t rd_de = TIM_DIER_CC1DE;
static volatile uint32_t* rd_ccr = &TIM2->CCR1;

void ufi_flux_select_apple(bool apple)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return;
    }
    TIM2->DIER &= ~(TIM_DIER_CC1DE | TIM_DIER_CC3DE);
    rd_de = apple ? TIM_DIER_CC3DE : TIM_DIER_CC1DE;
    rd_ccr = apple ? &TIM2->CCR3 : &TIM2->CCR1;
    /* DMA1_Stream0 <-> DMAMUX1 channel 0 */
    DMAMUX1_Channel0->CCR = (DMAMUX1_Channel0->CCR & ~DMAMUX_CxCR_DMAREQ_ID) |
                            (apple ? DMA_REQUEST_TIM2_CH3 : DMA_REQUEST_TIM2_CH1);
}

/* No disk or motor off: no index within this time ends the capture (error_code 3) */
#define INDEX_TIMEOUT_MS    600     /* > 1 revolution at 300 rpm (200 ms) with margin */

#define INDEX_FILTER    0x3     /* fCK_INT, N=8: ~30 ns glitch filter, index pulses are >1 us */

static void dma_stop(void);

/* Samples written so far.  Called from the TIM2 ISR (priority above the DMA ISR), so a
 * pending, not yet serviced transfer-complete is accounted for here. */
static uint32_t dma_pos(void)
{
    const uint32_t tc1 = DMA1->LISR & DMA_LISR_TCIF0;
    uint32_t ndtr = DMA1_Stream0->NDTR;
    const uint32_t tc2 = DMA1->LISR & DMA_LISR_TCIF0;
    if (tc1 != tc2) {
        ndtr = DMA1_Stream0->NDTR;
    }
    if (ndtr == 0) {
        ndtr = chunk_words;
    }
    return (chunks_done + (tc2 ? 1u : 0u)) * chunk_words + (chunk_words - ndtr);
}

/* ============================================================================
 * FLUX TIMER INITIALISIERUNG
 * ============================================================================ */

void ufi_flux_init(void)
{
    if (ufi_psram_init() == UFI_OK) {
        flux_store = (uint32_t*)OCTOSPI1_BASE;
        store_words = PSRAM_SIZE_BYTES / sizeof(uint32_t);
        store_psram = true;
    }
    chunk_words = (store_words / 2u < CHUNK_MAX_WORDS) ? store_words / 2u : CHUNK_MAX_WORDS;
    n_chunks = store_words / chunk_words;

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
    HAL_TIM_IC_ConfigChannel(&htim2, &ic, TIM_CHANNEL_3);  /* v0.7 Apple RDDATA (PA2) */
    ic.ICFilter = INDEX_FILTER;
    HAL_TIM_IC_ConfigChannel(&htim2, &ic, FLUX_INDEX_CHANNEL);

    hdma_tim2.Instance = DMA1_Stream0;
    hdma_tim2.Init.Request = DMA_REQUEST_TIM2_CH1;
    hdma_tim2.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_tim2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim2.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_tim2.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_tim2.Init.Mode = DMA_NORMAL;       /* double-buffer mode is enabled per capture */
    hdma_tim2.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_tim2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_tim2) != HAL_OK) {
        Error_Handler();
    }

    /* Free-running counter, both capture channels enabled; DMA/IRQ armed per capture */
    HAL_TIM_IC_Start(&htim2, FLUX_RDATA_CHANNEL);
    HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_3);
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
        *words = store_words;
    }
    return flux_store;
}

bool ufi_flux_store_is_psram(void)
{
    return store_psram;
}

/* ============================================================================
 * DMA (direct register control, double-buffer mode)
 * ============================================================================ */

static void dma_stop(void)
{
    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream0->CR & DMA_SxCR_EN) {
    }
    DMA1->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0 |
                  DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;
}

static void dma_arm(void)
{
    dma_stop();
    chunks_done = 0;
    next_chunk = 2;
    DMA1_Stream0->PAR = (uint32_t)rd_ccr;
    DMA1_Stream0->M0AR = (uint32_t)&flux_store[0];
    DMA1_Stream0->M1AR = (uint32_t)&flux_store[chunk_words];
    DMA1_Stream0->NDTR = chunk_words;
    DMA1_Stream0->CR = (DMA1_Stream0->CR & ~(DMA_SxCR_CT | DMA_SxCR_CIRC)) |
                       DMA_SxCR_DBM | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    DMA1_Stream0->CR |= DMA_SxCR_EN;    /* idle until TIM2 issues CC1 requests */
}

/* ============================================================================
 * CAPTURE STARTEN / STOPPEN
 * ============================================================================ */

int ufi_flux_capture_start(uint8_t revolutions, uint32_t period_ticks)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (revolutions == 0 || revolutions > REVOLUTIONS_STREAM) {
        return UFI_ERR_BUFFER_FULL;
    }
    period = period_ticks;
    stream_consumed = 0;
    stream_scratch = false;

    g_capture.revolutions_requested = revolutions;
    g_capture.revolutions_captured = 0;
    g_capture.error_code = 0;
    g_capture.buffer = revs;
    idx_count = 0;
    end_pos = 0;
    finalised = false;
    last_index_ms = HAL_GetTick();

    /* Dirty lines (write path, earlier finalise) must not be evicted over fresh DMA data */
    SCB_CleanInvalidateDCache();

    /* Arm the stream; transfers only start once CC1DE is set at the first index */
    TIM2->DIER &= ~(rd_de | TIM_DIER_CC2IE);
    dma_arm();

    if (period) {                           /* index-less: revolution 0 starts now */
        __disable_irq();
        idx_time[0] = TIM2->CNT;
        idx_pos[0] = 0;
        idx_count = 1;
        TIM2->DIER |= rd_de;
        g_capture.state = CAPTURE_RUNNING;
        __enable_irq();
        return UFI_OK;
    }

    g_capture.state = CAPTURE_WAITING_INDEX;
    TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    TIM2->DIER |= TIM_DIER_CC2IE;
    return UFI_OK;
}

static void capture_halt(void)
{
    TIM2->DIER &= ~(rd_de | TIM_DIER_CC2IE);
    end_pos = dma_pos();
    dma_stop();
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

static volatile bool index_internal;    /* index pulses come from the simulation (ufi_diag.c) */

void ufi_flux_index_source(bool internal)
{
    index_internal = internal;
}

bool ufi_flux_index_asserted(void)
{
    return index_internal ? ufi_index_sim_pulse() : bus_in(&PIN_FDD_INDEX);
}

void ufi_flux_tim2_irq(void)
{
    if (!(TIM2->SR & TIM_SR_CC2IF)) {
        return;
    }
    const uint32_t t = TIM2->CCR2;          /* reading CCR2 clears CC2IF */
    TIM2->SR = ~TIM_SR_CC2OF;
    if (index_internal) {
        return;                             /* the drive's INDEX line is ignored */
    }
    ufi_flux_index_event(t);
}

/* One index pulse at flux-timer time t: from the TIM2 capture or from the internal index
 * simulation (TIM7 ISR at the same preemption level, so dma_pos() stays consistent) */
void ufi_flux_index_event(uint32_t t)
{
    write_state_t ws = ufi_write_get_state();
    if (ws == WRITE_WAITING_INDEX || ws == WRITE_ACTIVE) {
        ufi_write_index_handler(t);
        return;
    }

    if (g_capture.state == CAPTURE_WAITING_INDEX) {
        idx_time[0] = t;
        idx_pos[0] = 0;
        idx_count = 1;
        last_index_ms = HAL_GetTick();
        TIM2->DIER |= rd_de;       /* start streaming RDATA timestamps */
        g_capture.state = CAPTURE_RUNNING;
    } else if (g_capture.state == CAPTURE_RUNNING && !period) {
        idx_time[idx_count] = t;
        idx_pos[idx_count] = dma_pos();
        last_index_ms = HAL_GetTick();
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
    const uint32_t isr = DMA1->LISR;

    if (isr & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0)) {
        DMA1->LIFCR = DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0;
        TIM2->DIER &= ~(rd_de | TIM_DIER_CC2IE);
        dma_stop();
        g_capture.error_code = 2;
        g_capture.state = CAPTURE_ERROR;
        return;
    }
    if (!(isr & DMA_LISR_TCIF0)) {
        return;
    }
    DMA1->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0;
    chunks_done++;

    if (stream_scratch || (!streaming && chunks_done >= n_chunks)) {
        /* Store full before all revolutions were captured: keep the complete ones */
        TIM2->DIER &= ~(rd_de | TIM_DIER_CC2IE);
        dma_stop();
        if (g_capture.state == CAPTURE_RUNNING) {
            end_pos = chunks_done * chunk_words;    /* logical: == store_words without ring */
            g_capture.error_code = 1;       /* overflow: fewer revolutions than requested */
            g_capture.state = (idx_count >= 2) ? CAPTURE_COMPLETE : CAPTURE_ERROR;
        }
        return;
    }

    /* The buffer that just completed is idle now: point it at the next chunk, or at a
     * scratch area once the store is used up (the stream keeps running until the ISR
     * after the last chunk stops it, and must not wrap into chunk n-2). */
    uint32_t* next = dma_scratch;
    if (streaming) {
        /* ring: logical chunk c overwrites chunk c - n_chunks, which must be read already */
        if (next_chunk < n_chunks || stream_consumed >= (next_chunk - n_chunks + 1u) * chunk_words) {
            next = &flux_store[(next_chunk % n_chunks) * chunk_words];
            next_chunk++;
        } else {
            stream_scratch = true;          /* streamer too slow: stop after this buffer */
        }
    } else if (next_chunk < n_chunks) {
        next = &flux_store[next_chunk * chunk_words];
        next_chunk++;
    }
    if (DMA1_Stream0->CR & DMA_SxCR_CT) {
        DMA1_Stream0->M0AR = (uint32_t)next;        /* M1 active, M0 finished */
    } else {
        DMA1_Stream0->M1AR = (uint32_t)next;
    }
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
    if (period && g_capture.state == CAPTURE_RUNNING) {
        /* index-less: record the boundaries that have passed, stop after the last one */
        const uint8_t seen = ufi_flux_index_count();
        while (idx_count < seen) {
            __disable_irq();
            idx_time[idx_count] = idx_time[0] + (uint32_t)idx_count * period;
            idx_pos[idx_count] = dma_pos();
            idx_count++;
            if (idx_count > g_capture.revolutions_requested && g_capture.state == CAPTURE_RUNNING) {
                capture_halt();
                g_capture.state = CAPTURE_COMPLETE;
            }
            __enable_irq();
        }
    }
    if (!period && (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) &&
        HAL_GetTick() - last_index_ms > INDEX_TIMEOUT_MS) {
        capture_halt();
        g_capture.error_code = 3;           /* no index pulse: no disk / motor off */
        g_capture.state = CAPTURE_ERROR;
    }
    if (g_capture.state != CAPTURE_COMPLETE || finalised || streaming) {
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
 * LIVE ACCESS (streaming, thread context)
 * ============================================================================ */

void ufi_flux_set_streaming(bool on)
{
    streaming = on;
}

/* Samples stored so far.  The store holds absolute TIM2 timestamps while streaming. */
uint32_t ufi_flux_written(void)
{
    uint32_t pos = 0;
    __disable_irq();                        /* chunks_done / NDTR / state must agree */
    const capture_state_t s = g_capture.state;
    if (s == CAPTURE_RUNNING) {
        pos = dma_pos();
    } else if (s == CAPTURE_COMPLETE || s == CAPTURE_ERROR) {
        pos = end_pos;
    }
    __enable_irq();
    return pos;
}

/* Index pulses up to now.  Index-less: boundaries that have passed by the timer, so the
 * streamer sees them as early as real pulses (poll() records them later). */
uint8_t ufi_flux_index_count(void)
{
    if (period && g_capture.state == CAPTURE_RUNNING && idx_count > 0) {
        const uint32_t last = idx_time[0] + (uint32_t)(idx_count - 1u) * period;
        uint32_t n = idx_count + (TIM2->CNT - last) / period;
        const uint32_t cap = (uint32_t)g_capture.revolutions_requested + 1u;
        return (uint8_t)((n > cap) ? cap : n);
    }
    return idx_count;
}

uint32_t ufi_flux_index_time(uint8_t k)
{
    if (period) {
        return idx_time[0] + (uint32_t)k * period;
    }
    return (k <= REVOLUTIONS_STREAM) ? idx_time[k] : 0;
}

void ufi_flux_stream_consumed(uint32_t samples)
{
    stream_consumed = samples;
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
