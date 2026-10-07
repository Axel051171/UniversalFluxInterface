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
#include <string.h>

/* ============================================================================
 * EXTERNE VARIABLEN
 * ============================================================================ */

extern capture_context_t g_capture;

/* ============================================================================
 * WRITE KONFIGURATION
 * ============================================================================ */

/* precompensation: see precomp_for_data() */
#define WRITE_PULSE_NS      400     // WDATA low pulse
#define WRITE_PULSE_TICKS   ((WRITE_PULSE_NS * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL)
#define WRITE_MIN_DELTA     (4 * WRITE_PULSE_TICKS)
#define WRITE_MAX_DELTA     65536U  // 16-bit timer: 238 us max interval

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
    uint32_t decoded;           // compact: deltas decoded so far
    uint8_t code[5];            // compact: multi-byte code being assembled
    uint8_t code_len, code_need;
    bool bad;                   // compact: reserved / index code seen
} write_context_t;

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

static uint32_t apply_precomp(uint32_t timing, uint32_t prev_timing, uint32_t next_timing, uint8_t track) {
    if (!g_write.use_precomp || track < 40) {
        return timing;
    }

    uint32_t precomp_ns = (track > 60) ? g_write.precomp_ns : (g_write.precomp_ns / 2);
    uint32_t precomp_ticks = (precomp_ns * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL;

    if (prev_timing > timing * 2) {
        return (timing > precomp_ticks) ? timing - precomp_ticks : timing;
    }
    if (next_timing > timing * 2) {
        return timing + precomp_ticks;
    }
    return timing;
}

/* Pack u32 deltas into u16 ARR values in place (halfword i never overlaps a word >= i-1;
 * the previous delta is kept in a local) */
/* Write precompensation per data rate (82077 FDC defaults): the shortest MFM interval is
 * 2 bit cells, so it identifies the rate: ~1 us = ED 1 Mbit/s -> 42 ns, else 125 ns */
#define PRECOMP_DD_HD_NS    125
#define PRECOMP_ED_NS       42
#define ED_SHORTEST_TICKS   ((1500UL * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL)   /* 1.5 us */

static uint32_t precomp_for_data(void) {
    /* 10th-percentile-ish shortest interval over the first samples, robust to glitches */
    uint32_t below = 0, n = (g_write.flux_count < 2000) ? g_write.flux_count : 2000;
    for (uint32_t i = 0; i < n; i++) {
        below += (write_buffer[i] < ED_SHORTEST_TICKS) ? 1u : 0u;
    }
    return (below * 10 > n) ? PRECOMP_ED_NS : PRECOMP_DD_HD_NS;
}

static void pack_deltas(void) {
    const uint16_t pc = ufi_drive_get_timing().precomp_ns;    /* host override (B11) */
    g_write.precomp_ns = (pc == PRECOMP_AUTO) ? precomp_for_data() : pc;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < g_write.flux_count; i++) {
        const uint32_t cur = write_buffer[i];
        const uint32_t next = (i + 1 < g_write.flux_count) ? write_buffer[i + 1] : cur;
        uint32_t d = (i > 0 && i + 1 < g_write.flux_count)
                     ? apply_precomp(cur, prev, next, g_write.track) : cur;
        if (d < WRITE_MIN_DELTA) d = WRITE_MIN_DELTA;
        if (d > WRITE_MAX_DELTA) d = WRITE_MAX_DELTA;
        prev = cur;
        write_arr[i] = (uint16_t)(d - 1);
    }
    SCB_CleanDCache_by_Addr((uint32_t*)write_arr,
                            (int32_t)(((g_write.flux_count * 2u) + 31u) & ~31u));
}

/* ============================================================================
 * FLUX-DATEN EMPFANGEN
 * ============================================================================ */

int ufi_write_prepare(uint8_t track, uint8_t side, uint32_t flux_count, bool verify) {
    if (g_write.state != WRITE_IDLE ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (flux_count < 3 || flux_count > write_buffer_words) {
        return UFI_ERR_BUFFER_FULL;
    }

    g_write.track = track;
    g_write.side = side;
    g_write.flux_count = flux_count;
    g_write.bytes_expected = flux_count * sizeof(uint32_t);
    g_write.bytes_received = 0;
    g_write.verify_after = verify;
    g_write.compact = false;
    g_write.state = WRITE_RECEIVING;

    return UFI_OK;
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
    g_write.bytes_expected = byte_count;
    g_write.decoded = 0;
    g_write.code_len = 0;
    g_write.code_need = 0;
    g_write.bad = false;
    return UFI_OK;
}

static void put_decoded(uint32_t d) {
    if (g_write.decoded < g_write.flux_count) {
        write_buffer[g_write.decoded] = d;
    }
    g_write.decoded++;                      /* overcount = malformed data, caught later */
}

static void decode_compact(const uint8_t* p, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t b = p[i];
        if (g_write.code_need == 0) {
            if (b >= 0x01u && b <= 0xEFu) {
                put_decoded(b);
                continue;
            }
            g_write.code[0] = b;
            g_write.code_len = 1;
            g_write.code_need = (b >= 0xF0u && b <= 0xFCu) ? 2u : (b == 0xFEu) ? 5u : 0xFFu;
            continue;
        }
        g_write.code[g_write.code_len++] = b;
        if (g_write.code_len < g_write.code_need) {
            continue;
        }
        const uint8_t* c = g_write.code;
        if (g_write.code_need == 2u) {
            put_decoded(240u + ((uint32_t)(c[0] - 0xF0u) << 8) + c[1]);
        } else if (g_write.code_need == 5u) {
            put_decoded((uint32_t)c[1] | ((uint32_t)c[2] << 8) | ((uint32_t)c[3] << 16) |
                        ((uint32_t)c[4] << 24));
        } else {
            g_write.bad = true;             /* reserved / index code: reject the track */
        }
        g_write.code_need = 0;
    }
}

