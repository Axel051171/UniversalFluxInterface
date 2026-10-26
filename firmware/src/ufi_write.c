/**
 * UFI Flux Engine - Write Support
 *
 * Flux-basiertes Schreiben für Disk-Erstellung und Kopien.
 *
 * WDATA = TIM3_CH1 on PA6 (AF2), 275 MHz, same tick as the read timer:
 *  - TIM3 counts DOWN in PWM mode 1 with active-low output: the channel is active
 *    while CNT <= CCR1, so every period ends with a WRITE_PULSE_TICKS pulse.
 *  - Period k = flux interval k.  ARR is preloaded; DMA1_Stream1 (TIM3_UP request)
 *    writes the next interval into ARR on every update, circularly from a ring of two
 *    2048-period halves in SRAM2 that the HT/TC interrupts refill from the packed track
 *    (a DMA transfer is limited to 65535 items; tracks are limited by the flux store).
 *    The update interrupt counts the last periods against the DMA counter and stops
 *    the timer exactly after the last one (ufi_write_stream.c, tested on the host).
 *  - Started from the index interrupt; stopped after the last interval, WGATE drops
 *    right away.  The next index releases WGATE as a safety net.
 * Host deltas are TIM2/TIM3 ticks (u32); they are packed in place to u16 (ARR values)
 * with write precompensation applied.
 */

#include "ufi_firmware.h"
#include "ufi_metrics.h"
#include "ufi_profile.h"
#include "ufi_verify.h"
#include "ufi_quality.h"
#include "ufi_write_stream.h"
#include "ufi_write_wave.h"
#include "ufi_job.h"
#include "ufi_cancel.h"
#include "ufi_drive_extra.h"
#include "ufi_media.h"
static uint32_t write_media_token;
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
    volatile write_state_t state;
    bool gw_mode, wave_mode, cue_index, terminate_index;
    uint16_t gw_pre_us;
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
    bool erase_rest;            // flags2 bit0 (1.18): keep WGATE on, WDATA idle, until the next index
    uint32_t start_offset;      // flags2 bit1 (1.18): open the gate this many ticks after the index, 0 = at the index
    bool raw_check;             // flags2 bit4 (1.18, 4.17): raw fingerprint read-back without a content reference
} write_context_t;

/* Greaseweazle pre_write / post_write (ufi_gw.c SET_PARAMS), ticks; 0 = off.  Configuration,
 * not per write: the native paths leave it at 0. */
static uint32_t cfg_pre_ticks, cfg_post_ticks;
static uint32_t active_pre_ticks,active_post_ticks;
static bool post_pending;           /* the silent post_write period is running */
static bool index_cut;              /* the index ended an active write: track too long */
static bool erase_pending;          /* flags2 bit0: data is out, gate stays on until the index */
static uint32_t erase_start;        /* flux-timer time the erase tail began */
static uint32_t erased_ticks;       /* END payload: length of the erased tail, 0 = none */
/* Speed check before writing (op 41, 1.18): with a nonzero tolerance a native WRITE that
 * carries the source index period (flags bit6) measures the drive's own index period
 * before seek and WGATE and refuses with UFI_ERR_SPEED_MISMATCH outside the tolerance. */
static uint16_t speed_tolerance_permille;
static uint32_t speed_measured, speed_source;
/* Start offset (flags2 bit1, 1.18): the index arms TIM2 compare channel 4 at index + offset,
 * its interrupt opens the gate; start_applied = measured gate-open time minus index time. */
static bool start_pending;
static uint32_t start_index_time, start_applied;
/* Precompensation selection (1.18, op 45 / flags2 bit2): per-write override, else the profile zone of
 * the physical track, else TIMING / auto with the built-in rule on the physical track. */
static bool precomp_override;
static uint16_t precomp_override_ns;
/* weak-cell markers (flags2 bit3, 4.16): jitter in ticks for this write (< 0 = off), the seed of the
 * write's random stream and the number of transitions displaced or dropped (END payload) */
static int32_t weak_jitter = -1;
static uint32_t weak_seed, weak_applied;
static uint8_t precomp_zones, precomp_zone_from[4];
static uint16_t precomp_zone_ns[4];
static uint16_t precomp_last_ns;
static uint8_t precomp_last_track, precomp_last_phys, precomp_source;
static volatile bool receive_failed; /* IRQ -> main loop; never send USB from the IRQ */

static write_context_t g_write;
__attribute__((section(".dtcm"))) static verify_ref_t verify_ref;
__attribute__((section(".dtcm"))) static mfm_decoder_t verify_dec;
static uint32_t source_index_ticks, reference_started;
static bool local_reference_ready;
#include "ufi_raw_verify.h"
#include "ufi_precision.h"
static uint8_t raw_flags;
static uint16_t raw_tolerance=30;
static bool raw_available;
__attribute__((section(".dtcm"))) static raw_profile_t raw_source;
static uint8_t raw_report[RAW_REPORT_SIZE];
int ufi_raw_verify_options(const uint8_t* p,uint16_t n,uint8_t* out){
    if(n!=1 && n!=5)return UFI_ERR_BAD_ARGS;
    if(n==5){
        uint16_t tolerance=(uint16_t)(p[3]|(uint16_t)p[4]<<8);
        if(p[1]!=1 || (p[2]&~3u) || ((p[2]&2) && !(p[2]&1)) || !tolerance || tolerance>1000)return UFI_ERR_BAD_ARGS;
        raw_flags=p[2];raw_tolerance=tolerance;
    }
    memset(out,0,8);out[0]=1;out[1]=raw_flags;out[2]=(uint8_t)raw_tolerance;out[3]=(uint8_t)(raw_tolerance>>8);
    out[4]=3;out[5]=32;out[6]=RAW_WINDOWS;return UFI_OK;
}
uint16_t ufi_raw_verify_report(uint8_t* out){
    if(!raw_available)return 0;
    memcpy(out,raw_report,sizeof(raw_report));return sizeof(raw_report);
}
static uint64_t expected_sectors;
static uint8_t expected_c,expected_h,expected_n;
static bool reference_poll(void) {
    UFI_WATCHDOG_FEED();
    return !ufi_cancel_requested() && HAL_GetTick()-reference_started<10000u;
}

