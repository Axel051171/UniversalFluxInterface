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
#include "ufi_metrics.h"
#include "ufi_evidence.h"
#include "ufi_read_info.h"
#include "ufi_media.h"
static uint32_t capture_media_token;
#include <string.h>

extern capture_context_t g_capture;

/* ============================================================================
 * TIMER, DMA, BUFFER
 * ============================================================================ */

TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2;

__attribute__((section(".axi_sram"), aligned(32)))
static uint32_t flux_fallback[FLUX_STORE_FALLBACK_WORDS];

/* Reserve one FULL DMA chunk inside the store as the overrun target. A small
 * separate buffer cannot contain NDTR transfers while an interrupt is delayed. */
static uint32_t* dma_scratch;

static uint32_t* flux_store = flux_fallback;
static uint32_t store_words = FLUX_STORE_FALLBACK_WORDS;
static bool store_psram;
static bool reference_reserved;

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
static uint32_t store_generation;
static uint32_t archive_fingerprint;
static uint8_t archive_info[8];
static volatile bool index_internal;    /* synthetic index ignores the physical channel */
static volatile uint8_t capture_loss_flags; /* bit0 RDATA, bit1 INDEX */
static bool loss_ready,loss_hashed;
static uint32_t loss_first,loss_count,loss_hash;
static uint8_t loss_position[4];
/* READ info (READ flag bit5, 4.24): the loss checks note the last moment they saw the capture
 * intact (flux-timer stamp and stored samples), the halt notes the detection stamp.  DTCM: the
 * AXI SRAM budget stays unchanged; set at every capture start, never read before. */
__attribute__((section(".dtcm"))) static volatile uint32_t loss_clean_time,loss_clean_pos,loss_seen_time;
void ufi_flux_archive_info(uint8_t out[8]) {
    memcpy(out,archive_info,8);
}
void ufi_flux_invalidate(void) {
    finalised=false;
    loss_ready=loss_hashed=false;
    if(!++store_generation)store_generation=1;
}
uint32_t ufi_flux_archive_id(void) {
    return finalised && !g_capture.error_code?store_generation:0;
}
uint32_t ufi_flux_archive_media(void) {
    return capture_media_token;
}
uint32_t ufi_flux_archive_fingerprint(void) {
    return archive_fingerprint;
}
/* Explicitly incomplete, absolute timestamps. Never published as a clean archive
 * or passed to a decoder that expects index-relative, complete revolutions. */
bool ufi_flux_loss_snapshot(uint32_t info[4],uint8_t position[4]){
    memset(info,0,4*sizeof(*info));memset(position,0,4);
    if(!loss_ready)return false;
    if(!loss_hashed){
        SCB_InvalidateDCache_by_Addr(flux_store,(int32_t)(store_words*sizeof(*flux_store)));
        uint32_t hash=2166136261u;
        hash=(hash^store_generation)*16777619u;hash=(hash^loss_first)*16777619u;
        hash=(hash^loss_count)*16777619u;
        for(unsigned k=0;k<4;k++)hash=(hash^loss_position[k])*16777619u;
        for(uint32_t i=0;i<loss_count;i++){
            hash=(hash^flux_store[(loss_first+i)%store_words])*16777619u;
            if(!(i&1023u)){UFI_WATCHDOG_FEED();}
        }
        loss_hash=(hash^capture_media_token)*16777619u;loss_hashed=true;
    }
    info[0]=store_generation;info[1]=loss_count;info[2]=loss_hash;info[3]=loss_first;
    memcpy(position,loss_position,4);return true;
}
bool ufi_flux_loss_read(uint32_t offset,uint16_t count,uint8_t* out){
    if(!loss_ready || count>128 || offset>loss_count || count>loss_count-offset || (!out && count))return false;
    if(!loss_hashed){uint32_t info[4];uint8_t position[4];if(!ufi_flux_loss_snapshot(info,position))return false;}
    for(unsigned i=0;i<count;i++){
        uint32_t value=flux_store[(loss_first+offset+i)%store_words];
        for(unsigned k=0;k<4;k++)out[4*i+k]=(uint8_t)(value>>(8*k));
    }
    return true;
}
static volatile bool streaming;         /* samples are read live, store stays untouched */
static volatile uint32_t last_index_ms;  /* HAL tick of capture start / last index pulse */

