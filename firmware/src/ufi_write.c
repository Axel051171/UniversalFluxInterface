/**
 * UFI Flux Engine - Write Support
 *
 * Flux-basiertes Schreiben für Disk-Erstellung und Kopien.
 *
 * WDATA = TIM3_CH1 on PA6 (AF2), 275 MHz, same tick as the read timer:
 *  - TIM3 counts DOWN in PWM mode 1 with active-low output: the channel is active
 *    while CNT <= CCR1, so every period ends with a WRITE_PULSE_TICKS pulse.
 *  - Period k = flux interval k.  ARR is preloaded; DMA1_Stream1 (TIM3_UP request)
 *    writes the next interval into ARR on every update.
 *  - Started from the index interrupt; stopped after the last interval, WGATE drops
 *    right away.  The next index releases WGATE as a safety net.
 * Host deltas are TIM2/TIM3 ticks (u32); they are packed in place to u16 (ARR values)
 * with write precompensation applied.
 */

#include "ufi_firmware.h"
#include "ufi_verify.h"
#include <string.h>

/* ============================================================================
 * EXTERNE VARIABLEN
 * ============================================================================ */

extern capture_context_t g_capture;

/* ============================================================================
 * WRITE KONFIGURATION
 * ============================================================================ */

/* pulse width, representable range, precompensation: ufi_write_codec.h */
#define WRITE_INDEX_TIMEOUT_MS  1000u   // no index pulse within this time: give up

typedef struct {
    write_state_t state;
    uint8_t track;
    uint8_t side;
    uint32_t flux_count;        // Anzahl Flux-Übergänge
    uint32_t bytes_received;    // Empfangene Bytes
    uint32_t bytes_expected;    // Erwartete Bytes
    bool verify_after;          // Nach Schreiben verifizieren?
    bool use_precomp;           // Write Precompensation?
    uint32_t precomp_ns;        // chosen per data rate when the track is packed
    volatile uint8_t updates_left;  // timer updates until the last pulse is out
    bool compact;               // WRITE_TRACK_C: OUT data in the READ_TRACK stream code
    compact_dec_t dec;          // compact: stream decoder state (ufi_write_codec.c)
    bool clamp;                 // out-of-range periods are clamped (counted) instead of refused
    uint32_t clamped;           // periods moved into range by the last pack
    uint8_t verify_level;       // END payload: 0 none, 1 completed, 2 plausible, 3 content, 4 unverifiable
    bool apple;                 // Apple Disk II: WRDATA toggles on TIM3_CH2, /WRREQ, no index
    uint32_t wait_since;        // HAL tick when the wait for the index pulse began
    uint8_t quarter;            // Apple quarter-track offset of the seek (1.14)
    uint16_t scale;             // time scale of the deltas in 1/1000 (1000 = as sent), 1.14
} write_context_t;

/* Greaseweazle pre_write / post_write (ufi_gw.c SET_PARAMS), ticks; 0 = off.  Configuration,
 * not per write: the native paths leave it at 0. */
static uint32_t cfg_pre_ticks, cfg_post_ticks;
static bool post_pending;           /* the silent post_write period is running */

static write_context_t g_write;

/* Write data lives in the shared flux store (ufi_flux.c); capture and write never
 * run at the same time.  A verify capture overwrites it (only counts are compared). */
static uint32_t* write_buffer;
static uint32_t write_buffer_words;
static uint16_t* write_arr;     // packed ARR values (same memory as write_buffer)

static TIM_HandleTypeDef htim3;
static DMA_HandleTypeDef hdma_tim3_up;

/* ============================================================================
 * WRITE INITIALISIERUNG
 * ============================================================================ */