/* streaming ring (DMA1 reaches D2 SRAM2; nothing else lives there) and its bookkeeping */
__attribute__((section(".sram2"), aligned(32))) static uint16_t write_ring[2u * WSTREAM_HALF];
static wstream_t ws;
static wave_cursor_t wave_cursor;
static uint32_t wave_segments, write_timeout_ms;
static uint32_t last_ndtr;          /* DMA counter at the previous update interrupt */
static volatile int write_start_error;
static volatile bool diagnostic_write;
enum {VERIFY_CAPTURE=1,VERIFY_PRECISION,VERIFY_RAW,VERIFY_SECTORS};
static uint8_t verify_phase;
static uint32_t verify_since;
static bool verify_count_ok;
static raw_builder_t verify_raw_builder;
__attribute__((section(".backup"),aligned(4))) static raw_profile_t verify_observed;
static void dma_stop(void);
static void ring_fill(const wstream_fill_t* f);
static void start_now(uint32_t t);
static void write_verify_cancel(void);

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
    memset(&verify_ref,0,sizeof(verify_ref)); /* .dtcm is NOLOAD; coverage can precede first WRITE */
    g_write.state = WRITE_IDLE;
    g_write.use_precomp = true;

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();         /* write ring */

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
    /* active = bus asserted: low into the LS07 U7 (v0.x), high on the MOSFET gate Q10 (v1.0, A11b) */
    oc.OCPolarity = board_fdd_mosfet ? TIM_OCPOLARITY_HIGH : TIM_OCPOLARITY_LOW;
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
    hdma_tim3_up.Init.Mode = DMA_CIRCULAR;      /* over the ring; HT/TC refill (1.15) */
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
    if (g_write.wave_mode) {
        uint64_t duration;
        weak_applied = 0;
        if (weak_jitter >= 0 && !g_write.gw_mode) {   /* flags2 bit3 (4.16): randomize the marked cells once per write */
            g_write.flux_count = wave_weak_apply(write_buffer, g_write.flux_count, (uint16_t)weak_jitter, weak_seed, &weak_applied);
            if (g_write.flux_count < 3u) return UFI_ERR_RANGE;
        }
        if (!wave_plan(write_buffer, g_write.flux_count, &wave_segments, &duration))
            return UFI_ERR_RANGE;
        const uint64_t ms = duration / (FLUX_TIMER_FREQ / 1000u) + 2000u;
        if (ms > 60000u) return UFI_ERR_RANGE;
        write_timeout_ms = (uint32_t)ms;
        wave_init(&wave_cursor, write_buffer, g_write.flux_count);
        if(!g_write.gw_mode){
            wave_cursor.lead=(uint16_t)active_pre_ticks;wave_cursor.tail=(uint16_t)active_post_ticks;
            wave_segments+=(active_pre_ticks!=0)+(active_post_ticks!=0);
            active_post_ticks=0; /* silent tail is already in the DMA waveform */
        }
        return UFI_OK;
    }
    write_timeout_ms = 2000u;
    const pack_cfg_t cfg = {
        .scale = g_write.scale,
        .precomp_ns = (!g_write.use_precomp || g_write.apple) ? 0u : ufi_write_precomp_select(g_write.track),
        .track = g_write.track,
        .clamp = g_write.clamp,
        .precomp_fixed = true,              /* the rule already ran on the physical track (1.18) */
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
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING ||
        ufi_stream_active() || receive_failed) {
        return UFI_ERR_BUSY;
    }
    write_buffer=ufi_flux_store(&write_buffer_words);write_arr=(uint16_t*)write_buffer;
    if (flux_count < 3 || flux_count > write_buffer_words) {
        return UFI_ERR_BUFFER_FULL;
    }

    g_write.track = track;
    write_start_error=UFI_OK;
    diagnostic_write=false;
    ufi_flux_invalidate();
    source_index_ticks=0;
    local_reference_ready=false;
    raw_available=false;raw_source.valid=false;ufi_precision_reset();
    expected_sectors=0;
    expected_c=expected_h=expected_n=0;
    memset(&verify_ref,0,sizeof(verify_ref));
    if (verify) ufi_quality_clear();
    g_write.gw_mode = g_write.wave_mode = false;
    write_media_token=ufi_drive_media_token();
    g_write.gw_pre_us = 0;
    g_write.cue_index = g_write.terminate_index = true;
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
    g_write.erase_rest = false;
    g_write.start_offset = 0;
    g_write.raw_check = false;
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

/* Media generation the current/last native WRITE was bound to (END generation block, 4.5) */
uint32_t ufi_write_media_token(void) {
    return write_media_token;
}

/* flags2 bit0 (1.18): after the last interval keep WGATE on with WDATA idle (the ERASE
 * 0x23 signal) until the next index, so nothing of the old track survives behind the
 * new data.  Native WRITE only; the GW path and Apple drives (no index) never use it. */
void ufi_write_set_erase_rest(bool on) {
    g_write.erase_rest = on;
}

uint32_t ufi_write_erased_ticks(void) {
    return erased_ticks;
}

/* op 41: tolerance in 1/1000 of the source index period (0 = check off), last measurement */
void ufi_write_set_speed_check(uint16_t permille) {
    speed_tolerance_permille = permille;
}

/* Precompensation for this write (1.18): override, profile zone, or TIMING/auto with the rule;
 * all three count the physical track (double step: logical n sits at 2n). */
uint32_t ufi_write_precomp_select(uint8_t track) {
    const drive_timing_t t = ufi_drive_get_timing();
    const uint32_t phys = t.double_step ? 2u * track : track;
    uint32_t ns;
    if (precomp_override) {
        ns = precomp_override_ns; precomp_source = 2;
    } else if (precomp_zones) {
        ns = 0;
        for (uint8_t i = 0; i < precomp_zones; i++) if (phys >= precomp_zone_from[i]) ns = precomp_zone_ns[i];
        precomp_source = 1;
    } else {
        const uint16_t pc = t.precomp_ns;
        const uint32_t base = (pc == PRECOMP_AUTO) ? write_precomp_auto_ns(write_buffer, g_write.flux_count) : pc;
        ns = (phys < 40u) ? 0u : (phys > 60u) ? base : base / 2u;
        precomp_source = 0;
    }
    precomp_last_ns = (uint16_t)ns; precomp_last_track = track; precomp_last_phys = phys > 255u ? 255u : (uint8_t)phys;
    return ns;
}

void ufi_write_set_raw_check(bool on) {               /* flags2 bit4 (4.17): fingerprint read-back, no content reference needed */
    g_write.raw_check = on;
}

void ufi_write_set_weak(int32_t jitter, uint32_t seed) {   /* jitter < 0: markers off; seed 0: the board picks one */
    weak_jitter = jitter;
    weak_applied = 0;
    weak_seed = jitter < 0 ? 0u : seed ? seed : (ufi_flux_now() ^ (HAL_GetTick() * 2654435761u) ^ 0xA5A5F00Du);
    if (jitter >= 0 && !weak_seed) weak_seed = 0x9E3779B9u;
}

void ufi_write_weak_result(uint32_t* applied, uint32_t* seed) {   /* END payload (4.16) */
    *applied = weak_applied;
    *seed = weak_jitter >= 0 ? weak_seed : 0u;
}

void ufi_write_set_precomp_ns(int32_t ns) {           /* < 0: no per-write override */
    precomp_override = ns >= 0;
    precomp_override_ns = ns >= 0 ? (uint16_t)ns : 0u;
}

int ufi_write_precomp_profile_set(const uint8_t* zones, uint8_t n) {   /* n x (from_phys u8, ns u16), n 0..4 */
    if (n > 4u) return UFI_ERR_BAD_ARGS;
    uint8_t from[4]; uint16_t ns[4];
    for (uint8_t i = 0; i < n; i++) {
        from[i] = zones[3u * i]; ns[i] = (uint16_t)(zones[3u * i + 1u] | (uint16_t)zones[3u * i + 2u] << 8);
        if ((i == 0 && from[0] != 0) || (i && from[i] <= from[i - 1u]) || ns[i] > 2000u) return UFI_ERR_BAD_ARGS;
    }
    precomp_zones = n;
    memcpy(precomp_zone_from, from, n); memcpy(precomp_zone_ns, ns, n * sizeof ns[0]);
    return UFI_OK;
}