/* Streaming turns the store into a ring: logical sample n lives at n % store_words, and
 * a chunk may be reused once the streamer has read everything it held. */
static volatile uint32_t stream_consumed;
static volatile bool stream_scratch;    /* no free chunk: the DMA runs into dma_scratch */

/* Index-less capture (no index hole/sensor, flippy side, hard-sectored): revolution
 * boundaries every `period` ticks from the capture start instead of index pulses */
static uint32_t period;
static bool gw_capture, gw_linger;
static uint32_t gw_origin;
static uint64_t gw_origin64, gw_limit, gw_linger_ticks, gw_end;
static volatile uint32_t timer_high;
static uint16_t gw_indexes;
static uint32_t index_mask_ticks, index_previous;
static bool index_seen;

void ufi_flux_index_mask(uint16_t us)
{
    index_mask_ticks = (uint32_t)us * (FLUX_TIMER_FREQ / 1000000u);
    index_seen = false;
}

uint32_t ufi_flux_gw_origin(void) { return gw_origin; }
bool ufi_flux_gw_past_end(uint32_t t)
{
    const uint64_t now = ufi_flux_now64();
    const uint64_t at = now - (uint32_t)((uint32_t)now - t);
    const uint64_t elapsed = at - gw_origin64;
    return (gw_limit && elapsed >= gw_limit) ||
           (gw_linger && at >= gw_end);
}

uint32_t ufi_flux_gw_cursor(void)
{
    uint64_t now=ufi_flux_now64();
    if(gw_limit && now>gw_origin64+gw_limit)now=gw_origin64+gw_limit;
    if(gw_linger && now>gw_end)now=gw_end;
    return (uint32_t)now;
}

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
static void dma_freeze(void);
static void capture_halt(void);
static uint32_t dma_pos(void);

/* TIMx_SR is write-zero-to-clear; never use a read/modify/write to acknowledge it. */
static void capture_clear_status(uint32_t bits){
    TIM2->SR=~bits;
}
static void capture_rdata_arm(void){
    TIM2->CCER&=~(TIM_CCER_CC1E|TIM_CCER_CC3E);
    capture_clear_status(TIM_SR_CC1IF|TIM_SR_CC1OF|TIM_SR_CC3IF|TIM_SR_CC3OF);
    TIM2->DIER|=rd_de;
    TIM2->CCER|=rd_de==TIM_DIER_CC3DE?TIM_CCER_CC3E:TIM_CCER_CC1E;
}
static uint8_t capture_loss_status(uint32_t status,bool running){
    uint8_t loss=0;
    if(running && (status&(rd_de==TIM_DIER_CC3DE?TIM_SR_CC3OF:TIM_SR_CC1OF)))loss|=1;
    if(!index_internal && (gw_capture || !period || (evidence_flags()&EVIDENCE_RAW_INDEX)) &&
       (status&TIM_SR_CC2OF))loss|=2;
    return loss;
}
static void capture_latch_loss(uint8_t loss){
    uint8_t fresh=loss&(uint8_t)~capture_loss_flags;
    if(fresh&1)ufi_metric_add(&ufi_metrics.capture_rdata_loss,1);
    if(fresh&2)ufi_metric_add(&ufi_metrics.capture_index_loss,1);
    capture_loss_flags|=loss;
}
static bool capture_check_loss(void){
    if(g_capture.state!=CAPTURE_RUNNING && g_capture.state!=CAPTURE_WAITING_INDEX)return false;
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    bool active=g_capture.state==CAPTURE_RUNNING || g_capture.state==CAPTURE_WAITING_INDEX;
    /* READ info (4.24): position and stamp before the status read, so a clear flag covers both */
    const uint32_t pos=g_capture.state==CAPTURE_RUNNING?dma_pos():0u,now=TIM2->CNT;
    bool lost=active && capture_loss_status(TIM2->SR,g_capture.state==CAPTURE_RUNNING);
    if(lost){
        capture_halt();g_capture.error_code=2;g_capture.state=CAPTURE_ERROR;
    }else if(active){
        loss_clean_time=now;loss_clean_pos=pos;
    }
    __set_PRIMASK(mask);return lost;
}

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

