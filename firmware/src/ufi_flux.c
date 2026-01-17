/**
 * UFI Flux Engine - Flux Capture Module
 * 
 * Präzise Flux-Timing Erfassung mit DMA
 */

#include "ufi_firmware.h"
#include "stm32h7xx_hal.h"

/* ============================================================================
 * EXTERNE VARIABLEN (aus ufi_main.c)
 * ============================================================================ */

extern capture_context_t g_capture;
extern flux_revolution_t g_revolution_buffer[];

/* ============================================================================
 * TIMER & DMA HANDLES (Global für stm32h7xx_it.c)
 * ============================================================================ */

TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2;

// DMA Double Buffer für unterbrechungsfreies Capture
#define DMA_BUFFER_SIZE     16384
__attribute__((section(".dtcm"), aligned(32)))
static uint32_t dma_buffer_a[DMA_BUFFER_SIZE];
__attribute__((section(".dtcm"), aligned(32)))
static uint32_t dma_buffer_b[DMA_BUFFER_SIZE];

static volatile uint8_t active_buffer = 0;
static volatile uint32_t total_samples = 0;
static volatile uint32_t last_timestamp = 0;

/* ============================================================================
 * FLUX TIMER INITIALISIERUNG
 * ============================================================================ */

void ufi_flux_init(void) {
    // TIM2 Clock aktivieren
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    
    // TIM2: 32-bit Timer @ 275 MHz (SYSCLK/2)
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_IC_Init(&htim2);
    
    // Input Capture Konfiguration
    TIM_IC_InitTypeDef ic_config = {0};
    ic_config.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    ic_config.ICSelection = TIM_ICSELECTION_DIRECTTI;
    ic_config.ICPrescaler = TIM_ICPSC_DIV1;
    ic_config.ICFilter = 0;  // Kein Filter für maximale Geschwindigkeit
    HAL_TIM_IC_ConfigChannel(&htim2, &ic_config, TIM_CHANNEL_1);
    
    // DMA Konfiguration
    hdma_tim2.Instance = DMA1_Stream0;
    hdma_tim2.Init.Request = DMA_REQUEST_TIM2_CH1;
    hdma_tim2.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_tim2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim2.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_tim2.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_tim2.Init.Mode = DMA_CIRCULAR;
    hdma_tim2.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_tim2.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_tim2.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_tim2.Init.MemBurst = DMA_MBURST_INC4;
    hdma_tim2.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_tim2);
    
    // DMA Double-Buffer Mode aktivieren
    HAL_DMAEx_MultiBufferStart_IT(&hdma_tim2,
        (uint32_t)&TIM2->CCR1,
        (uint32_t)dma_buffer_a,
        (uint32_t)dma_buffer_b,
        DMA_BUFFER_SIZE);
    
    // Link DMA to Timer
    __HAL_LINKDMA(&htim2, hdma[TIM_DMA_ID_CC1], hdma_tim2);
    
    // NVIC Prioritäten (höchste für Timing!)
    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

/* ============================================================================
 * CAPTURE STARTEN
 * ============================================================================ */

int ufi_flux_capture_start(uint8_t revolutions) {
    if (g_capture.state != CAPTURE_IDLE) {
        return -1;
    }
    
    // Buffer zurücksetzen
    total_samples = 0;
    last_timestamp = 0;
    active_buffer = 0;
    
    g_capture.revolutions_requested = revolutions;
    g_capture.revolutions_captured = 0;
    g_capture.error_code = 0;
    
    // Timer zurücksetzen
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    
    // DMA starten
    HAL_DMA_Start_IT(&hdma_tim2, 
        (uint32_t)&TIM2->CCR1,
        (uint32_t)dma_buffer_a,
        DMA_BUFFER_SIZE);
    
    // Timer Input Capture mit DMA starten
    HAL_TIM_IC_Start_DMA(&htim2, TIM_CHANNEL_1, dma_buffer_a, DMA_BUFFER_SIZE);
    
    // Auf Index warten
    g_capture.state = CAPTURE_WAITING_INDEX;
    
    return 0;
}

/* ============================================================================
 * CAPTURE STOPPEN
 * ============================================================================ */

int ufi_flux_capture_stop(void) {
    HAL_TIM_IC_Stop_DMA(&htim2, TIM_CHANNEL_1);
    HAL_DMA_Abort(&hdma_tim2);
    
    g_capture.state = CAPTURE_IDLE;
    
    return 0;
}

/* ============================================================================
 * INDEX-PULS VERARBEITUNG
 * ============================================================================ */