uint16_t ufi_write_precomp_profile(uint8_t* out) {   /* op 45 reply, 19 bytes */
    memset(out, 0, 19);
    out[0] = 1; out[1] = precomp_source; out[2] = precomp_zones;
    for (uint8_t i = 0; i < precomp_zones; i++) {
        out[3 + 3 * i] = precomp_zone_from[i];
        out[4 + 3 * i] = (uint8_t)(precomp_zone_ns[i] & 0xFFu); out[5 + 3 * i] = (uint8_t)(precomp_zone_ns[i] >> 8);
    }
    out[15] = (uint8_t)(precomp_last_ns & 0xFFu); out[16] = (uint8_t)(precomp_last_ns >> 8);
    out[17] = precomp_last_track; out[18] = precomp_last_phys;
    return 19;
}

void ufi_write_speed_state(uint16_t* permille, uint32_t* measured, uint32_t* source) {
    *permille = speed_tolerance_permille;
    *measured = speed_measured;
    *source = speed_source;
}

/* flags2 bit1 (1.18): gate opens at index + ticks instead of at the index (native WRITE only) */
void ufi_write_set_start_offset(uint32_t ticks) {
    g_write.start_offset = ticks;
}

uint32_t ufi_write_start_applied(void) {
    return start_applied;
}

/* WRITE_TRACK_C: byte_count bytes in the READ_TRACK stream code (ufi_stream.c; index
 * markers are not allowed), decoded on arrival into the same u32 delta buffer */
