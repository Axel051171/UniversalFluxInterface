/**
 * UFI Flux Engine - Drive Control Module
 * 
 * Laufwerk-Steuerung für alle unterstützten Interfaces
 */

#include "ufi_firmware.h"
#include "stm32h7xx_hal.h"

/* ============================================================================
 * GPIO DEFINITIONEN
 * ============================================================================ */

// Port A - Drive Select & Motor
#define FDD_MOTOR_A_PIN     GPIO_PIN_0
#define FDD_MOTOR_B_PIN     GPIO_PIN_1
#define FDD_SEL_A_PIN       GPIO_PIN_2
#define FDD_SEL_B_PIN       GPIO_PIN_3
#define FDD_PORT_A          GPIOA

// Port B - Control Signals
#define FDD_STEP_PIN        GPIO_PIN_0
#define FDD_DIR_PIN         GPIO_PIN_1
#define FDD_SIDE_PIN        GPIO_PIN_2
#define FDD_WGATE_PIN       GPIO_PIN_3
#define FDD_WDATA_PIN       GPIO_PIN_4
#define FDD_PORT_B          GPIOB

// Port C - Status Signals (active active active Input)
#define FDD_INDEX_PIN       GPIO_PIN_0
#define FDD_TRACK0_PIN      GPIO_PIN_1
#define FDD_WPROT_PIN       GPIO_PIN_2
#define FDD_RDATA_PIN       GPIO_PIN_3
#define FDD_DKCHG_PIN       GPIO_PIN_4
#define FDD_READY_PIN       GPIO_PIN_5
#define FDD_PORT_C          GPIOC

// Apple Disk II spezifische Pins (Port E)
#define APPLE_ENABLE_PIN    GPIO_PIN_0
#define APPLE_PH0_PIN       GPIO_PIN_1
#define APPLE_PH1_PIN       GPIO_PIN_2
#define APPLE_PH2_PIN       GPIO_PIN_3
#define APPLE_PH3_PIN       GPIO_PIN_4
#define APPLE_WRREQ_PIN     GPIO_PIN_5
#define APPLE_PORT          GPIOE

/* ============================================================================
 * GLOBALE VARIABLEN
 * ============================================================================ */

static drive_type_t g_current_drive = DRIVE_NONE;
static drive_status_t g_drive_status[6];  // Index 0 nicht verwendet

// Timing-Konstanten (µs)
#define STEP_PULSE_US       3
#define STEP_RATE_US        3000
#define SETTLE_TIME_US      15000
#define MOTOR_SPINUP_MS     500
#define DIR_SETUP_US        1

/* ============================================================================
 * DELAY FUNKTIONEN
 * ============================================================================ */