/* v0.7 Apple port: PA7 as TIM3_CH2 in toggle mode while writing, plain GPIO otherwise */
static void apple_wrdata(bool on) {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = PIN_APL_WRDATA.pin;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    if (on) {
        TIM3->CCR2 = 0;
        TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_OC2M | TIM_CCMR1_CC2S)) | (TIM_OCMODE_TOGGLE << 8);
        TIM3->CCER |= TIM_CCER_CC2E;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Alternate = APL_WRDATA_AF;
    } else {
        TIM3->CCER &= ~TIM_CCER_CC2E;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
    }
    HAL_GPIO_Init(PIN_APL_WRDATA.port, &gpio);
}

static void wdata_idle(void) {
    /* Forced inactive: active-low channel -> PA6 high -> LS07 releases WDATA */
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_FORCED_INACTIVE;
}

void ufi_write_init(void) {
    write_buffer = ufi_flux_store(&write_buffer_words);
    write_arr = (uint16_t*)write_buffer;
    memset(&g_write, 0, sizeof(g_write));
    g_write.state = WRITE_IDLE;
    g_write.use_precomp = true;

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_DOWN;
    htim3.Init.Period = 0xFFFF;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_FORCED_INACTIVE;
    oc.Pulse = WRITE_PULSE_TICKS - 1;
    oc.OCPolarity = TIM_OCPOLARITY_LOW;     // active = low = bus asserted
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);
    TIM_CCxChannelCmd(TIM3, TIM_CHANNEL_1, TIM_CCx_ENABLE);

    /* Hand PA6 from GPIO to TIM3_CH1 only now that the output is forced high */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = PIN_FDD_WDATA.pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = FDD_WDATA_AF;
    HAL_GPIO_Init(PIN_FDD_WDATA.port, &gpio);

    hdma_tim3_up.Instance = DMA1_Stream1;
    hdma_tim3_up.Init.Request = DMA_REQUEST_TIM3_UP;
    hdma_tim3_up.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim3_up.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim3_up.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim3_up.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tim3_up.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_tim3_up.Init.Mode = DMA_NORMAL;
    hdma_tim3_up.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_tim3_up.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_tim3_up) != HAL_OK) {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(TIM3_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}

/* ============================================================================
 * WRITE PRECOMPENSATION
 * ============================================================================ */

/* Pack the u32 deltas into u16 ARR values in place (ufi_write_codec.c: scale, write
 * precompensation, range check).  Nothing is written to the disk when a period is not
 * representable and clamping was not requested (UFI_ERR_RANGE, status 18). */
static int pack_deltas(void) {
    const uint16_t pc = ufi_drive_get_timing().precomp_ns;    /* host override (B11) */
    const pack_cfg_t cfg = {
        .scale = g_write.scale,
        .precomp_ns = (!g_write.use_precomp || g_write.apple) ? 0u
                    : (pc == PRECOMP_AUTO) ? write_precomp_auto_ns(write_buffer, g_write.flux_count) : pc,
        .track = g_write.track,
        .clamp = g_write.clamp,
    };
    const int ret = write_pack(write_buffer, g_write.flux_count, &cfg, write_arr, &g_write.clamped);
    if (ret != 0) {
        return UFI_ERR_RANGE;
    }
    SCB_CleanDCache_by_Addr((uint32_t*)write_arr,
                            (int32_t)(((g_write.flux_count * 2u) + 31u) & ~31u));
    return UFI_OK;
}

/* ============================================================================
 * FLUX-DATEN EMPFANGEN
 * ============================================================================ */

int ufi_write_prepare(uint8_t track, uint8_t side, uint32_t flux_count, bool verify) {
    if (g_write.state != WRITE_IDLE ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (flux_count < 3 || flux_count > write_buffer_words ||
        flux_count - 2u > WRITE_DMA_ITEMS_MAX) {    /* F02: one DMA transfer only */
        return UFI_ERR_BUFFER_FULL;
    }

    g_write.track = track;
    g_write.side = side;
    g_write.flux_count = flux_count;
    g_write.bytes_expected = flux_count * sizeof(uint32_t);
    g_write.bytes_received = 0;
    g_write.verify_after = verify;
    g_write.compact = false;
    g_write.quarter = 0;
    g_write.scale = 1000;
    g_write.clamp = false;
    g_write.clamped = 0;
    g_write.verify_level = 0;
    g_write.state = WRITE_RECEIVING;

    return UFI_OK;
}

void ufi_write_set_quarter(uint8_t quarter) {
    g_write.quarter = quarter;
}

void ufi_write_set_scale(uint16_t per_mille) {
    g_write.scale = per_mille ? per_mille : 1000u;
}

void ufi_write_set_pre_post(uint16_t pre_us, uint16_t post_us) {
    uint32_t pre = (uint32_t)pre_us * (FLUX_TIMER_FREQ / 1000000UL);
    uint32_t post = (uint32_t)post_us * (FLUX_TIMER_FREQ / 1000000UL);
    cfg_pre_ticks = pre > WRITE_MAX_DELTA ? WRITE_MAX_DELTA : pre;
    cfg_post_ticks = post > WRITE_MAX_DELTA ? WRITE_MAX_DELTA : post;
}

void ufi_write_set_clamp(bool allow) {
    g_write.clamp = allow;
}

uint32_t ufi_write_clamped(void) {
    return g_write.clamped;
}

/* END payload of a WRITE (1.15): verify level u8 (0 none, 1 write completed, 2 read-back
 * plausible, 3 content verified, 4 unverifiable), clamped periods u32 */
void ufi_write_result_payload(uint8_t out[5]) {
    out[0] = g_write.verify_level;
    memcpy(&out[1], &g_write.clamped, 4);
}

/* WRITE_TRACK_C: byte_count bytes in the READ_TRACK stream code (ufi_stream.c; index
 * markers are not allowed), decoded on arrival into the same u32 delta buffer */
int ufi_write_prepare_compact(uint8_t track, uint8_t side, uint32_t flux_count,
                              uint32_t byte_count, bool verify) {
    int ret = ufi_write_prepare(track, side, flux_count, verify);
    if (ret != UFI_OK) {
        return ret;
    }
    if (byte_count < flux_count || byte_count > flux_count * 5u) {
        g_write.state = WRITE_IDLE;
        return UFI_ERR_BUFFER_FULL;
    }
    g_write.compact = true;
    g_write.quarter = 0;
    g_write.bytes_expected = byte_count;
    compact_init(&g_write.dec, write_buffer, flux_count);
    return UFI_OK;
}

int ufi_write_receive_chunk(uint8_t* data, uint32_t len) {
    if (g_write.state != WRITE_RECEIVING) {
        return UFI_ERR_BUSY;
    }
    if (g_write.bytes_received + len > g_write.bytes_expected) {
        return UFI_ERR_BUFFER_FULL;
    }

    if (g_write.compact) {
        if (compact_feed(&g_write.dec, data, len) != 0) {
            g_write.state = WRITE_ERROR;        /* F01: stop at the first bad byte */
            return UFI_ERR_BUFFER_FULL;
        }
    } else {
        memcpy(((uint8_t*)write_buffer) + g_write.bytes_received, data, len);
    }
    g_write.bytes_received += len;

    return UFI_OK;
}

bool ufi_write_data_complete(void) {
    return (g_write.state == WRITE_RECEIVING &&
            g_write.bytes_received >= g_write.bytes_expected);
}

/* ============================================================================
 * TRACK SCHREIBEN
 * ============================================================================ */

int ufi_write_start(void) {
    if (g_write.state != WRITE_RECEIVING) {
        return UFI_ERR_BUSY;
    }
    if (g_write.bytes_received < g_write.bytes_expected) {
        return UFI_ERR_BUFFER_FULL;
    }
    if (g_write.compact && !compact_complete(&g_write.dec)) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_BUFFER_FULL;         /* malformed / incomplete compact data */
    }
    g_write.apple = ufi_drive_is_apple();
    {
        const int pret = pack_deltas();     /* before seek and WGATE: refuse, write nothing */
        if (pret != UFI_OK) {
            g_write.state = WRITE_ERROR;
            return pret;
        }
    }
    if (ufi_board_write_locked()) {         /* WRITE LOCK jumper: WGATE is blocked anyway */
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    if (ufi_drive_write_protected()) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    const int sret = ufi_drive_seek_q(g_write.track, g_write.quarter);
    if (sret != 0) {
        g_write.state = WRITE_ERROR;
        return sret == UFI_ERR_BAD_ARGS ? UFI_ERR_BAD_ARGS : UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(g_write.side);

    /* Timer: first two intervals by hand (UG loads the shadow ARR), DMA feeds the rest */
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    if (cfg_pre_ticks && !g_write.apple) {
        /* gw pre_write: WGATE goes on at the index, the first flux comes that much later */
        const uint32_t a = (uint32_t)write_arr[0] + 1u + cfg_pre_ticks;
        write_arr[0] = (uint16_t)((a > WRITE_MAX_DELTA ? WRITE_MAX_DELTA : a) - 1u);
    }
    post_pending = false;
    TIM3->ARR = write_arr[0];
    TIM3->EGR = TIM_EGR_UG;                 // shadow ARR = interval 0, CNT = ARR (down)
    TIM3->SR = 0;
    TIM3->ARR = write_arr[1];               // preload = interval 1
    if (g_write.apple) {
        /* Disk II: WRDATA level change at every update (CNT reaches CCR2 = 0 once per
         * interval) on TIM3_CH2 / PA7; the 34-pin WDATA (CH1) stays idle */
        apple_wrdata(true);
    } else {
        TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_PWM1;
    }

    HAL_DMA_Abort(&hdma_tim3_up);
    if (HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)&write_arr[2], (uint32_t)&TIM3->ARR,
                         g_write.flux_count - 2) != HAL_OK) {
        wdata_idle();
        apple_wrdata(false);
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }
    HAL_Delay(20);                          // head settle after side select

    led_set(&PIN_LED_FDD, true);
    if (g_write.apple && !ufi_config_apple_sync()) {
        /* no index pulse: start right away, /WRREQ low; ends after the last interval */
        HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_RESET);
        g_write.state = WRITE_ACTIVE;
        g_write.updates_left = 0;
        TIM3->DIER |= TIM_DIER_UDE;
        __HAL_TIM_ENABLE(&htim3);
        ufi_sched_arm(TIM2->CNT);
        return UFI_OK;
    }
    g_write.wait_since = HAL_GetTick();
    g_write.state = WRITE_WAITING_INDEX;
    return UFI_OK;
}