int ufi_write_prepare_compact(uint8_t track, uint8_t side, uint32_t flux_count,
                              uint32_t byte_count, bool verify) {
    /* Reject malformed sizes before clearing retained verification evidence. */
    if (byte_count < flux_count || (uint64_t)byte_count > (uint64_t)flux_count * 5u)
        return UFI_ERR_BUFFER_FULL;
    int ret = ufi_write_prepare(track, side, flux_count, verify);
    if (ret != UFI_OK) {
        return ret;
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
    if (len > g_write.bytes_expected - g_write.bytes_received) {
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
    write_start_error=UFI_OK;
    if(ufi_cancel_requested()){g_write.state=WRITE_ERROR;return UFI_ERR_CANCELLED;}
    if(!g_write.gw_mode && !ufi_drive_media_matches(write_media_token)){
        g_write.state=WRITE_ERROR;return UFI_ERR_MEDIA_CHANGED;
    }
    if (g_write.compact && !compact_complete(&g_write.dec)) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_BUFFER_FULL;         /* malformed / incomplete compact data */
    }
    g_write.apple = ufi_drive_is_apple();
    if (g_write.erase_rest && g_write.apple) {  /* no index on the Disk II write path */
        g_write.state = WRITE_ERROR;
        return UFI_ERR_NOT_IMPL;
    }
    drive_extra_t extra=ufi_drive_extra_get();
    active_pre_ticks=g_write.gw_mode?cfg_pre_ticks:(uint32_t)extra.gate_lead_us*(FLUX_TIMER_FREQ/1000000u);
    active_post_ticks=g_write.gw_mode?cfg_post_ticks:(uint32_t)extra.gate_tail_us*(FLUX_TIMER_FREQ/1000000u);
    if(!g_write.gw_mode && (g_write.apple || !g_write.wave_mode) && (active_pre_ticks || active_post_ticks)){
        g_write.state=WRITE_ERROR;return UFI_ERR_NOT_IMPL;
    }
    if(g_write.verify_after && (raw_flags&1) && !g_write.wave_mode){
        bool valid=raw_profile_build(&raw_source,write_buffer,g_write.flux_count,source_index_ticks,false);
        if(!valid && (raw_flags&2)){g_write.state=WRITE_ERROR;return UFI_ERR_BAD_ARGS;}
    }
    if(!g_write.gw_mode && (g_write.verify_after || g_write.raw_check) && ufi_precision_enabled() &&
       (!g_write.wave_mode || active_pre_ticks || active_post_ticks)){
        g_write.state=WRITE_ERROR;return UFI_ERR_NOT_IMPL;
    }
    {
        const int pret = pack_deltas();     /* before seek and WGATE: refuse, write nothing */
        if (pret != UFI_OK) {
            g_write.state = WRITE_ERROR;
            return pret;
        }
    }
    if (g_write.raw_check || (g_write.wave_mode && g_write.verify_after && (raw_flags&1))) {
        /* Fingerprint the actual waveform, after the weak pass and before DMA. */
        if (!g_write.wave_mode || !source_index_ticks ||
            !raw_profile_build(&raw_source, write_buffer, g_write.flux_count, source_index_ticks, false)) {
            g_write.state = WRITE_ERROR;
            return UFI_ERR_BAD_ARGS;
        }
    }
    if(!g_write.gw_mode && (g_write.verify_after || g_write.raw_check) && ufi_precision_enabled()){
        int ret=ufi_precision_source(write_buffer,g_write.flux_count,source_index_ticks,g_write.start_offset);
        if(ret){g_write.state=WRITE_ERROR;return ret;}
    }
    if (ufi_board_soft_locked()) {          /* session lock engaged after acceptance (op 37) */
        g_write.state = WRITE_ERROR;
        return UFI_ERR_SOFT_LOCK;
    }
    if (ufi_board_write_locked()) {         /* WRITE LOCK jumper: WGATE is blocked anyway */
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    ufi_board_write_enable(true);           /* v1.0 (A9b): WLOCK_FW low opens the hardware WGATE gate for this write */
    if (g_write.wave_mode && g_write.verify_after) {
        reference_started=HAL_GetTick();
        if (!local_reference_ready && !verify_ref_build_u32(&verify_ref,&verify_dec,write_buffer,g_write.flux_count,
                                  source_index_ticks,reference_poll)) {
            g_write.state=WRITE_ERROR;
            return ufi_cancel_requested()?UFI_ERR_CANCELLED:
                HAL_GetTick()-reference_started>=10000u?UFI_ERR_TIMEOUT:UFI_ERR_RANGE;
        }
        if (!verify_ref.n || verify_ref.overflow || verify_ref.conflicting ||
            (verify_ref.amiga && verify_ref.n!=11u && verify_ref.n!=22u)) {
            g_write.state=WRITE_ERROR;return UFI_ERR_NOT_IMPL;
        }
        if (verify_ref.amiga) for (uint8_t i=0;i<verify_ref.n;i++) {
            const verify_sector_t* s=&verify_ref.s[i];
            if (s->c!=g_write.track || s->h!=g_write.side || !s->r || s->r>verify_ref.n) {
                g_write.state=WRITE_ERROR;return UFI_ERR_BAD_ARGS;
            }
        }
        if(expected_sectors && !verify_ref_expected(&verify_ref,expected_sectors,expected_c,expected_h,expected_n)) {
            g_write.state=WRITE_ERROR;return UFI_ERR_BAD_ARGS;
        }
    }
    if (ufi_drive_write_protected()) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_WRITE_PROT;
    }
    if (speed_tolerance_permille && source_index_ticks && !g_write.gw_mode &&
        !(g_write.apple && !ufi_drive_sync_index())) {
        /* op 41: the drive's own index period against the source revolution, before any seek */
        diag_rpm_t r;
        speed_measured=0;speed_source=source_index_ticks;
        int rpm_ret=ufi_diag_rpm(2, &r);
        if(rpm_ret){g_write.state=WRITE_ERROR;return rpm_ret;}
        {
            speed_measured = r.period_avg;
            speed_source = source_index_ticks;
            const uint64_t allowed = (uint64_t)source_index_ticks * speed_tolerance_permille / 1000u;
            const uint32_t diff = r.period_avg > source_index_ticks ? r.period_avg - source_index_ticks
                                                                     : source_index_ticks - r.period_avg;
            if (diff > allowed) {
                g_write.state = WRITE_ERROR;
                return UFI_ERR_SPEED_MISMATCH;
            }
        }
    }
    const int sret = ufi_drive_seek_q(g_write.track, g_write.quarter);
    if (sret != 0) {
        g_write.state = WRITE_ERROR;
        return sret == UFI_ERR_BAD_ARGS ? UFI_ERR_BAD_ARGS : UFI_ERR_SEEK_FAIL;
    }
    int side_ret=ufi_drive_select_side(g_write.side);
    if(side_ret){g_write.state=WRITE_ERROR;return side_ret;}
    if(!g_write.gw_mode && !ufi_drive_media_matches(write_media_token)){
        g_write.state=WRITE_ERROR;return UFI_ERR_MEDIA_CHANGED;
    }

    /* Timer: first two intervals by hand (UG loads the shadow ARR), DMA feeds the rest */
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    if (active_pre_ticks && !g_write.apple && !g_write.wave_mode) {
        /* gw pre_write: WGATE goes on at the index, the first flux comes that much later */
        const uint32_t a = (uint32_t)write_arr[0] + 1u + active_pre_ticks;
        if(!g_write.gw_mode && a>WRITE_MAX_DELTA){g_write.state=WRITE_ERROR;return UFI_ERR_RANGE;}
        write_arr[0] = (uint16_t)((a > WRITE_MAX_DELTA ? WRITE_MAX_DELTA : a) - 1u);
    }
    post_pending = false;
    index_cut = false;
    erase_pending = false;
    erased_ticks = 0;
    start_pending = false;
    start_applied = 0;
    wave_item_t first, second;
    if (g_write.wave_mode) {
        wave_next(&wave_cursor, &first);
        wave_next(&wave_cursor, &second);
        TIM3->CR1 &= ~TIM_CR1_DIR;
        TIM3->CCMR1 |= TIM_CCMR1_OC1PE;
        TIM3->DCR = TIM_DMABASE_ARR | TIM_DMABURSTLENGTH_3TRANSFERS;
        TIM3->CCR1 = first.ccr;
    } else {
        TIM3->CR1 |= TIM_CR1_DIR;
        TIM3->CCMR1 &= ~TIM_CCMR1_OC1PE;
        TIM3->CCR1 = WRITE_PULSE_TICKS - 1u;
    }
    TIM3->ARR = g_write.wave_mode ? first.arr : write_arr[0];
    TIM3->EGR = TIM_EGR_UG;                 // shadow ARR = interval 0, CNT = ARR (down)
    TIM3->SR = 0;
    TIM3->ARR = g_write.wave_mode ? second.arr : write_arr[1];
    if (g_write.wave_mode) TIM3->CCR1 = second.ccr;
    if (g_write.apple) {
        /* Disk II: WRDATA level change at every update (CNT reaches CCR2 = 0 once per
         * interval) on TIM3_CH2 / PA7; the 34-pin WDATA (CH1) stays idle */
        apple_wrdata(true);
    } else {
        TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) |
                      (g_write.wave_mode ? TIM_OCMODE_PWM2 : TIM_OCMODE_PWM1);
    }

    /* ring: the first two halves of the stream; the DMA circles over the ring and the
     * HT/TC interrupts refill the half it has left (ufi_write_stream.c) */
    wstream_fill_t fills[2];
    const uint32_t half = g_write.wave_mode ? WAVE_HALF : WSTREAM_HALF;
    const uint32_t stride = g_write.wave_mode ? 3u : 1u;
    /* One additional silent preload prevents a repeated last pulse even when the
     * completion ISR is delayed. Stop one update after its DMA transfer. */
    wstream_init(&ws, g_write.wave_mode ? wave_segments - 1u : g_write.flux_count - 2u, half, fills);
    ring_fill(&fills[0]);
    ring_fill(&fills[1]);
    dma_stop();
    if (HAL_DMA_Start_IT(&hdma_tim3_up, (uint32_t)write_ring,
                         (uint32_t)(g_write.wave_mode ? &TIM3->DMAR : &TIM3->ARR),
                         2u * half * stride) != HAL_OK) {
        wdata_idle();
        apple_wrdata(false);
        g_write.state = WRITE_ERROR;
        return UFI_ERR_DMA;
    }
    DMA1_Stream1->CR |= DMA_SxCR_HTIE;      /* HAL arms TC/TE only */
    last_ndtr = 2u * half * stride;
    if (ws.tail) {                          /* short stream: count every update */
        TIM3->DIER |= TIM_DIER_UIE;
    }
    ufi_delay_us(g_write.gw_mode?20000u:extra.write_settle_us);
    if (g_write.gw_pre_us) ufi_delay_us(g_write.gw_pre_us);
    if (ufi_cancel_requested()) {ufi_write_abort();return UFI_ERR_CANCELLED;}
    if(!g_write.gw_mode && !ufi_drive_media_matches(write_media_token)){
        ufi_write_abort();return UFI_ERR_MEDIA_CHANGED;
    }

    led_set(&PIN_LED_FDD, true);
    g_write.wait_since = HAL_GetTick();
    if (g_write.apple && !ufi_drive_sync_index()) {
        /* no index pulse: start right away, /WRREQ low; ends after the last interval */
        start_now(TIM2->CNT);
        return write_start_error;
    }
    g_write.wait_since = HAL_GetTick();
    const uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF);
    g_write.state = WRITE_WAITING_INDEX;
    if (g_write.cue_index || g_write.terminate_index) TIM2->DIER |= TIM_DIER_CC2IE;
    else TIM2->DIER &= ~TIM_DIER_CC2IE;
    if (!g_write.cue_index) ufi_write_index_handler(TIM2->CNT);
    __set_PRIMASK(irq_mask);
    return UFI_OK;
}

static void dma_stop(void) {
    DMA1_Stream1->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream1->CR & DMA_SxCR_EN) {
    }
    DMA1->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 |
                  DMA_LIFCR_CFEIF1;
    hdma_tim3_up.State = HAL_DMA_STATE_READY;
    __HAL_UNLOCK(&hdma_tim3_up);
}

/* copy stream items into a ring half (the DMA reads SRAM2 behind the data cache) */
static void ring_fill(const wstream_fill_t* f) {
    if (f->count == 0) {
        return;
    }
    if (g_write.wave_mode) {
        wave_item_t* dst = (wave_item_t*)&write_ring[(uint32_t)f->half * WAVE_HALF * 3u];
        for (uint32_t i = 0; i < f->count; i++)
            if (!wave_next(&wave_cursor, &dst[i])) dst[i]=(wave_item_t){65534,0,65535};
        SCB_CleanDCache_by_Addr((uint32_t*)dst, (int32_t)(WAVE_HALF * sizeof(*dst)));
        return;
    }
    uint16_t* dst = &write_ring[(uint32_t)f->half * WSTREAM_HALF];
    memcpy(dst, &write_arr[2u + f->from], (size_t)f->count * 2u);
    SCB_CleanDCache_by_Addr((uint32_t*)dst, (int32_t)(WSTREAM_HALF * 2u));
}