static void configure_store(uint32_t available)
{
    chunk_words = (available / 3u < CHUNK_MAX_WORDS) ? available / 3u : CHUNK_MAX_WORDS;
    chunk_words &= ~7u;  /* cache-line aligned chunk boundaries */
    n_chunks = available / chunk_words - 1u;
    store_words = n_chunks * chunk_words;
    dma_scratch = &flux_store[store_words];
}

/* Every selection starts from known capacities, including a failed re-init. */
static void select_store(bool psram_ready)
{
    ufi_flux_invalidate();
    store_psram = psram_ready;
    reference_reserved = false;
    flux_store = psram_ready ? (uint32_t*)OCTOSPI1_BASE : flux_fallback;
    configure_store(psram_ready ? PSRAM_SIZE_BYTES / sizeof(uint32_t)
                               : FLUX_STORE_FALLBACK_WORDS);
}

int ufi_flux_reference_reserve(bool enable){
    if(enable && !store_psram)return UFI_ERR_NOT_IMPL;
    if(enable==reference_reserved)return UFI_OK;
    ufi_flux_invalidate();reference_reserved=enable;
    configure_store(store_psram?PSRAM_SIZE_BYTES/(enable?8u:4u):FLUX_STORE_FALLBACK_WORDS);
    return UFI_OK;
}
uint32_t* ufi_flux_reference(uint32_t* words){
    *words=reference_reserved && store_psram?PSRAM_SIZE_BYTES/8u:0;
    return *words?flux_store+PSRAM_SIZE_BYTES/8u:NULL;
}
void ufi_flux_init(void)
{
    __HAL_RCC_D2SRAM1_CLK_ENABLE(); /* CPU-only capture evidence */
#if defined(UFI_FORCE_INTERNAL_FLUX_STORE)
    select_store(false);
#else
    select_store(ufi_psram_init() == UFI_OK);
#endif

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
    TIM2->SR = ~TIM_SR_UIF;
    timer_high = 0;
    TIM2->DIER |= TIM_DIER_UIE;

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

/* The update flag is accounted under PRIMASK, including a pending overflow ISR. */
uint64_t ufi_flux_now64(void)
{
    const uint32_t mask=__get_PRIMASK();
    __disable_irq();
    uint32_t low=TIM2->CNT;
    if (TIM2->SR & TIM_SR_UIF) {
        timer_high++;
        TIM2->SR=~TIM_SR_UIF;
        low=TIM2->CNT;
    }
    const uint64_t result=((uint64_t)timer_high<<32)|low;
    __set_PRIMASK(mask);
    return result;
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

static void dma_freeze(void)
{
    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA1_Stream0->CR & DMA_SxCR_EN) {
    }
}

static void dma_stop(void)
{
    dma_freeze();
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
    ufi_flux_invalidate();
    capture_media_token=ufi_drive_media_token();
    gw_capture = false;
    stream_consumed = 0;
    ufi_metrics.capture_peak_words=ufi_metrics.capture_pending_words=0;
    stream_scratch = false;
    capture_loss_flags=0;

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
    TIM2->CCER&=~(TIM_CCER_CC1E|TIM_CCER_CC3E);
    capture_clear_status(TIM_SR_CC1IF|TIM_SR_CC1OF|TIM_SR_CC2IF|TIM_SR_CC2OF|TIM_SR_CC3IF|TIM_SR_CC3OF);
    dma_arm();
    loss_clean_time=TIM2->CNT;loss_clean_pos=0;loss_seen_time=loss_clean_time;   /* flags cleared: intact */

    if (period) {                           /* index-less: revolution 0 starts now */
        const uint32_t mask=__get_PRIMASK();
        __disable_irq();
        idx_time[0] = TIM2->CNT;
        idx_pos[0] = 0;
        idx_count = 1;
        capture_rdata_arm();
        g_capture.state = CAPTURE_RUNNING;
        if(evidence_flags()&EVIDENCE_RAW_INDEX){capture_clear_status(TIM_SR_CC2IF|TIM_SR_CC2OF);TIM2->DIER|=TIM_DIER_CC2IE;}
        __set_PRIMASK(mask);
        ufi_sched_arm(idx_time[0]);
        return UFI_OK;
    }

    g_capture.state = CAPTURE_WAITING_INDEX;
    capture_clear_status(TIM_SR_CC2IF | TIM_SR_CC2OF);
    TIM2->DIER |= TIM_DIER_CC2IE;
    return UFI_OK;
}

static void capture_halt(void)
{
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    bool active=g_capture.state==CAPTURE_RUNNING || g_capture.state==CAPTURE_WAITING_INDEX;
    /* Stop the hardware latch before inspecting flags: post-stop edges cannot
     * manufacture an RDATA overcapture while DMA requests are disabled. */
    TIM2->CCER&=~(rd_de==TIM_DIER_CC3DE?TIM_CCER_CC3E:TIM_CCER_CC1E);
    TIM2->DIER &= ~(rd_de | TIM_DIER_CC2IE);
    if(active)capture_latch_loss(capture_loss_status(TIM2->SR,g_capture.state==CAPTURE_RUNNING));
    dma_freeze(); /* drain pending writes before taking the final ring cursor */
    end_pos = dma_pos();
    bool scratch=(DMA1_Stream0->CR&DMA_SxCR_CT)?DMA1_Stream0->M1AR==(uint32_t)dma_scratch:
                                                          DMA1_Stream0->M0AR==(uint32_t)dma_scratch;
    dma_stop();
    if(active && capture_loss_flags){
        loss_seen_time=TIM2->CNT;           /* READ info (4.24): detection stamp */
        g_capture.error_code=2;g_capture.state=CAPTURE_ERROR;
        uint32_t valid=scratch?(end_pos/chunk_words)*chunk_words:end_pos;
        loss_count=valid>store_words?store_words:valid;
        loss_first=valid-loss_count;loss_hashed=false;loss_ready=loss_count!=0;
        loss_position[0]=g_capture.current_track;loss_position[1]=g_capture.current_side;
        loss_position[2]=g_capture.current_quarter;loss_position[3]=capture_loss_flags;
    }
    __set_PRIMASK(mask);
}

int ufi_flux_capture_gw(uint32_t ticks, uint16_t indexes, uint32_t linger)
{
    /* Keep timestamps within one timer wrap; report unsupported limits explicitly. */
    if (indexes > REVOLUTIONS_STREAM)
        return UFI_ERR_BAD_ARGS;
    ufi_evidence_finish();
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const int ret = ufi_flux_capture_start(REVOLUTIONS_STREAM, 0);
    if (ret == UFI_OK) {
        gw_capture = true;
        gw_linger = false;
        gw_origin64 = ufi_flux_now64();
        gw_origin = (uint32_t)gw_origin64;
        gw_limit = (uint64_t)ticks * 4u;
        gw_indexes = indexes;
        gw_linger_ticks = (uint64_t)linger * 4u;
        idx_count = 0;                       /* actual pulses only, no synthetic index */
        g_capture.state = CAPTURE_RUNNING;
        capture_rdata_arm();
    }
    __set_PRIMASK(mask);
    return ret;
}

int ufi_flux_capture_stop(void)
{
    /* Snapshot while the DMA cursor and capture state still describe this run. */
    ufi_evidence_finish();
    capture_halt();
    g_capture.state = CAPTURE_IDLE;
    return UFI_OK;
}

/* ============================================================================
 * INTERRUPTS
 * ============================================================================ */


/* Hard-sectored disks: n sector holes plus one index hole halfway between two of them.
 * Only the index hole counts as index; it is the pulse that follows its predecessor after
 * less than 3/4 of the previous gap (the sector hole after it has the same short gap, but
 * then the previous gap was short too). */
static uint8_t hard_sectors;
static uint32_t hs_last, hs_gap;        /* last hole time, gap before it */
static uint8_t hs_n;                    /* holes seen since the setting (0..2) */
void ufi_flux_evidence_context(uint8_t out[6]) {
    memcpy(out,&index_mask_ticks,4);out[4]=hard_sectors;out[5]=index_internal;
}
static void record_index(uint32_t t,uint32_t reason) {
    if(!(evidence_flags()&EVIDENCE_RAW_INDEX) || gw_capture ||
       (g_capture.state!=CAPTURE_WAITING_INDEX && g_capture.state!=CAPTURE_RUNNING))return;
    uint64_t now=ufi_flux_now64();
    evidence_index(now-(uint32_t)((uint32_t)now-t),g_capture.state==CAPTURE_RUNNING?dma_pos():0,
                   reason|((index_internal && !(reason&16u))?8u:0u));
}

void ufi_flux_hard_sectors(uint8_t n)
{
    hard_sectors = n;
    hs_n = 0;
}

static bool hs_is_index(uint32_t t)
{
    const uint32_t gap = t - hs_last, prev = hs_gap;
    hs_last = t;
    hs_gap = gap;
    if (hs_n < 2u) {
        hs_n++;
        return false;
    }
    return gap < prev - prev / 4u;
}

static bool index_sync;                     /* A10c (v1.0): SYNC capture on TIM8 replaces the INDEX line */

void ufi_flux_index_source(bool internal)
{
    index_internal = internal;
}

/* SYNC as the index source: the TIM2 CC2 capture channel is switched off (CCER CC2E, set only
 * once at flux init; capture and write arm nothing but CC2IE), so the INDEX line produces no
 * events at all and the TIM2 ISR needs no further condition; the TIM8 capture ISR reports the
 * sensor pulse as the index event instead (ufi_sync.c) */
void ufi_flux_index_source_sync(bool on)
{
    index_sync = on;
    if (on) {
        TIM2->CCER &= ~TIM_CCER_CC2E;
    } else {
        TIM2->CCER |= TIM_CCER_CC2E;
    }
    TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF);  /* nothing stale from before the switch */
}