static void delay_us(uint32_t us) {
    // DWT Cycle Counter für präzise µs-Verzögerung
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

/* ============================================================================
 * GPIO INITIALISIERUNG
 * ============================================================================ */

void ufi_drive_init(void) {
    GPIO_InitTypeDef gpio = {0};
    
    // Clocks aktivieren
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    
    // DWT für delay_us aktivieren
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    // ========================================
    // Port A - Outputs (Motor, Select)
    // ========================================
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = FDD_MOTOR_A_PIN | FDD_MOTOR_B_PIN | FDD_SEL_A_PIN | FDD_SEL_B_PIN;
    HAL_GPIO_Init(FDD_PORT_A, &gpio);
    
    // Alle inactive (high für active-low Signale)
    HAL_GPIO_WritePin(FDD_PORT_A, gpio.Pin, GPIO_PIN_SET);
    
    // ========================================
    // Port B - Outputs (Step, Dir, Side, Write)
    // ========================================
    gpio.Pin = FDD_STEP_PIN | FDD_DIR_PIN | FDD_SIDE_PIN | FDD_WGATE_PIN | FDD_WDATA_PIN;
    HAL_GPIO_Init(FDD_PORT_B, &gpio);
    HAL_GPIO_WritePin(FDD_PORT_B, gpio.Pin, GPIO_PIN_SET);
    
    // ========================================
    // Port C - Inputs (Index, Track0, etc.)
    // ========================================
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = FDD_TRACK0_PIN | FDD_WPROT_PIN | FDD_DKCHG_PIN | FDD_READY_PIN;
    HAL_GPIO_Init(FDD_PORT_C, &gpio);
    
    // Index als Interrupt (fallende Flanke)
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pin = FDD_INDEX_PIN;
    HAL_GPIO_Init(FDD_PORT_C, &gpio);
    
    // RDATA als Alternate Function für Timer Capture
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF1_TIM2;
    gpio.Pin = FDD_RDATA_PIN;
    HAL_GPIO_Init(FDD_PORT_C, &gpio);
    
    // ========================================
    // Port E - Apple Disk II
    // ========================================
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = APPLE_ENABLE_PIN | APPLE_PH0_PIN | APPLE_PH1_PIN | 
               APPLE_PH2_PIN | APPLE_PH3_PIN | APPLE_WRREQ_PIN;
    HAL_GPIO_Init(APPLE_PORT, &gpio);
    HAL_GPIO_WritePin(APPLE_PORT, gpio.Pin, GPIO_PIN_RESET);
    
    // ========================================
    // Index Interrupt (EXTI)
    // ========================================
    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    
    // Status initialisieren
    for (int i = 0; i < 6; i++) {
        g_drive_status[i].type = DRIVE_NONE;
        g_drive_status[i].current_track = 0;
        g_drive_status[i].current_side = 0;
        g_drive_status[i].motor_on = false;
    }
}

/* ============================================================================
 * LAUFWERK AUSWÄHLEN
 * ============================================================================ */

int ufi_drive_select(drive_type_t type) {
    // Alle deselektieren
    HAL_GPIO_WritePin(FDD_PORT_A, FDD_SEL_A_PIN | FDD_SEL_B_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN, GPIO_PIN_RESET);
    
    switch (type) {
        case DRIVE_SHUGART_A:
            HAL_GPIO_WritePin(FDD_PORT_A, FDD_SEL_A_PIN, GPIO_PIN_RESET);
            break;
            
        case DRIVE_SHUGART_B:
            HAL_GPIO_WritePin(FDD_PORT_A, FDD_SEL_B_PIN, GPIO_PIN_RESET);
            break;
            
        case DRIVE_APPLE_II:
            HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN, GPIO_PIN_SET);
            break;
            
        case DRIVE_AMIGA:
            // Amiga verwendet gleiche Signale wie Shugart A
            HAL_GPIO_WritePin(FDD_PORT_A, FDD_SEL_A_PIN, GPIO_PIN_RESET);
            break;
            
        case DRIVE_IEC:
            // IEC braucht kein Select
            break;
            
        case DRIVE_NONE:
        default:
            g_current_drive = DRIVE_NONE;
            return 0;
    }
    
    g_current_drive = type;
    g_drive_status[type].type = type;
    
    delay_us(10000);  // 10ms Settle
    
    return 0;
}

/* ============================================================================
 * MOTOR STEUERUNG
 * ============================================================================ */

int ufi_drive_motor(bool on) {
    if (g_current_drive == DRIVE_NONE) {
        return -1;
    }
    
    switch (g_current_drive) {
        case DRIVE_SHUGART_A:
        case DRIVE_AMIGA:
            HAL_GPIO_WritePin(FDD_PORT_A, FDD_MOTOR_A_PIN, 
                             on ? GPIO_PIN_RESET : GPIO_PIN_SET);
            break;
            
        case DRIVE_SHUGART_B:
            HAL_GPIO_WritePin(FDD_PORT_A, FDD_MOTOR_B_PIN,
                             on ? GPIO_PIN_RESET : GPIO_PIN_SET);
            break;
            
        case DRIVE_APPLE_II:
            // Apple: Motor durch Enable gesteuert
            HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN,
                             on ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;
            
        case DRIVE_IEC:
            // 1541: Motor-Steuerung über IEC-Protokoll
            // (wird in ufi_iec.c behandelt)
            break;
            
        default:
            return -1;
    }
    
    g_drive_status[g_current_drive].motor_on = on;
    
    if (on) {
        HAL_Delay(MOTOR_SPINUP_MS);  // Spin-up Zeit
    }
    
    return 0;
}