static void write_finish(write_state_t final_state) {
    /* End the destructive signal before waiting for DMA to acknowledge its stop. */
    bus_out(&PIN_FDD_WGATE, false);
    HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
    ufi_board_write_enable(false);          /* v1.0 (A9b): hardware WGATE gate locked again */
    if (g_write.state == WRITE_WAITING_INDEX || g_write.state == WRITE_ACTIVE) {
        TIM2->DIER &= ~TIM_DIER_CC2IE;
    }
    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    dma_stop();
    wdata_idle();
    apple_wrdata(false);
    led_set(&PIN_LED_FDD, false);
    erase_pending = false;
    TIM2->DIER &= ~TIM_DIER_CC4IE;          /* a pending delayed start dies with the write */
    start_pending = false;
    g_write.state = final_state;
    diagnostic_write=false;
}

/* Open the gate and start the write timer at flux-timer time t: at the index / sensor
 * pulse, or at index + start_offset from the compare-channel interrupt. */
static void start_now(uint32_t t) {
    /* EP0 cancellation can arrive after main-loop preparation, before INDEX/CC4.
     * Only IRQ-safe flag/GPIO reads here; media observation stays in main context. */
    int ret=ufi_cancel_requested()?UFI_ERR_CANCELLED:
        ufi_board_soft_locked()?UFI_ERR_SOFT_LOCK:
        (ufi_board_write_locked() || ufi_drive_write_protected())?UFI_ERR_WRITE_PROT:UFI_OK;
    if(ret){write_start_error=ret;write_finish(WRITE_ERROR);return;}
    if (g_write.apple) {
        /* Disk II with sync sensor: start at the sensor pulse (track alignment kept);
         * the end comes from the last interval, not from the next pulse */
        HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_RESET);
    } else {
        bus_out(&PIN_FDD_WGATE, true);
    }
    g_write.state = WRITE_ACTIVE;
    g_write.updates_left = 0;
    TIM3->DIER |= TIM_DIER_UDE;             // DMA: next interval on every update
    __HAL_TIM_ENABLE(&htim3);               // first pulse after interval 0
    ufi_sched_arm(t);
    if (!g_write.apple && !g_write.terminate_index) TIM2->DIER &= ~TIM_DIER_CC2IE;
}

/* TIM2 compare channel 4 (ufi_flux_tim2_irq): index + start_offset reached */
void ufi_write_delayed_start(uint32_t t) {
    if (g_write.state != WRITE_WAITING_INDEX || !start_pending) return;
    start_pending = false;
    start_applied = t - start_index_time;   /* measured: compare latency included */
    start_now(t);
}

/* Called from the TIM2 index-capture interrupt */
void ufi_write_index_handler(uint32_t t) {
    if(diagnostic_write)return; /* timed ERASE/PATTERN owns its duration */
    if (g_write.state == WRITE_WAITING_INDEX) {
        if (start_pending) return;          /* another index while waiting for index + offset */
        if (g_write.start_offset) {
            start_index_time = t;
            start_pending = true;
            TIM2->CCR4 = t + g_write.start_offset;
            TIM2->SR = ~TIM_SR_CC4IF;
            TIM2->DIER |= TIM_DIER_CC4IE;
            return;
        }
        start_now(t);
        return;
    }
    if (g_write.apple) {
        return;                             /* Disk II: no index safety stop */
    }
    if (g_write.state == WRITE_ACTIVE && g_write.terminate_index) {
        if (erase_pending) {
            erased_ticks = t - erase_start;  /* the erased tail ends at this index */
            write_finish(WRITE_COMPLETE);
            return;
        }
        if (g_write.gw_mode) {
            write_finish(WRITE_COMPLETE);   /* requested index termination is success */
            return;
        }
        index_cut = true;                   /* the track did not fit one revolution */
        write_finish(WRITE_ERROR);          /* never write past the index, never call it success */
    }
}

/* All data is out.  flags2 bit0 (1.18): keep WGATE on with WDATA idle, the same signal
 * ERASE 0x23 produces, until the next index finishes the write; otherwise the gate drops
 * now as before. */
static void data_done(void) {
    if (g_write.erase_rest && !g_write.apple && !g_write.gw_mode) {
        wdata_idle();
        __HAL_TIM_DISABLE(&htim3);
        TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
        dma_stop();
        erase_start = TIM2->CNT;
        erase_pending = true;               /* ufi_write_index_handler() completes */
        return;
    }
    write_finish(WRITE_COMPLETE);
}

/* The last period has ended: gw post_write keeps WGATE on for one silent period */
static void end_of_data(void) {
    if (active_post_ticks && !g_write.apple && !post_pending) {
        post_pending = true;
        TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_FORCED_INACTIVE;
        TIM3->ARR = active_post_ticks - 1u;    /* after the running (stale) period */
        g_write.updates_left = 2;
    } else {
        data_done();
    }
}

/* Timer update: only enabled from the half before the last one on (and for the post
 * period).  The DMA counter tells how many items went in; the arithmetic in
 * ufi_write_stream.c arms the two trailing updates after the last item. */
void ufi_write_tim3_irq(void) {
    if (!(TIM3->SR & TIM_SR_UIF)) {
        return;
    }
    TIM3->SR = ~TIM_SR_UIF;
    if (g_write.state != WRITE_ACTIVE) {
        return;
    }
    if (post_pending) {
        if (--g_write.updates_left == 0) {
            data_done();
        }
        return;
    }
    if (!ws.armed && (TIM3->DIER & TIM_DIER_UDE)) {
        /* this update's DMA transfer may still be in flight: wait for the counter */
        uint32_t spins = 0;
        while ((DMA1_Stream1->NDTR == last_ndtr ||
                (g_write.wave_mode && DMA1_Stream1->NDTR % 3u)) && ++spins < 4000u) {
        }
    }
    const uint32_t ndtr = DMA1_Stream1->NDTR;
    last_ndtr = ndtr;
    switch (wstream_update(&ws, g_write.wave_mode ? (ndtr + 2u) / 3u : ndtr)) {
        case WSTREAM_STOP_DMA:
            TIM3->DIER &= ~TIM_DIER_UDE;    /* last item is in: no more transfers */
            if(g_write.wave_mode) ws.updates_left=1; /* silent sentinel follows last pulse */
            break;
        case WSTREAM_FINISH:
            end_of_data();
            break;
        default:
            break;
    }
}

/* DMA half / transfer complete: refill the half just played; both flags pending or the
 * next one pending after the copy means a half was missed (underrun -> error, WGATE off) */