static void write_finish(write_state_t final_state) {
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    wdata_idle();
    bus_out(&PIN_FDD_WGATE, false);
    HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
    apple_wrdata(false);
    led_set(&PIN_LED_FDD, false);
    g_write.state = final_state;
}

/* Called from the TIM2 index-capture interrupt */
void ufi_write_index_handler(uint32_t t) {
    if (g_write.apple && g_write.state == WRITE_WAITING_INDEX) {
        /* Disk II with sync sensor: start at the sensor pulse (track alignment kept);
         * the end comes from the last interval, not from the next pulse */
        HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_RESET);
        g_write.state = WRITE_ACTIVE;
        g_write.updates_left = 0;
        TIM3->DIER |= TIM_DIER_UDE;
        __HAL_TIM_ENABLE(&htim3);
        ufi_sched_arm(t);
        return;
    }
    if (g_write.apple) {
        return;                             /* Disk II: no index safety stop */
    }
    if (g_write.state == WRITE_WAITING_INDEX) {
        bus_out(&PIN_FDD_WGATE, true);
        g_write.state = WRITE_ACTIVE;
        g_write.updates_left = 0;
        TIM3->DIER |= TIM_DIER_UDE;         // DMA: next interval on every update
        __HAL_TIM_ENABLE(&htim3);           // first pulse after interval 0
        ufi_sched_arm(t);
    }
    else if (g_write.state == WRITE_ACTIVE) {
        write_finish(WRITE_COMPLETE);       // safety: never write past one revolution
    }
}