bool ufi_flux_index_asserted(void)
{
    return index_internal ? ufi_index_sim_pulse() : index_sync ? ufi_sync_asserted() : bus_in(&PIN_FDD_INDEX);
}

void ufi_flux_tim2_irq(void)
{
    if (TIM2->SR & TIM_SR_UIF) (void)ufi_flux_now64();
    if(capture_check_loss())return;
    if ((TIM2->DIER & TIM_DIER_CC4IE) && (TIM2->SR & TIM_SR_CC4IF)) {
        /* compare channel 4: delayed write start at index + start_offset (ufi_write.c) */
        TIM2->SR = ~TIM_SR_CC4IF;
        TIM2->DIER &= ~TIM_DIER_CC4IE;
        ufi_write_delayed_start(TIM2->CNT);
    }
    if (!(TIM2->SR & TIM_SR_CC2IF)) {
        return;
    }
    const uint32_t t = TIM2->CCR2;          /* reading CCR2 clears CC2IF */
    capture_clear_status(TIM_SR_CC2OF);
    if (index_internal) {
        record_index(t,16);
        return;                             /* the drive's INDEX line is ignored */
    }
    ufi_flux_index_event(t);
}

/* One index pulse at flux-timer time t: from the TIM2 capture or from the internal index
 * simulation (TIM7 ISR at the same preemption level, so dma_pos() stays consistent) */