void ufi_write_dma_irq(void) {
    const uint32_t isr = DMA1->LISR;
    if (isr & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) {
        ufi_metrics.write_dma_errors++;
        write_finish(WRITE_ERROR);          /* dma_stop() clears the flags */
        return;
    }
    if (g_write.state != WRITE_ACTIVE) {
        DMA1->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1;
        return;
    }
    if ((isr & DMA_LISR_HTIF1) && (isr & DMA_LISR_TCIF1)) {
        ufi_metrics.write_underruns++;
        write_finish(WRITE_ERROR);          /* two halves pending: refill came too late */
        return;
    }
    const bool tc = (isr & DMA_LISR_TCIF1) != 0;
    DMA1->LIFCR = tc ? DMA_LIFCR_CTCIF1 : DMA_LIFCR_CHTIF1;
    const wstream_fill_t f = wstream_event(&ws, tc);
    if (ws.underrun) {
        ufi_metrics.write_underruns++;
        write_finish(WRITE_ERROR);
        return;
    }
    ring_fill(&f);
    if (DMA1->LISR & (tc ? DMA_LISR_HTIF1 : DMA_LISR_TCIF1)) {
        ufi_metrics.write_underruns++;
        write_finish(WRITE_ERROR);          /* the DMA ran into the half being filled */
        return;
    }
    if (ws.tail && !(TIM3->DIER & TIM_DIER_UIE)) {
        last_ndtr = DMA1_Stream1->NDTR;
        TIM3->DIER |= TIM_DIER_UIE;         /* count the updates from here on */
    }
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

/* Blocking diagnostics must honour EP0 cancel/disconnect while the main loop is
 * occupied. Recheck physical interlocks and the same medium throughout. */
static int blocking_write_guard(uint32_t media) {
    if(ufi_cancel_requested())return UFI_ERR_CANCELLED;
    if(ufi_board_soft_locked())return UFI_ERR_SOFT_LOCK;
    if(ufi_board_write_locked() || ufi_drive_write_protected())return UFI_ERR_WRITE_PROT;
    if(!ufi_drive_motor_is_on())return UFI_ERR_NO_DRIVE;
    if(!ufi_drive_media_matches(media))return UFI_ERR_MEDIA_CHANGED;
    return UFI_OK;
}

static int blocking_write_wait(uint32_t ms,uint32_t media) {
    const uint32_t start=HAL_GetTick();
    int ret;
    do {
        ret=blocking_write_guard(media);if(ret)return ret;
        UFI_WATCHDOG_FEED();
    } while(HAL_GetTick()-start<ms);
    return blocking_write_guard(media);
}

int ufi_erase_track_ms(uint8_t track, uint8_t side, uint32_t ms) {
    if(g_write.state!=WRITE_IDLE || g_capture.state==CAPTURE_WAITING_INDEX ||
       g_capture.state==CAPTURE_RUNNING || ufi_stream_active())return UFI_ERR_BUSY;
    if (ms < 1u) ms = 1u;
    if (ms > 5000u) ms = 5000u;
    const uint32_t media=ufi_drive_media_token();
    int ret=blocking_write_guard(media);if(ret)return ret;
    ret=ufi_drive_seek(track);if(ret)return ret;
    ret=ufi_drive_select_side(side);if(ret)return ret;
    ret=blocking_write_wait(20,media);if(ret)return ret;

    if(!ufi_drive_is_apple()) {
        const uint32_t start=HAL_GetTick();
        /* Leave an existing pulse, then wait for its next leading edge. */
        for(unsigned phase=0;phase<2;phase++) {
            while(ufi_flux_index_asserted()==(phase==0)) {
                ret=blocking_write_guard(media);if(ret)return ret;
                UFI_WATCHDOG_FEED();
                if(HAL_GetTick()-start>500u)return UFI_ERR_NO_INDEX;
            }
        }
    }
    ret=blocking_write_guard(media);if(ret)return ret;
    wdata_idle();
    led_set(&PIN_LED_FDD,true);diagnostic_write=true;g_write.state=WRITE_ACTIVE;
    if(ufi_drive_is_apple())HAL_GPIO_WritePin(PIN_APL_WRREQ.port,PIN_APL_WRREQ.pin,GPIO_PIN_RESET);
    else bus_out(&PIN_FDD_WGATE,true); /* no WDATA pulses = DC erase */
    ret=blocking_write_wait(ms,media);
    write_finish(WRITE_IDLE);
    return ret;
}

/* ============================================================================
 * VERIFY
 * ============================================================================ */

/* Verify with evidence levels (ufi_verify.c, 1.15): the packed track is decoded into a
 * sector reference (ids + data CRC-32) before the read-back reuses the flux store; two
 * revolutions are read back and every reference sector must come back with the same
 * data (level 3).  Tracks the decoder does not understand (GCR, protections) only get
 * the transition-count check (level 2).  Anything else fails with UFI_ERR_DMA (6). */
static int write_verify_result(uint8_t level) {
    int ret=ufi_cancel_requested()?UFI_ERR_CANCELLED:
        !ufi_drive_media_matches(write_media_token)?UFI_ERR_MEDIA_CHANGED:UFI_OK;
    g_write.verify_level=ret?VERIFY_NONE:level;
    g_write.state=ret?WRITE_ERROR:WRITE_IDLE;
    return ret;
}

static void write_verify_cancel(void){
    if(!verify_phase)return;
    if(verify_phase==VERIFY_SECTORS)ufi_quality_async_cancel();
    ufi_precision_verify_cancel();
    if(g_capture.state==CAPTURE_WAITING_INDEX || g_capture.state==CAPTURE_RUNNING)ufi_capture_abort();
    verify_phase=0;
}
static int write_verify_end(int ret,uint8_t level){
    if(ret){write_verify_cancel();g_write.verify_level=(ret==UFI_ERR_MEDIA_CHANGED || ret==UFI_ERR_CANCELLED)?
        VERIFY_NONE:VERIFY_WRITE_COMPLETED;g_write.state=WRITE_ERROR;return ret;}
    verify_phase=0;return write_verify_result(level);
}
static int write_verify_begin(void) {
    if (g_write.state != WRITE_COMPLETE) {
        return UFI_ERR_BUSY;
    }
    if(!ufi_drive_media_matches(write_media_token)){
        g_write.verify_level=VERIFY_NONE;g_write.state=WRITE_ERROR;return UFI_ERR_MEDIA_CHANGED;
    }
    g_write.state = WRITE_VERIFYING;
    g_write.verify_level=VERIFY_WRITE_COMPLETED;
    if (!g_write.wave_mode && !local_reference_ready) verify_ref_build(&verify_ref, &verify_dec, write_arr, g_write.flux_count);

    int ret = ufi_capture_start_q(g_write.track, g_write.side, 2, 0, g_write.quarter);
    if (ret != 0) {
        g_write.state = WRITE_ERROR;
        return ret;
    }
    verify_phase=VERIFY_CAPTURE;verify_since=HAL_GetTick();return UFI_OK;
}
static int write_verify_step_impl(void){
    if(!verify_phase || g_write.state!=WRITE_VERIFYING)return UFI_ERR_BUSY;
    UFI_WATCHDOG_FEED();
    ufi_metric_add(&ufi_metrics.verify_steps,1);
    if(ufi_cancel_requested())return write_verify_end(UFI_ERR_CANCELLED,0);
    if(!ufi_drive_media_matches(write_media_token))return write_verify_end(UFI_ERR_MEDIA_CHANGED,0);
    if(HAL_GetTick()-verify_since>(verify_phase==VERIFY_CAPTURE?1500u:10000u))return write_verify_end(UFI_ERR_TIMEOUT,0);
    if(verify_phase==VERIFY_CAPTURE){
        capture_state_t state=ufi_capture_get_state();
        if(state==CAPTURE_ERROR)return write_verify_end(g_capture.error_code==4?UFI_ERR_MEDIA_CHANGED:
            g_capture.error_code==2?UFI_ERR_DMA:g_capture.error_code==3?UFI_ERR_NO_INDEX:UFI_ERR_BUFFER_FULL,0);
        if(state!=CAPTURE_COMPLETE)return UFI_ERR_BUSY;
    const uint8_t revs = ufi_flux_get_revolution_count();
    if (revs < 2u || g_capture.error_code) {
        return write_verify_end(UFI_ERR_DMA,0);
    }
    const flux_revolution_t* r0 = ufi_flux_get_revolution(0);
    if(!r0)return write_verify_end(UFI_ERR_DMA,0);
    uint32_t diff=r0->count>g_write.flux_count?r0->count-g_write.flux_count:g_write.flux_count-r0->count;
    uint32_t tolerance=g_write.flux_count/20u;if(tolerance<100)tolerance=100;
    verify_count_ok=diff<=tolerance;
    g_capture.state=CAPTURE_IDLE;
    int ret=g_write.gw_mode?UFI_OK:ufi_precision_verify_begin();
    if(ret)return write_verify_end(ret,0);
    verify_phase=VERIFY_PRECISION;verify_since=HAL_GetTick();return UFI_ERR_BUSY;
    }
    if(verify_phase==VERIFY_PRECISION){
        int ret=g_write.gw_mode?UFI_OK:ufi_precision_verify_step();
        if(ret==UFI_ERR_BUSY)return ret;
        if(ret)return write_verify_end(ret,0);
        if(g_write.raw_check || (raw_flags&1)){
            const flux_revolution_t* r0=ufi_flux_get_revolution(0);
            if(!r0)return write_verify_end(UFI_ERR_DMA,0);
            /* An invalid raw profile is still reported as unavailable, as before. */
            (void)raw_profile_begin(&verify_observed,&verify_raw_builder,(const uint32_t*)r0->samples,r0->count,r0->index_time,true);
            verify_phase=VERIFY_RAW;verify_since=HAL_GetTick();return UFI_ERR_BUSY;
        }
        verify_phase=VERIFY_SECTORS;
        if(verify_ref.n){ret=ufi_quality_async_verify(&verify_ref,verify_count_ok);if(ret)return write_verify_end(ret,0);}
        verify_since=HAL_GetTick();return UFI_ERR_BUSY;
    }
    if(verify_phase==VERIFY_RAW){
        int step=raw_profile_step(&verify_observed,&verify_raw_builder,256);if(!step)return UFI_ERR_BUSY;
        bool matches=raw_profile_compare(&raw_source,&verify_observed,raw_tolerance,raw_report);
        raw_available=true;
        if(!matches && (raw_flags&2))return write_verify_end(UFI_ERR_DMA,0);
        if(g_write.raw_check)return write_verify_end(UFI_OK,matches?VERIFY_READBACK_PLAUSIBLE:VERIFY_WRITE_COMPLETED);
        verify_phase=VERIFY_SECTORS;verify_since=HAL_GetTick();
        if(verify_ref.n){int ret=ufi_quality_async_verify(&verify_ref,verify_count_ok);if(ret)return write_verify_end(ret,0);}
        return UFI_ERR_BUSY;
    }
    uint8_t level=verify_count_ok?VERIFY_READBACK_PLAUSIBLE:VERIFY_NONE;
    int ret=UFI_OK;
    if(verify_ref.n){
        ret=ufi_quality_async_step();if(ret==UFI_ERR_BUSY)return ret;
        level=ufi_quality_async_verify_level();
    }
    return write_verify_end(ret?ret:!level?UFI_ERR_DMA:UFI_OK,level);
}
static int write_verify_step(void){
    ufi_profile_mark_t profile=ufi_profile_begin();
    int ret=write_verify_step_impl();
    ufi_profile_end(UFI_CYCLE_VERIFY,profile);return ret;
}
int ufi_write_verify(void) {
    int ret=write_verify_begin();if(ret)return ret;
    do{ret=write_verify_step();}while(ret==UFI_ERR_BUSY);
    return ret;
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
        uint32_t done = wstream_done(&ws, g_write.wave_mode ? (DMA1_Stream1->NDTR+2u)/3u : DMA1_Stream1->NDTR);
        if (done > ws.n) done = ws.n;
        return ws.n ? (uint32_t)((uint64_t)done * 100u / ws.n) : 0u;
    }
    return 0;
}