void ufi_flux_index_pulse(uint32_t timestamp) {
    if (g_capture.state == CAPTURE_WAITING_INDEX) {
        // Erstes Index - Capture beginnt
        __HAL_TIM_SET_COUNTER(&htim2, 0);
        total_samples = 0;
        g_capture.state = CAPTURE_RUNNING;
    }
    else if (g_capture.state == CAPTURE_RUNNING) {
        // Revolution abgeschlossen
        flux_revolution_t* rev = &g_revolution_buffer[g_capture.revolutions_captured];
        
        // DMA Position ermitteln
        uint32_t dma_remaining = __HAL_DMA_GET_COUNTER(&hdma_tim2);
        uint32_t samples_in_buffer = DMA_BUFFER_SIZE - dma_remaining;
        
        // Daten kopieren
        uint32_t* src = (active_buffer == 0) ? dma_buffer_a : dma_buffer_b;
        rev->count = samples_in_buffer;
        rev->index_time = timestamp;
        rev->revolution = g_capture.revolutions_captured;
        
        // Samples kopieren (mit Überlauf-Schutz)
        uint32_t copy_count = (samples_in_buffer > MAX_FLUX_PER_REV) ? 
                              MAX_FLUX_PER_REV : samples_in_buffer;
        
        for (uint32_t i = 0; i < copy_count; i++) {
            rev->samples[i].timestamp = src[i];
        }
        
        g_capture.revolutions_captured++;
        
        // Mehr Revolutions nötig?
        if (g_capture.revolutions_captured >= g_capture.revolutions_requested) {
            g_capture.state = CAPTURE_COMPLETE;
            ufi_flux_capture_stop();
        } else {
            // Timer für nächste Revolution zurücksetzen
            __HAL_TIM_SET_COUNTER(&htim2, 0);
            total_samples = 0;
        }
    }
}

/* ============================================================================
 * DMA CALLBACKS
 * ============================================================================ */

void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *hdma) {
    if (hdma == &hdma_tim2) {
        // Buffer voll - zur anderen Hälfte wechseln
        active_buffer = 1 - active_buffer;
        total_samples += DMA_BUFFER_SIZE;
        
        // Überlauf-Prüfung
        if (total_samples > MAX_FLUX_PER_REV * 2) {
            g_capture.error_code = 1;  // Überlauf
            g_capture.state = CAPTURE_ERROR;
            ufi_flux_capture_stop();
        }
    }
}

void HAL_DMA_XferHalfCpltCallback(DMA_HandleTypeDef *hdma) {
    if (hdma == &hdma_tim2) {
        // Half-Transfer - kann für Streaming genutzt werden
    }
}

void HAL_DMA_ErrorCallback(DMA_HandleTypeDef *hdma) {
    if (hdma == &hdma_tim2) {
        g_capture.error_code = 2;  // DMA-Fehler
        g_capture.state = CAPTURE_ERROR;
        ufi_flux_capture_stop();
    }
}

/* ============================================================================
 * FLUX-DATEN AUSLESEN
 * ============================================================================ */

flux_revolution_t* ufi_flux_get_revolution(uint8_t index) {
    if (index >= g_capture.revolutions_captured) {
        return NULL;
    }
    return &g_revolution_buffer[index];
}

uint8_t ufi_flux_get_revolution_count(void) {
    return g_capture.revolutions_captured;
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

flux_stats_t ufi_flux_calculate_stats(flux_revolution_t* rev) {
    flux_stats_t stats = {0};
    
    if (!rev || rev->count < 2) {
        return stats;
    }
    
    stats.min_delta = 0xFFFFFFFF;
    stats.max_delta = 0;
    uint64_t sum = 0;
    uint32_t prev = 0;
    
    for (uint32_t i = 0; i < rev->count; i++) {
        uint32_t ts = rev->samples[i].timestamp;
        uint32_t delta = ts - prev;
        prev = ts;
        
        if (i > 0) {  // Erste Delta ignorieren
            if (delta < stats.min_delta) stats.min_delta = delta;
            if (delta > stats.max_delta) stats.max_delta = delta;
            sum += delta;
        }
    }
    
    stats.total_flux = rev->count;
    stats.duration_ticks = rev->index_time;
    stats.avg_delta = (rev->count > 1) ? sum / (rev->count - 1) : 0;
    
    // RPM berechnen (bei 275 MHz Timer)
    // 1 Umdrehung = index_time Ticks
    // RPM = 60 / (index_time * 3.636ns / 1e9)
    if (stats.duration_ticks > 0) {
        float duration_s = stats.duration_ticks * 3.636e-9f;
        stats.rpm = 60.0f / duration_s;
    }
    
    return stats;
}