/* DMA has loaded the last interval: two more updates (end of n-2, end of n-1) */
static void write_dma_done(void) {
    TIM3->DIER &= ~TIM_DIER_UDE;
    g_write.updates_left = 2;
    TIM3->SR = ~TIM_SR_UIF;
    TIM3->DIER |= TIM_DIER_UIE;
}

void ufi_write_tim3_irq(void) {
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = ~TIM_SR_UIF;
        if (g_write.updates_left > 0 && --g_write.updates_left == 0) {
            if (cfg_post_ticks && !g_write.apple && !post_pending) {
                /* gw post_write: WGATE stays on for one more period without a pulse */
                post_pending = true;
                TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_FORCED_INACTIVE;
                TIM3->ARR = cfg_post_ticks - 1u;    /* after the running (stale) period */
                g_write.updates_left = 2;
            } else {
                write_finish(WRITE_COMPLETE);
            }
        }
    }
}

void ufi_write_dma_irq(void) {
    /* direct register handling: TC = done, TE = error */
    const uint32_t isr = DMA1->LISR;
    if (isr & DMA_LISR_TCIF1) {
        DMA1->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1;
        if (g_write.state == WRITE_ACTIVE) {
            write_dma_done();
        }
    }
    if (isr & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) {
        DMA1->LIFCR = DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;
        write_finish(WRITE_ERROR);
    }
    hdma_tim3_up.State = HAL_DMA_STATE_READY;
    __HAL_UNLOCK(&hdma_tim3_up);
}