void ufi_flux_index_event(uint32_t t)
{
    if (index_mask_ticks && index_seen && (uint32_t)(t - index_previous) < index_mask_ticks) {record_index(t,2);return;}
    index_seen = true;
    index_previous = t;
    if (hard_sectors && !hs_is_index(t)) {
        record_index(t,4);
        return;                             /* sector hole */
    }
    record_index(t,1);
    write_state_t ws = ufi_write_get_state();
    if (ws == WRITE_WAITING_INDEX || ws == WRITE_ACTIVE) {
        ufi_write_index_handler(t);
        return;
    }

    if (gw_capture && g_capture.state == CAPTURE_RUNNING) {
        const uint64_t now=ufi_flux_now64();
        const uint64_t at=now-(uint32_t)((uint32_t)now-t);
        if ((gw_limit && at-gw_origin64>=gw_limit) || (gw_linger && at>=gw_end)) {
            capture_halt();if(!g_capture.error_code)g_capture.state=CAPTURE_COMPLETE;return;
        }
        if (idx_count >= REVOLUTIONS_STREAM) {
            capture_halt();
            g_capture.error_code = 1;
            g_capture.state = CAPTURE_ERROR;
            return;
        }
        idx_time[idx_count] = t;
        idx_pos[idx_count++] = dma_pos();
        last_index_ms = HAL_GetTick();
        if (!gw_linger && gw_indexes && idx_count >= gw_indexes) {
            gw_end = at + gw_linger_ticks;
            gw_linger = true;
        }
        return;
    }

    if (g_capture.state == CAPTURE_WAITING_INDEX) {
        idx_time[0] = t;
        idx_pos[0] = 0;
        idx_count = 1;
        last_index_ms = HAL_GetTick();
        capture_rdata_arm();       /* start streaming RDATA timestamps */
        g_capture.state = CAPTURE_RUNNING;
        ufi_sched_arm(t);
    } else if (g_capture.state == CAPTURE_RUNNING && !period) {
        idx_time[idx_count] = t;
        idx_pos[idx_count] = dma_pos();
        last_index_ms = HAL_GetTick();
        idx_count++;
        if (idx_count > g_capture.revolutions_requested) {
            capture_halt();
            if(!g_capture.error_code)g_capture.state = CAPTURE_COMPLETE;
        }
    } else if(g_capture.state!=CAPTURE_RUNNING || !(evidence_flags()&EVIDENCE_RAW_INDEX)) {
        TIM2->DIER &= ~TIM_DIER_CC2IE;      /* stray index while idle */
    }
}