/* ============================================================================
 * STEP FUNKTIONEN
 * ============================================================================ */

int ufi_drive_step(int direction) {
    if (g_current_drive == DRIVE_NONE) {
        return -1;
    }
    
    if (g_current_drive == DRIVE_APPLE_II) {
        return ufi_drive_apple_step(direction);
    }
    
    // Shugart/Amiga Step
    
    // Direction setzen (active low)
    // DIR low = Step In (zur Mitte), DIR high = Step Out
    HAL_GPIO_WritePin(FDD_PORT_B, FDD_DIR_PIN,
                     (direction > 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    
    delay_us(DIR_SETUP_US);
    
    // Step-Puls (active low)
    HAL_GPIO_WritePin(FDD_PORT_B, FDD_STEP_PIN, GPIO_PIN_RESET);
    delay_us(STEP_PULSE_US);
    HAL_GPIO_WritePin(FDD_PORT_B, FDD_STEP_PIN, GPIO_PIN_SET);
    
    // Step Rate
    delay_us(STEP_RATE_US);
    
    // Track-Counter aktualisieren
    drive_status_t* status = &g_drive_status[g_current_drive];
    if (direction > 0 && status->current_track < 83) {
        status->current_track++;
    } else if (direction < 0 && status->current_track > 0) {
        status->current_track--;
    }
    
    return 0;
}

// Apple Disk II Stepper (4-Phasen)
static uint8_t apple_phase = 0;

int ufi_drive_apple_step(int direction) {
    // Apple verwendet 4-Phasen Stepper
    // Phase-Sequenz: 0-1-2-3-0-1-2-3 (vorwärts)
    //                0-3-2-1-0-3-2-1 (rückwärts)
    
    static const uint16_t phase_pins[4] = {
        APPLE_PH0_PIN, APPLE_PH1_PIN, APPLE_PH2_PIN, APPLE_PH3_PIN
    };
    
    // Aktuelle Phase aus
    HAL_GPIO_WritePin(APPLE_PORT, phase_pins[apple_phase], GPIO_PIN_RESET);
    
    // Nächste Phase
    if (direction > 0) {
        apple_phase = (apple_phase + 1) & 3;
    } else {
        apple_phase = (apple_phase - 1) & 3;
    }
    
    // Neue Phase an
    HAL_GPIO_WritePin(APPLE_PORT, phase_pins[apple_phase], GPIO_PIN_SET);
    
    delay_us(5000);  // 5ms Phase-Zeit
    
    // Track-Counter (2 Phasen = 1 Track)
    static uint8_t phase_count = 0;
    phase_count++;
    if (phase_count >= 2) {
        phase_count = 0;
        drive_status_t* status = &g_drive_status[DRIVE_APPLE_II];
        if (direction > 0 && status->current_track < 39) {
            status->current_track++;
        } else if (direction < 0 && status->current_track > 0) {
            status->current_track--;
        }
    }
    
    return 0;
}

/* ============================================================================
 * SEEK & RECALIBRATE
 * ============================================================================ */

int ufi_drive_seek(uint8_t track) {
    if (g_current_drive == DRIVE_NONE) {
        return -1;
    }
    
    drive_status_t* status = &g_drive_status[g_current_drive];
    
    // Max-Track je nach Laufwerk
    uint8_t max_track;
    switch (g_current_drive) {
        case DRIVE_APPLE_II:
            max_track = 39;  // Apple: 40 Tracks (0-39)
            break;
        case DRIVE_AMIGA:
            max_track = 83;  // Amiga: 84 Tracks (0-83)
            break;
        default:
            max_track = 83;  // PC: bis 84 Tracks
            break;
    }
    
    if (track > max_track) {
        return -2;
    }
    
    // Steps berechnen
    int steps = (int)track - (int)status->current_track;
    int direction = (steps > 0) ? 1 : -1;
    steps = (steps > 0) ? steps : -steps;
    
    // Steppen
    for (int i = 0; i < steps; i++) {
        ufi_drive_step(direction);
        
        // Track 0 Check beim Rausfahren
        if (direction < 0 && ufi_drive_at_track0()) {
            status->current_track = 0;
            break;
        }
    }
    
    // Head Settle
    delay_us(SETTLE_TIME_US);
    
    return 0;
}

int ufi_drive_recalibrate(void) {
    if (g_current_drive == DRIVE_NONE) {
        return -1;
    }
    
    drive_status_t* status = &g_drive_status[g_current_drive];
    
    // Max Steps nach außen
    int max_steps = (g_current_drive == DRIVE_APPLE_II) ? 50 : 90;
    
    for (int i = 0; i < max_steps; i++) {
        if (ufi_drive_at_track0()) {
            status->current_track = 0;
            status->track0 = true;
            delay_us(SETTLE_TIME_US);
            return 0;
        }
        ufi_drive_step(-1);
    }
    
    return -1;  // Track 0 nicht gefunden
}

/* ============================================================================
 * SEITE WÄHLEN
 * ============================================================================ */

int ufi_drive_select_side(uint8_t side) {
    if (g_current_drive == DRIVE_NONE) {
        return -1;
    }
    
    // Apple II hat keine Seiten-Auswahl
    if (g_current_drive == DRIVE_APPLE_II) {
        return (side == 0) ? 0 : -1;
    }
    
    // Side Select: Low = Seite 0, High = Seite 1
    HAL_GPIO_WritePin(FDD_PORT_B, FDD_SIDE_PIN,
                     (side == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    
    g_drive_status[g_current_drive].current_side = side;
    
    delay_us(100);  // Kurzes Settle
    
    return 0;
}

/* ============================================================================
 * STATUS ABFRAGEN
 * ============================================================================ */

bool ufi_drive_at_track0(void) {
    // Track 0 Signal ist active-low
    return HAL_GPIO_ReadPin(FDD_PORT_C, FDD_TRACK0_PIN) == GPIO_PIN_RESET;
}

bool ufi_drive_write_protected(void) {
    return HAL_GPIO_ReadPin(FDD_PORT_C, FDD_WPROT_PIN) == GPIO_PIN_RESET;
}

bool ufi_drive_disk_changed(void) {
    return HAL_GPIO_ReadPin(FDD_PORT_C, FDD_DKCHG_PIN) == GPIO_PIN_RESET;
}

bool ufi_drive_ready(void) {
    return HAL_GPIO_ReadPin(FDD_PORT_C, FDD_READY_PIN) == GPIO_PIN_RESET;
}

drive_status_t ufi_drive_get_status(void) {
    if (g_current_drive == DRIVE_NONE) {
        drive_status_t empty = {0};
        return empty;
    }
    
    drive_status_t* status = &g_drive_status[g_current_drive];
    
    // Aktuelle Signale lesen
    status->track0 = ufi_drive_at_track0();
    status->write_protected = ufi_drive_write_protected();
    status->disk_changed = ufi_drive_disk_changed();
    status->ready = ufi_drive_ready();
    
    return *status;
}

drive_type_t ufi_drive_get_current(void) {
    return g_current_drive;
}

/* Index Interrupt wurde nach stm32h7xx_it.c verschoben */