/* Kept for the main loop: the timer does the work now */
void ufi_write_process(void) {
}

/* ============================================================================
 * TRACK LÖSCHEN
 * ============================================================================ */

int ufi_erase_track(uint8_t track, uint8_t side) {
    return ufi_erase_track_ms(track, side, 220u);   /* one revolution and a bit */
}

int ufi_erase_track_ms(uint8_t track, uint8_t side, uint32_t ms) {
    if (ms < 1u) ms = 1u;
    if (ms > 5000u) ms = 5000u;
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    if (ufi_drive_seek(track) != 0) {
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(side);
    HAL_Delay(20);

    led_set(&PIN_LED_FDD, true);

    if (ufi_drive_is_apple()) {             /* Disk II: /WRREQ without level changes */
        HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_RESET);
        HAL_Delay(ms);
        HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
        led_set(&PIN_LED_FDD, false);
        return UFI_OK;
    }

    const uint32_t start = HAL_GetTick();

    /* Wait for the start of an index pulse (INDEX pin stays readable in AF mode; or the
     * internal index simulation) */
    while (ufi_flux_index_asserted()) {
        if (HAL_GetTick() - start > 500) {
            led_set(&PIN_LED_FDD, false);
            return UFI_ERR_NO_INDEX;
        }
    }
    while (!ufi_flux_index_asserted()) {
        if (HAL_GetTick() - start > 500) {
            led_set(&PIN_LED_FDD, false);
            return UFI_ERR_NO_INDEX;
        }
    }

    bus_out(&PIN_FDD_WGATE, true);          // gate on, no WDATA pulses = DC erase
    HAL_Delay(ms);
    bus_out(&PIN_FDD_WGATE, false);

    led_set(&PIN_LED_FDD, false);

    return UFI_OK;
}

/* ============================================================================
 * VERIFY
 * ============================================================================ */

/* Verify with evidence levels (ufi_verify.c, 1.15): the packed track is decoded into a
 * sector reference (ids + data CRC-32) before the read-back reuses the flux store; two
 * revolutions are read back and every reference sector must come back with the same
 * data (level 3).  Tracks the decoder does not understand (GCR, protections) only get
 * the transition-count check (level 2).  Anything else fails with UFI_ERR_DMA (6). */
__attribute__((section(".dtcm"))) static verify_ref_t verify_ref;
__attribute__((section(".dtcm"))) static mfm_decoder_t verify_dec;