int ufi_write_receive_chunk(uint8_t* data, uint32_t len) {
    if (g_write.state != WRITE_RECEIVING) {
        return UFI_ERR_BUSY;
    }
    if (g_write.bytes_received + len > g_write.bytes_expected) {
        return UFI_ERR_BUFFER_FULL;
    }

    if (g_write.compact) {
        decode_compact(data, len);
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
    if (g_write.compact && (g_write.bad || g_write.code_need != 0 ||
                            g_write.decoded != g_write.flux_count)) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_BUFFER_FULL;         /* malformed compact data */
    }
    if (ufi_board_write_locked()) {         /* WRITE LOCK jumper: WGATE is blocked anyway */
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    if (ufi_drive_write_protected()) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    if (ufi_drive_seek(g_write.track) != 0) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(g_write.side);
    pack_deltas();

    /* Timer: first two intervals by hand (UG loads the shadow ARR), DMA feeds the rest */
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    TIM3->ARR = write_arr[0];
    TIM3->EGR = TIM_EGR_UG;                 // shadow ARR = interval 0, CNT = ARR (down)
    TIM3->SR = 0;
    TIM3->ARR = write_arr[1];               // preload = interval 1
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_PWM1;

    HAL_DMA_Abort(&hdma_tim3_up);
    if (HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)&write_arr[2], (uint32_t)&TIM3->ARR,
                         g_write.flux_count - 2) != HAL_OK) {
        wdata_idle();
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }
    HAL_Delay(20);                          // head settle after side select

    g_write.state = WRITE_WAITING_INDEX;
    led_set(&PIN_LED_FDD, true);
    return UFI_OK;
}

static void write_finish(write_state_t final_state) {
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    wdata_idle();
    bus_out(&PIN_FDD_WGATE, false);
    led_set(&PIN_LED_FDD, false);
    g_write.state = final_state;
}

/* Called from the TIM2 index-capture interrupt */
void ufi_write_index_handler(uint32_t t) {
    (void)t;
    if (g_write.state == WRITE_WAITING_INDEX) {
        bus_out(&PIN_FDD_WGATE, true);
        g_write.state = WRITE_ACTIVE;
        g_write.updates_left = 0;
        TIM3->DIER |= TIM_DIER_UDE;         // DMA: next interval on every update
        __HAL_TIM_ENABLE(&htim3);           // first pulse after interval 0
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
            write_finish(WRITE_COMPLETE);
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
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    if (ufi_drive_seek(track) != 0) {
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(side);
    HAL_Delay(20);

    led_set(&PIN_LED_FDD, true);

    const uint32_t start = HAL_GetTick();

    /* Wait for the start of an index pulse (INDEX pin stays readable in AF mode) */
    while (bus_in(&PIN_FDD_INDEX)) {
        if (HAL_GetTick() - start > 500) {
            led_set(&PIN_LED_FDD, false);
            return UFI_ERR_NO_INDEX;
        }
    }
    while (!bus_in(&PIN_FDD_INDEX)) {
        if (HAL_GetTick() - start > 500) {
            led_set(&PIN_LED_FDD, false);
            return UFI_ERR_NO_INDEX;
        }
    }

    bus_out(&PIN_FDD_WGATE, true);          // gate on, no WDATA pulses = DC erase
    HAL_Delay(220);
    bus_out(&PIN_FDD_WGATE, false);

    led_set(&PIN_LED_FDD, false);

    return UFI_OK;
}

/* ============================================================================
 * VERIFY
 * ============================================================================ */

int ufi_write_verify(void) {
    if (g_write.state != WRITE_COMPLETE) {
        return UFI_ERR_BUSY;
    }

    g_write.state = WRITE_VERIFYING;

    int ret = ufi_capture_start(g_write.track, g_write.side, 1, 0);
    if (ret != 0) {
        g_write.state = WRITE_ERROR;
        return ret;
    }

    const uint32_t start = HAL_GetTick();
    while (ufi_capture_get_state() != CAPTURE_COMPLETE) {
        if (HAL_GetTick() - start > 1000 || g_capture.state == CAPTURE_ERROR) {
            ufi_flux_capture_stop();
            g_write.state = WRITE_ERROR;
            return UFI_ERR_TIMEOUT;
        }
    }

    flux_revolution_t* read_data = ufi_capture_get_data(0);
    if (!read_data) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }

    int32_t diff = (int32_t)read_data->count - (int32_t)g_write.flux_count;
    if (diff < 0) diff = -diff;

    uint32_t tolerance = g_write.flux_count / 20;
    if (tolerance < 100) tolerance = 100;

    if ((uint32_t)diff > tolerance) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }

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
    if (g_write.state == WRITE_COMPLETE || g_write.state == WRITE_ERROR) {
        const uint8_t cmd = write_event_cmd();
        int ret = UFI_ERR_DMA;
        if (g_write.state == WRITE_COMPLETE) {
            ret = g_write.verify_after ? ufi_write_verify() : UFI_OK;
        }
        g_capture.state = CAPTURE_IDLE;     /* verify capture is internal, not sent */
        g_write.state = WRITE_IDLE;
        ufi_usb_send_event(cmd, ret);
    }
}
