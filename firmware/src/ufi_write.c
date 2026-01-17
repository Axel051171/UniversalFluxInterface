/**
 * UFI Flux Engine - Write Support
 * 
 * Flux-basiertes Schreiben für Disk-Erstellung und Kopien
 */

#include "ufi_firmware.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/* ============================================================================
 * EXTERNE VARIABLEN
 * ============================================================================ */

extern TIM_HandleTypeDef htim2;
extern capture_context_t g_capture;

/* ============================================================================
 * WRITE KONFIGURATION
 * ============================================================================ */

#define WRITE_BUFFER_SIZE   65536   // Max 64K Flux-Samples pro Track
#define WRITE_PRECOMP_NS    140     // ns - Write Precompensation

/* Write State Machine */
typedef enum {
    WRITE_IDLE,
    WRITE_RECEIVING,        // Empfange Flux-Daten
    WRITE_WAITING_INDEX,    // Warte auf Index für Sync
    WRITE_ACTIVE,           // Schreibe Daten
    WRITE_COMPLETE,
    WRITE_VERIFYING,
    WRITE_ERROR
} write_state_t;

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

/* Write Buffer im AXI SRAM */
__attribute__((section(".axi_sram")))
static uint32_t write_buffer[WRITE_BUFFER_SIZE];

/* ============================================================================
 * GPIO PINS (Referenzen aus ufi_main.c)
 * ============================================================================ */

extern const gpio_pin_t PIN_FDD_WGATE;
extern const gpio_pin_t PIN_FDD_WDATA;
extern const gpio_pin_t PIN_FDD_INDEX;
extern const gpio_pin_t PIN_LED_FDD;

/* ============================================================================
 * WRITE INITIALISIERUNG
 * ============================================================================ */

void ufi_write_init(void) {
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
    if (g_write.state != WRITE_IDLE) {
        return UFI_ERR_BUSY;
    }
    if (flux_count > WRITE_BUFFER_SIZE) {
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
    
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_SET);
    
    return UFI_OK;
}

void ufi_write_index_handler(void) {
    if (g_write.state == WRITE_WAITING_INDEX) {
        g_write.state = WRITE_ACTIVE;
        g_write.flux_index = 0;
        g_write.next_flux_time = 0;
        
        HAL_GPIO_WritePin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin, GPIO_PIN_RESET);
        __HAL_TIM_SET_COUNTER(&htim2, 0);
        
        if (g_write.flux_count > 0) {
            g_write.next_flux_time = write_buffer[0];
        }
    }
    else if (g_write.state == WRITE_ACTIVE) {
        HAL_GPIO_WritePin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin, GPIO_PIN_SET);
        g_write.state = WRITE_COMPLETE;
        HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
    }
}

void ufi_write_process(void) {
    if (g_write.state != WRITE_ACTIVE || g_write.flux_index >= g_write.flux_count) {
        return;
    }
    
    uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);
    
    if (now >= g_write.next_flux_time) {
        HAL_GPIO_WritePin(PIN_FDD_WDATA.port, PIN_FDD_WDATA.pin, GPIO_PIN_RESET);
        for (volatile int i = 0; i < 140; i++) { __NOP(); }
        HAL_GPIO_WritePin(PIN_FDD_WDATA.port, PIN_FDD_WDATA.pin, GPIO_PIN_SET);
        
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
    
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_SET);
    
    uint32_t timeout = HAL_GetTick() + 500;
    
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_RESET) {
        if (HAL_GetTick() > timeout) {
            HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
            return UFI_ERR_NO_INDEX;
        }
    }
    while (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_SET) {
        if (HAL_GetTick() > timeout) {
            HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
            return UFI_ERR_NO_INDEX;
        }
    }
    
    HAL_GPIO_WritePin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin, GPIO_PIN_RESET);
    HAL_Delay(220);
    HAL_GPIO_WritePin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin, GPIO_PIN_SET);
    
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
    
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
    
    uint32_t timeout = HAL_GetTick() + 1000;
    while (ufi_capture_get_state() != CAPTURE_COMPLETE) {
        if (HAL_GetTick() > timeout) {
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
    HAL_GPIO_WritePin(PIN_FDD_WGATE.port, PIN_FDD_WGATE.pin, GPIO_PIN_SET);
    g_write.state = WRITE_IDLE;
    g_write.flux_count = 0;
    g_write.bytes_received = 0;
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
}

void ufi_write_set_precomp(bool enable) {
    g_write.use_precomp = enable;
}