int ufi_write_verify(void) {
    if (g_write.state != WRITE_COMPLETE) {
        return UFI_ERR_BUSY;
    }
    g_write.state = WRITE_VERIFYING;
    verify_ref_build(&verify_ref, &verify_dec, write_arr, g_write.flux_count);

    int ret = ufi_capture_start_q(g_write.track, g_write.side, 2, 0, g_write.quarter);
    if (ret != 0) {
        g_write.state = WRITE_ERROR;
        return ret;
    }
    const uint32_t start = HAL_GetTick();
    while (ufi_capture_get_state() != CAPTURE_COMPLETE) {
        if (HAL_GetTick() - start > 1500 || g_capture.state == CAPTURE_ERROR) {
            ufi_flux_capture_stop();
            g_write.state = WRITE_ERROR;
            return UFI_ERR_TIMEOUT;
        }
    }
    const uint8_t revs = ufi_flux_get_revolution_count();
    if (revs == 0) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }

    /* plausibility: transition count of revolution 0 within 5 % (at least 100) */
    const flux_revolution_t* r0 = ufi_flux_get_revolution(0);
    int32_t diff = (int32_t)r0->count - (int32_t)g_write.flux_count;
    if (diff < 0) diff = -diff;
    uint32_t tolerance = g_write.flux_count / 20;
    if (tolerance < 100) tolerance = 100;
    const bool count_ok = (uint32_t)diff <= tolerance;

    /* content: all revolutions as one stream through the decoder */
    verify_match_t m;
    verify_match_begin(&m, &verify_ref, &verify_dec);
    uint32_t carry = 0;
    for (uint8_t k = 0; k < revs; k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            verify_match_interval(&m, r->samples[i].timestamp - prev + carry);
            carry = 0;
            prev = r->samples[i].timestamp;
        }
        carry = r->index_time - prev;
    }
    const uint8_t level = verify_match_result(&m, count_ok);
    if (!level) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }
    g_write.verify_level = level;
    g_write.state = WRITE_IDLE;
    return UFI_OK;
}

/* ============================================================================
 * STATUS & KONTROLLE
 * ============================================================================ */

write_state_t ufi_write_get_state(void) {
    return g_write.state;
}

uint32_t ufi_write_get_progress(void) {
    if (g_write.state == WRITE_RECEIVING && g_write.bytes_expected) {
        return (g_write.bytes_received * 100) / g_write.bytes_expected;
    }
    else if (g_write.state == WRITE_ACTIVE && g_write.flux_count > 2) {
        const uint32_t left = __HAL_DMA_GET_COUNTER(&hdma_tim3_up);
        return ((g_write.flux_count - 2 - left) * 100) / (g_write.flux_count - 2);
    }
    return 0;
}

void ufi_write_abort(void) {
    write_finish(WRITE_IDLE);
    HAL_DMA_Abort(&hdma_tim3_up);
    g_write.flux_count = 0;
    g_write.bytes_received = 0;
}

void ufi_write_set_precomp(bool enable) {
    g_write.use_precomp = enable;
}

static uint8_t write_event_cmd(void);

/* v1 data path (ufi_usb.c): a bad compact byte ends the write with an error event */
void ufi_write_receive_failed(void) {
    const uint8_t cmd = write_event_cmd();
    ufi_write_abort();
    ufi_usb_send_event(cmd, UFI_ERR_BUFFER_FULL);
}

/* Completion events carry the command code the host used */
static uint8_t write_event_cmd(void) {
    if (g_write.compact) {
        return UFI_CMD_WRITE_TRACK_C;
    }
    return g_write.verify_after ? UFI_CMD_WRITE_TRACK_VERIFY : UFI_CMD_WRITE_TRACK;
}