void ufi_flux_dma_irq(void)
{
    if(capture_check_loss())return;
    /* A continuous stream must fail explicitly before its 32-bit sample cursor
     * wraps. A pending TC and one active buffer are included in this bound. */
    if(g_capture.state==CAPTURE_RUNNING && chunks_done>=UINT32_MAX/chunk_words-1u){
        capture_halt();ufi_metric_add(&ufi_metrics.capture_overruns,1);
        g_capture.error_code=1;g_capture.state=CAPTURE_ERROR;return;
    }
    const uint32_t isr = DMA1->LISR;

    if (isr & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0)) {
        ufi_metrics.capture_dma_errors++;
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
            ufi_metrics.capture_overruns++;
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

static void gw_poll_deadline(void)
{
    const uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (gw_capture && g_capture.state == CAPTURE_RUNNING) {
        const uint64_t now = ufi_flux_now64();
        const bool timed = gw_limit && now - gw_origin64 >= gw_limit;
        const bool lingered = gw_linger && now >= gw_end;
        if (timed || lingered) {
            if (timed && (!gw_linger || gw_end - gw_origin64 > gw_limit))
                gw_end = gw_origin64 + gw_limit;
            capture_halt();
            /* Poll latency must not add post-deadline index markers to the result. */
            if(!g_capture.error_code)g_capture.state = CAPTURE_COMPLETE;
        }
    }
    __set_PRIMASK(irq);
}

capture_state_t ufi_flux_poll(void)
{
    (void)capture_check_loss();
    if(!gw_capture && capture_media_token &&
       (g_capture.state==CAPTURE_WAITING_INDEX || g_capture.state==CAPTURE_RUNNING) &&
       !ufi_drive_media_matches(capture_media_token)){
        capture_halt();g_capture.error_code=4;g_capture.state=CAPTURE_ERROR;
    }
    gw_poll_deadline();
    if (period && g_capture.state == CAPTURE_RUNNING) {
        /* index-less: record the boundaries that have passed, stop after the last one */
        const uint8_t seen = ufi_flux_index_count();
        while (idx_count < seen) {
            const uint32_t mask=__get_PRIMASK();
            __disable_irq();
            idx_time[idx_count] = idx_time[0] + (uint32_t)idx_count * period;
            idx_pos[idx_count] = dma_pos();
            idx_count++;
            if (idx_count > g_capture.revolutions_requested && g_capture.state == CAPTURE_RUNNING) {
                capture_halt();
                if(!g_capture.error_code)g_capture.state = CAPTURE_COMPLETE;
            }
            __set_PRIMASK(mask);
        }
    }
    if (!period && !(gw_capture && (gw_limit || !gw_indexes || gw_linger)) &&
        (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) &&
        HAL_GetTick() - last_index_ms > INDEX_TIMEOUT_MS) {
        capture_halt();
        g_capture.error_code = 3;           /* no index pulse: no disk / motor off */
        g_capture.state = CAPTURE_ERROR;
    }
    if (g_capture.state != CAPTURE_COMPLETE || finalised || streaming) {
        return g_capture.state;
    }

    const uint32_t used = end_pos;
    if(idx_count<2 || used>store_words){
        g_capture.revolutions_captured=0;
        g_capture.error_code=2;g_capture.state=CAPTURE_ERROR;
        return g_capture.state;
    }
    SCB_InvalidateDCache_by_Addr(flux_store, (int32_t)(((used * 4u) + 31u) & ~31u));

    uint8_t n = idx_count - 1;              /* full revolutions */
    if (n > g_capture.revolutions_requested) {
        n = g_capture.revolutions_requested;
    }
    if (n > REVOLUTIONS_BUFFER) {
        n = REVOLUTIONS_BUFFER;
        g_capture.error_code = 1;
    }

    uint32_t b[REVOLUTIONS_BUFFER + 1];
    b[0] = 0;
    for (uint8_t r = 1; r <= n; r++) {
        b[r] = align_boundary(idx_pos[r], idx_time[r], b[r - 1], used);
    }
    uint32_t fingerprint=2166136261u;
    for (uint8_t r = 0; r < n; r++) {
        for (uint32_t i = b[r]; i < b[r + 1]; i++) {
            flux_store[i] -= idx_time[r];   /* ticks since this revolution's index */
            fingerprint=(fingerprint^flux_store[i])*16777619u;
            if(!(i&1023u)){UFI_WATCHDOG_FEED();}
        }
        revs[r].samples = (flux_sample_t*)&flux_store[b[r]];
        revs[r].count = b[r + 1] - b[r];
        revs[r].index_time = idx_time[r + 1] - idx_time[r];
        revs[r].revolution = r;
        fingerprint=(fingerprint^revs[r].count)*16777619u;
        fingerprint=(fingerprint^revs[r].index_time)*16777619u;
    }

    g_capture.revolutions_captured = n;
    archive_info[0]=g_capture.current_track;archive_info[1]=g_capture.current_side;
    archive_info[2]=g_capture.current_quarter;archive_info[3]=period?1:0;
    uint32_t eid=evidence_id();for(unsigned i=0;i<4;i++)archive_info[4+i]=(uint8_t)(eid>>(8*i));
    for(unsigned i=0;i<8;i++)fingerprint=(fingerprint^archive_info[i])*16777619u;
    archive_fingerprint=(fingerprint^capture_media_token)*16777619u;
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
    const uint32_t mask=__get_PRIMASK();
    __disable_irq();                        /* chunks_done / NDTR / state must agree */
    const capture_state_t s = g_capture.state;
    if (s == CAPTURE_RUNNING) {
        pos = dma_pos();
    } else if (s == CAPTURE_COMPLETE || s == CAPTURE_ERROR) {
        pos = end_pos;
    }
    __set_PRIMASK(mask);
    uint32_t pending=pos>=stream_consumed?pos-stream_consumed:0;
    if(pending>store_words)pending=store_words;
    ufi_metrics.capture_pending_words=pending;
    if(pending>ufi_metrics.capture_peak_words)ufi_metrics.capture_peak_words=pending;
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

/* READ info (4.24, thread context): the loss window of the capture that just ended, 0 without
 * one.  Every loss ends the capture, so there is at most one window per capture.  The index
 * stamps let the packer convert flux-timer stamps into stream time. */
uint8_t ufi_flux_loss_window(ufi_loss_raw_t* out,const volatile uint32_t** times,uint8_t* count,uint32_t* period_ticks)
{
    *times=idx_time;*count=idx_count;*period_ticks=period;
    memset(out,0,sizeof(*out));
    const uint32_t code=g_capture.error_code;
    if(gw_capture || (code!=1u && code!=2u))return 0;
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    const uint8_t flags=capture_loss_flags;
    out->start=loss_clean_time;out->samples=loss_clean_pos;out->end=loss_seen_time;
    __set_PRIMASK(mask);
    if(code==2u){
        /* overcapture: lost between the last clean check and the halt; DMA error: no stamp */
        out->kind=flags?(uint8_t)(flags&3u):(uint8_t)(READ_LOSS_DMA|READ_LOSS_OPEN_END);
    }else{
        /* store full / host too slow: nothing after the last stored sample */
        out->kind=READ_LOSS_FULL|READ_LOSS_OPEN_END;
        out->samples=end_pos;
        if(end_pos && !finalised){          /* finalised samples are index-relative */
            const uint32_t at=(end_pos-1u)%store_words;
            SCB_InvalidateDCache_by_Addr(&flux_store[at&~7u],32);
            out->start=flux_store[at];
        }
    }
    if(!idx_count)out->kind|=READ_LOSS_BEFORE_INDEX;
    return 1;
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
