/**
 * UFI Flux Engine - Write Support
 * 
 * Flux-basiertes Schreiben für Disk-Erstellung und Kopien
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

#define WRITE_PRECOMP_NS    140     // ns - Write Precompensation

typedef struct {
    write_state_t state;
    uint8_t track;
    uint8_t side;
    uint32_t flux_count;        // Anzahl Flux-Übergänge
    uint32_t flux_index;        // Aktueller Index beim Schreiben
    uint32_t bytes_received;    // Empfangene Bytes
    uint32_t bytes_expected;    // Erwartete Bytes
    bool verify_after;          // Nach Schreiben verifizieren?
    bool use_precomp;           // Write Precompensation?
    uint32_t next_flux_time;    // Nächster Flux-Zeitpunkt
} write_context_t;

static write_context_t g_write;

/* Write data lives in the shared flux store (ufi_flux.c); capture and write never
 * run at the same time.  A verify capture overwrites it (only counts are compared). */
static uint32_t* write_buffer;
static uint32_t write_buffer_words;

/* ============================================================================
 * WRITE INITIALISIERUNG
 * ============================================================================ */

void ufi_write_init(void) {
    write_buffer = ufi_flux_store(&write_buffer_words);
    g_write.state = WRITE_IDLE;
    g_write.flux_count = 0;
    g_write.flux_index = 0;
    g_write.bytes_received = 0;
    g_write.bytes_expected = 0;
    g_write.verify_after = false;
    g_write.use_precomp = true;
}

/* ============================================================================
 * WRITE PRECOMPENSATION
 * ============================================================================ */

static uint32_t apply_precomp(uint32_t timing, uint32_t prev_timing, uint32_t next_timing, uint8_t track) {
    if (!g_write.use_precomp || track < 40) {
        return timing;
    }
    
    uint32_t precomp_ns = (track > 60) ? WRITE_PRECOMP_NS : (WRITE_PRECOMP_NS / 2);
    uint32_t precomp_ticks = (precomp_ns * FLUX_TIMER_FREQ) / 1000000000UL;
    
    if (prev_timing > timing * 2) {
        return (timing > precomp_ticks) ? timing - precomp_ticks : timing;
    }
    if (next_timing > timing * 2) {
        return timing + precomp_ticks;
    }
    return timing;
}

/* ============================================================================
 * FLUX-DATEN EMPFANGEN
 * ============================================================================ */

int ufi_write_prepare(uint8_t track, uint8_t side, uint32_t flux_count, bool verify) {
    if (g_write.state != WRITE_IDLE ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (flux_count > write_buffer_words) {
        return UFI_ERR_BUFFER_FULL;
    }
    
    g_write.track = track;
    g_write.side = side;
    g_write.flux_count = flux_count;
    g_write.bytes_expected = flux_count * sizeof(uint32_t);
    g_write.bytes_received = 0;
    g_write.verify_after = verify;
    g_write.state = WRITE_RECEIVING;
    
    return UFI_OK;
}

int ufi_write_receive_chunk(uint8_t* data, uint32_t len) {
    if (g_write.state != WRITE_RECEIVING) {
        return UFI_ERR_BUSY;
    }
    if (g_write.bytes_received + len > g_write.bytes_expected) {
        return UFI_ERR_BUFFER_FULL;
    }
    
    memcpy(((uint8_t*)write_buffer) + g_write.bytes_received, data, len);
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
    
    if (ufi_drive_seek(g_write.track) != 0) {
        g_write.state = WRITE_ERROR;
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(g_write.side);
    HAL_Delay(20);
    
    g_write.flux_index = 0;
    g_write.next_flux_time = 0;
    g_write.state = WRITE_WAITING_INDEX;
    
    led_set(&PIN_LED_FDD, true);

    return UFI_OK;
}

/* Called from the TIM2 index-capture interrupt; t = TIM2 timestamp of the index pulse */
void ufi_write_index_handler(uint32_t t) {
    if (g_write.state == WRITE_WAITING_INDEX) {
        g_write.state = WRITE_ACTIVE;
        g_write.flux_index = 0;

        bus_out(&PIN_FDD_WGATE, true);

        g_write.next_flux_time = t + ((g_write.flux_count > 0) ? write_buffer[0] : 0);
    }
    else if (g_write.state == WRITE_ACTIVE) {
        bus_out(&PIN_FDD_WGATE, false);
        g_write.state = WRITE_COMPLETE;
        led_set(&PIN_LED_FDD, false);
    }
}

void ufi_write_process(void) {
    if (g_write.state != WRITE_ACTIVE || g_write.flux_index >= g_write.flux_count) {
        return;
    }

    uint32_t now = ufi_flux_now();

    if ((int32_t)(now - g_write.next_flux_time) >= 0) {
        /* short write pulse (bus asserted = low); bit-banged until TIM3_CH1 takes over */
        bus_out(&PIN_FDD_WDATA, true);
        for (volatile int i = 0; i < 140; i++) { __NOP(); }
        bus_out(&PIN_FDD_WDATA, false);
        
        g_write.flux_index++;
        
        if (g_write.flux_index < g_write.flux_count) {
            uint32_t delta = write_buffer[g_write.flux_index];
            
            if (g_write.use_precomp && g_write.flux_index > 0 && 
                g_write.flux_index < g_write.flux_count - 1) {
                delta = apply_precomp(delta,
                    write_buffer[g_write.flux_index - 1],
                    write_buffer[g_write.flux_index + 1],
                    g_write.track);
            }
            g_write.next_flux_time += delta;
        }
    }
}

/* ============================================================================
 * TRACK LÖSCHEN
 * ============================================================================ */

int ufi_erase_track(uint8_t track, uint8_t side) {
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

    bus_out(&PIN_FDD_WGATE, true);
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
    
    int ret = ufi_capture_start(g_write.track, g_write.side, 1);
    if (ret != 0) {
        g_write.state = WRITE_ERROR;
        return ret;
    }
    
    const uint32_t start = HAL_GetTick();
    while (ufi_capture_get_state() != CAPTURE_COMPLETE) {
        if (HAL_GetTick() - start > 1000 || g_capture.state == CAPTURE_ERROR) {
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
    if (g_write.state == WRITE_RECEIVING) {
        return (g_write.bytes_received * 100) / g_write.bytes_expected;
    }
    else if (g_write.state == WRITE_ACTIVE) {
        return (g_write.flux_index * 100) / g_write.flux_count;
    }
    return 0;
}

void ufi_write_abort(void) {
    bus_out(&PIN_FDD_WGATE, false);
    g_write.state = WRITE_IDLE;
    g_write.flux_count = 0;
    g_write.bytes_received = 0;
    led_set(&PIN_LED_FDD, false);
}

void ufi_write_set_precomp(bool enable) {
    g_write.use_precomp = enable;
}