/* Main-loop step: start once all data arrived, verify when done, report to the host */
void ufi_write_service(void) {
    if (ufi_write_data_complete()) {
        int ret = ufi_write_start();
        if (ret != UFI_OK) {
            ufi_usb_send_event(write_event_cmd(), ret);
            ufi_write_abort();
        }
        return;
    }
    if (g_write.state == WRITE_WAITING_INDEX &&
        HAL_GetTick() - g_write.wait_since > WRITE_INDEX_TIMEOUT_MS) {
        const uint8_t cmd = write_event_cmd();
        ufi_write_abort();                  /* no index pulse: nothing was written */
        ufi_usb_send_event(cmd, UFI_ERR_NO_INDEX);
        return;
    }
    if (g_write.state == WRITE_COMPLETE || g_write.state == WRITE_ERROR) {
        const uint8_t cmd = write_event_cmd();
        int ret = UFI_ERR_DMA;
        if (g_write.state == WRITE_COMPLETE) {
            g_write.verify_level = 1;           /* timer and DMA finished */
            ret = g_write.verify_after ? ufi_write_verify() : UFI_OK;
        }
        g_capture.state = CAPTURE_IDLE;     /* verify capture is internal, not sent */
        g_write.state = WRITE_IDLE;
        ufi_usb_send_event(cmd, ret);
    }
}

/* Firmware-generated track (USB floppy mode, ufi_floppy.c): the caller has put
 * flux_count u32 deltas into the flux store (ufi_flux_store).  Blocking until the track
 * is written (index wait + one revolution); no USB events, the caller verifies. */
int ufi_write_local(uint8_t track, uint8_t side, uint32_t flux_count, bool clamp) {
    int ret = ufi_write_prepare(track, side, flux_count, false);
    if (ret != UFI_OK) {
        return ret;
    }
    g_write.clamp = clamp;
    g_write.bytes_received = g_write.bytes_expected;    /* data is already in place */
    ret = ufi_write_start();
    if (ret == UFI_OK) {
        const uint32_t t0 = HAL_GetTick();
        while (g_write.state == WRITE_WAITING_INDEX || g_write.state == WRITE_ACTIVE) {
            if (HAL_GetTick() - t0 > 1500u) {
                ret = UFI_ERR_NO_INDEX;
                break;
            }
        }
        if (ret == UFI_OK && g_write.state != WRITE_COMPLETE) {
            ret = UFI_ERR_DMA;
        }
    }
    if (ret != UFI_OK) {
        ufi_write_abort();
    }
    g_write.state = WRITE_IDLE;
    return ret;
}

/* ============================================================================
 * DIAGNOSE: DAUERMUSTER
 * ============================================================================ */

/* Write one constant flux interval for duration_ms (write chain on the scope, head
 * alignment).  Same guards as a track write: write lock jumper, write protect, idle
 * write/capture path, motor running.  Blocking; duration capped at 5 s. */
int ufi_write_pattern(uint8_t track, uint8_t side, uint16_t interval_ns, uint16_t duration_ms) {
    if (g_write.state != WRITE_IDLE ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    if (!ufi_drive_motor_is_on()) {
        return UFI_ERR_NO_DRIVE;
    }
    if (ufi_drive_is_apple()) {
        return UFI_ERR_NOT_IMPL;            /* pattern test drives the 34-pin WDATA only */
    }
    const uint32_t ticks = ((uint32_t)interval_ns * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL;
    if (ticks < WRITE_MIN_DELTA || ticks > WRITE_MAX_DELTA) {
        return UFI_ERR_BUFFER_FULL;
    }
    if (duration_ms > 5000u) {
        duration_ms = 5000u;
    }
    if (ufi_drive_seek(track) != UFI_OK) {
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(side);
    HAL_Delay(20);

    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    TIM3->ARR = ticks - 1u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_PWM1;
    led_set(&PIN_LED_FDD, true);
    bus_out(&PIN_FDD_WGATE, true);
    __HAL_TIM_ENABLE(&htim3);

    const uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < duration_ms) {
        UFI_WATCHDOG_FEED();
    }
    write_finish(WRITE_IDLE);
    return UFI_OK;
}