void ufi_write_abort(void) {
    write_verify_cancel();
    write_finish(WRITE_IDLE);
    g_write.flux_count = 0;
    g_write.bytes_received = 0;
}

void ufi_write_set_precomp(bool enable) {
    g_write.use_precomp = enable;
}

static uint8_t write_event_cmd(void);

/* v1 data path (ufi_usb.c): a bad compact byte ends the write with an error event */
void ufi_write_receive_failed(void) {
    g_write.state = WRITE_ERROR;
    receive_failed = true;
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
    if(!g_write.gw_mode && g_write.state!=WRITE_IDLE && !ufi_drive_media_matches(write_media_token)){
        uint8_t cmd=write_event_cmd();ufi_write_abort();ufi_usb_send_event(cmd,UFI_ERR_MEDIA_CHANGED);return;
    }
    if (receive_failed) {
        const uint8_t cmd = write_event_cmd();
        ufi_write_abort();
        receive_failed = false;
        ufi_usb_send_event(cmd, UFI_ERR_BUFFER_FULL);
        return;
    }
    if (ufi_write_data_complete()) {
        if (ufi_job_live() && ufi_job.kind==2 && ufi_job.state!=JOB_READY) return;
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
    if (g_write.state == WRITE_ACTIVE && HAL_GetTick()-g_write.wait_since>write_timeout_ms) {
        const uint8_t cmd=write_event_cmd();
        ufi_write_abort();ufi_usb_send_event(cmd,UFI_ERR_TIMEOUT);return;
    }
    if (g_write.state == WRITE_COMPLETE || g_write.state == WRITE_ERROR || g_write.state==WRITE_VERIFYING) {
        const uint8_t cmd = write_event_cmd();
        int ret = write_start_error ? write_start_error : index_cut ? UFI_ERR_TRACK_LONG : UFI_ERR_DMA;
        if (g_write.state == WRITE_COMPLETE) {
            g_write.verify_level = 1;           /* timer and DMA finished */
            if(g_write.verify_after || g_write.raw_check){ret=write_verify_begin();if(!ret)return;}
            else ret=UFI_OK;
        }else if(g_write.state==WRITE_VERIFYING){
            ret=write_verify_step();if(ret==UFI_ERR_BUSY)return;
        }
        g_capture.state = CAPTURE_IDLE;     /* verify capture is internal, not sent */
        g_write.state = WRITE_IDLE;
        ufi_usb_send_event(cmd, ret);
    }
}

/* Firmware-generated track (USB floppy mode, ufi_floppy.c): the caller has put
 * flux_count u32 deltas into the flux store (ufi_flux_store).  Blocking until the track
 * is written (index wait + one revolution); no USB events, the caller verifies. */
static int write_local_run(void) {
    g_write.bytes_received = g_write.bytes_expected;    /* data is already in place */
    int ret = ufi_write_start();
    if (ret == UFI_OK) {
        const uint32_t t0 = HAL_GetTick();
        while (g_write.state == WRITE_WAITING_INDEX || g_write.state == WRITE_ACTIVE) {
            if(!ufi_drive_media_matches(write_media_token)){ret=UFI_ERR_MEDIA_CHANGED;break;}
            if (ufi_cancel_requested()) {ret=UFI_ERR_CANCELLED;break;}
            UFI_WATCHDOG_FEED();
            if (HAL_GetTick() - t0 > write_timeout_ms) {
                ret = UFI_ERR_NO_INDEX;
                break;
            }
        }
        if (ret == UFI_OK && g_write.state != WRITE_COMPLETE) {
            ret = write_start_error ? write_start_error : UFI_ERR_DMA;
        }
    }
    if(ret==UFI_OK && g_write.verify_after){
        ret=ufi_write_verify();g_capture.state=CAPTURE_IDLE;
    }
    if (ret != UFI_OK) ufi_write_abort();
    g_write.state = WRITE_IDLE;
    return ret;
}

int ufi_write_local_extended(uint8_t track,uint8_t side,uint32_t count,uint32_t index_ticks){
    if(ufi_drive_is_apple() || !index_ticks)return UFI_ERR_NOT_IMPL;
    int ret=ufi_write_prepare(track,side,count,true);if(ret)return ret;
    g_write.wave_mode=true;source_index_ticks=index_ticks;
    return write_local_run();
}

int ufi_write_local_checked(uint8_t track,uint8_t side,uint32_t count,bool extended,
                            uint32_t index_ticks,uint64_t expected,uint8_t expected_head,uint8_t* level){
    *level=VERIFY_NONE;
    if(ufi_drive_is_apple())return UFI_ERR_NOT_IMPL;
    int ret=ufi_write_prepare(track,side,count,true);if(ret)return ret;
    g_write.wave_mode=extended;source_index_ticks=index_ticks;
    /* Snapshot original u32 source before pack_deltas() or readback can alter it. */
    reference_started=HAL_GetTick();
    if(!verify_ref_build_u32(&verify_ref,&verify_dec,write_buffer,count,index_ticks,reference_poll)){
        ufi_write_abort();return ufi_cancel_requested()?UFI_ERR_CANCELLED:
            HAL_GetTick()-reference_started>=10000u?UFI_ERR_TIMEOUT:UFI_ERR_RANGE;
    }
    if(verify_ref.overflow || verify_ref.conflicting ||
       (expected && !verify_ref_expected(&verify_ref,expected,track,expected_head,2))){
        ufi_write_abort();return UFI_ERR_BAD_ARGS;
    }
    local_reference_ready=true;
    ret=write_local_run();*level=g_write.verify_level;
    if(!ret && expected && *level!=VERIFY_CONTENT)return UFI_ERR_DMA;
    return ret;
}

int ufi_write_local(uint8_t track, uint8_t side, uint32_t flux_count, bool clamp) {
    const int ret = ufi_write_prepare(track, side, flux_count, false);
    if (ret != UFI_OK) return ret;
    g_write.clamp = clamp;
    return write_local_run();
}

int ufi_write_local_gw(uint8_t track, uint8_t side, uint32_t flux_count,
                       bool cue_index, bool terminate_index, uint16_t pre_us) {
    const int ret = ufi_write_prepare(track, side, flux_count, false);
    if (ret != UFI_OK) return ret;
    g_write.gw_mode = g_write.wave_mode = true;
    g_write.gw_pre_us = pre_us;
    g_write.cue_index = cue_index;
    g_write.terminate_index = terminate_index;
    return write_local_run();            /* strict intervals; never clamp GW data */
}

/* ============================================================================
 * DIAGNOSE: DAUERMUSTER
 * ============================================================================ */

/* Write one constant flux interval for duration_ms (write chain on the scope, head
 * alignment).  Same guards as a track write: write lock jumper, write protect, idle
 * write/capture path, motor running.  Blocking; duration capped at 5 s. */
int ufi_write_pattern(uint8_t track, uint8_t side, uint16_t interval_ns, uint16_t duration_ms) {
    if (g_write.state != WRITE_IDLE ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING || ufi_stream_active()) {
        return UFI_ERR_BUSY;
    }
    const uint32_t media=ufi_drive_media_token();
    int ret=blocking_write_guard(media);if(ret)return ret;
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
    if(!duration_ms)duration_ms=1;
    ret=ufi_drive_seek(track);if(ret)return ret;
    ret=ufi_drive_select_side(side);if(ret)return ret;
    ret=blocking_write_wait(20,media);if(ret)return ret;

    __HAL_TIM_DISABLE(&htim3);
    TIM3->DIER &= ~(TIM_DIER_UDE | TIM_DIER_UIE);
    /* Waveform writes leave up-counting and an arbitrary CCR/preload behind. */
    TIM3->CR1 |= TIM_CR1_DIR;
    TIM3->CCMR1 &= ~TIM_CCMR1_OC1PE;
    TIM3->CCR1 = WRITE_PULSE_TICKS-1u;
    TIM3->ARR = ticks - 1u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~TIM_CCMR1_OC1M) | TIM_OCMODE_PWM1;
    led_set(&PIN_LED_FDD, true);
    diagnostic_write=true;
    g_write.state=WRITE_ACTIVE;
    bus_out(&PIN_FDD_WGATE, true);
    __HAL_TIM_ENABLE(&htim3);

    ret=blocking_write_wait(duration_ms,media);
    write_finish(WRITE_IDLE);
    return ret;
}

void ufi_write_set_extended(bool on) { g_write.wave_mode=on; }
void ufi_write_set_source_index(uint32_t ticks) {source_index_ticks=ticks;}
void ufi_write_set_expected(uint64_t sectors,uint8_t c,uint8_t h,uint8_t n) {
    expected_sectors=sectors;expected_c=c;expected_h=h;expected_n=n;
}
/* Reference inventory, not a second verification verdict. END supplies the level. */
uint16_t ufi_write_coverage(uint8_t* out) {
    memset(out,0,32+VERIFY_MAX_SECTORS*8);
    out[0]=1;out[1]=g_write.verify_level;out[2]=expected_sectors?2:verify_ref.n?1:0;
    out[3]=verify_ref.n;out[4]=verify_ref.format?verify_ref.format:!verify_ref.n?0:verify_ref.amiga?2:1;
    out[5]=verify_ref.overflow;out[6]=verify_ref.conflicting;
    memcpy(out+8,&expected_sectors,8);out[16]=expected_c;out[17]=expected_h;out[18]=expected_n;
    for(unsigned i=0;i<verify_ref.n;i++) {
        const verify_sector_t* s=&verify_ref.s[i];uint8_t* p=out+32+8*i;
        p[0]=s->c;p[1]=s->h;p[2]=s->r;p[3]=s->n;memcpy(p+4,&s->crc,4);
    }
    return (uint16_t)(32+verify_ref.n*8);
}
