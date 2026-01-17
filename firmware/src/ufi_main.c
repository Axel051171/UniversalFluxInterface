/**
 * UFI Flux Engine - STM32H723 Firmware
 * Main Implementation
 */

#include "ufi_firmware.h"
#include "stm32h7xx_hal.h"
#include <string.h>  /* für memcpy */

/* ============================================================================
 * GLOBALE VARIABLEN (extern referenziert in ufi_flux.c)
 * ============================================================================ */

capture_context_t g_capture;
static drive_status_t g_drive_status[5];  // Max 5 Laufwerke
static drive_type_t g_active_drive = DRIVE_NONE;

// DMA Buffer für Flux-Capture (im DTCM für schnellen Zugriff)
__attribute__((section(".dtcm")))
static uint32_t g_flux_dma_buffer[MAX_FLUX_PER_REV];

// Revolution Buffer (im AXI SRAM) - extern in ufi_flux.c
__attribute__((section(".axi_sram")))
flux_revolution_t g_revolution_buffer[REVOLUTIONS_BUFFER];

// USB TX Buffer
__attribute__((section(".axi_sram")))
static uint8_t g_usb_tx_buffer[USB_HS_BUFFER_SIZE];

/* ============================================================================
 * GPIO PIN DEFINITIONEN (global für ufi_write.c, ufi_debug.c)
 * ============================================================================ */

// FDD Shugart Interface (active low!)
const gpio_pin_t PIN_FDD_MOTOR_A     = {GPIOA, GPIO_PIN_0};
const gpio_pin_t PIN_FDD_MOTOR_B     = {GPIOA, GPIO_PIN_1};
const gpio_pin_t PIN_FDD_DRV_SEL_A   = {GPIOA, GPIO_PIN_2};
const gpio_pin_t PIN_FDD_DRV_SEL_B   = {GPIOA, GPIO_PIN_3};
const gpio_pin_t PIN_FDD_STEP        = {GPIOB, GPIO_PIN_0};
const gpio_pin_t PIN_FDD_DIR         = {GPIOB, GPIO_PIN_1};
const gpio_pin_t PIN_FDD_SIDE_SEL    = {GPIOB, GPIO_PIN_2};
const gpio_pin_t PIN_FDD_WGATE       = {GPIOB, GPIO_PIN_3};
const gpio_pin_t PIN_FDD_WDATA       = {GPIOB, GPIO_PIN_4};

// FDD Input Signale
const gpio_pin_t PIN_FDD_INDEX       = {GPIOC, GPIO_PIN_0};  // Interrupt!
const gpio_pin_t PIN_FDD_TRACK0      = {GPIOC, GPIO_PIN_1};
const gpio_pin_t PIN_FDD_WPROT       = {GPIOC, GPIO_PIN_2};
const gpio_pin_t PIN_FDD_RDATA       = {GPIOC, GPIO_PIN_3};  // Timer Capture!
const gpio_pin_t PIN_FDD_DKCHG       = {GPIOC, GPIO_PIN_4};

// IEC Bus (active low, active-loaded)
const gpio_pin_t PIN_IEC_ATN         = {GPIOD, GPIO_PIN_0};
const gpio_pin_t PIN_IEC_CLK         = {GPIOD, GPIO_PIN_1};
const gpio_pin_t PIN_IEC_DATA        = {GPIOD, GPIO_PIN_2};
const gpio_pin_t PIN_IEC_SRQ         = {GPIOD, GPIO_PIN_3};
const gpio_pin_t PIN_IEC_RESET       = {GPIOD, GPIO_PIN_4};

// Status LEDs
const gpio_pin_t PIN_LED_PWR         = {GPIOE, GPIO_PIN_0};
const gpio_pin_t PIN_LED_ACT         = {GPIOE, GPIO_PIN_1};
const gpio_pin_t PIN_LED_FDD         = {GPIOE, GPIO_PIN_2};
const gpio_pin_t PIN_LED_USB         = {GPIOE, GPIO_PIN_3};

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_init(void) {
    // HAL Init
    HAL_Init();
    
    // System Clock auf 550 MHz
    SystemClock_Config();
    
    // DWT Cycle Counter für µs-Delays (Fix #6)
    UFI_DWT_INIT();
    
    // Watchdog (Fix #9)
    UFI_WATCHDOG_INIT();
    
    // Peripherie initialisieren
    ufi_gpio_init();
    ufi_flux_init();   // Timer + DMA (in ufi_flux.c mit globalen Handles)
    ufi_write_init();  // Write-Support initialisieren
    ufi_usb_init();
    
    // Capture Buffer zuweisen
    g_capture.buffer = g_revolution_buffer;
    
    // Status initialisieren
    g_capture.state = CAPTURE_IDLE;
    g_active_drive = DRIVE_NONE;
    
    // LEDs: Power an
    HAL_GPIO_WritePin(PIN_LED_PWR.port, PIN_LED_PWR.pin, GPIO_PIN_SET);
}

void ufi_gpio_init(void) {
    GPIO_InitTypeDef gpio = {0};
    
    // Clocks aktivieren
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    
    // FDD Outputs (active low, default high)
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    
    gpio.Pin = PIN_FDD_MOTOR_A.pin | PIN_FDD_MOTOR_B.pin | 
               PIN_FDD_DRV_SEL_A.pin | PIN_FDD_DRV_SEL_B.pin;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_WritePin(GPIOA, gpio.Pin, GPIO_PIN_SET);  // Inactive (high)
    
    gpio.Pin = PIN_FDD_STEP.pin | PIN_FDD_DIR.pin | 
               PIN_FDD_SIDE_SEL.pin | PIN_FDD_WGATE.pin | PIN_FDD_WDATA.pin;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, gpio.Pin, GPIO_PIN_SET);
    
    // FDD Inputs
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = PIN_FDD_TRACK0.pin | PIN_FDD_WPROT.pin | PIN_FDD_DKCHG.pin;
    HAL_GPIO_Init(GPIOC, &gpio);
    
    // FDD INDEX - Interrupt auf fallende Flanke
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = PIN_FDD_INDEX.pin;
    HAL_GPIO_Init(GPIOC, &gpio);
    
    // FDD RDATA - Timer Capture (Alternate Function)
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF1_TIM2;  // TIM2_CH1
    gpio.Pin = PIN_FDD_RDATA.pin;
    HAL_GPIO_Init(GPIOC, &gpio);
    
    // IEC Bus (Open Drain mit Pull-up)
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Pin = PIN_IEC_ATN.pin | PIN_IEC_CLK.pin | 
               PIN_IEC_DATA.pin | PIN_IEC_SRQ.pin | PIN_IEC_RESET.pin;
    HAL_GPIO_Init(GPIOD, &gpio);
    HAL_GPIO_WritePin(GPIOD, gpio.Pin, GPIO_PIN_SET);  // Release (high)
    
    // LEDs
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = PIN_LED_PWR.pin | PIN_LED_ACT.pin | 
               PIN_LED_FDD.pin | PIN_LED_USB.pin;
    HAL_GPIO_Init(GPIOE, &gpio);
    
    // NVIC für Index-Interrupt
    HAL_NVIC_SetPriority(EXTI0_IRQn, 1, 0);  // Hohe Priorität
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/* Timer und DMA Init sind jetzt in ufi_flux.c (ufi_flux_init) */

/* ============================================================================
 * FLUX CAPTURE
 * ============================================================================ */

int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions) {
    if (g_capture.state != CAPTURE_IDLE) {
        return -1;  // Bereits aktiv
    }
    
    if (g_active_drive == DRIVE_NONE) {
        return -2;  // Kein Laufwerk aktiv
    }
    
    // Zum Track fahren
    if (ufi_drive_seek(track) != 0) {
        return -3;
    }
    
    // Seite wählen
    ufi_drive_select_side(side);
    
    // Capture konfigurieren
    g_capture.current_track = track;
    g_capture.current_side = side;
    g_capture.revolutions_requested = revolutions;
    g_capture.revolutions_captured = 0;
    g_capture.buffer = g_revolution_buffer;
    g_capture.error_code = 0;
    
    // DMA konfigurieren
    HAL_DMA_Start(DMA1_Stream0, 
                  (uint32_t)&TIM2->CCR1, 
                  (uint32_t)g_flux_dma_buffer, 
                  MAX_FLUX_PER_REV);
    
    // Timer starten
    __HAL_TIM_SET_COUNTER(TIM2, 0);
    HAL_TIM_IC_Start_DMA(TIM2, TIM_CHANNEL_1, g_flux_dma_buffer, MAX_FLUX_PER_REV);
    
    // Warte auf Index-Puls
    g_capture.state = CAPTURE_WAITING_INDEX;
    
    // LED an
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_SET);
    
    return 0;
}

int ufi_capture_abort(void) {
    HAL_TIM_IC_Stop_DMA(TIM2, TIM_CHANNEL_1);
    HAL_DMA_Abort(DMA1_Stream0);
    
    g_capture.state = CAPTURE_IDLE;
    
    HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
    
    return 0;
}

/* ============================================================================
 * LAUFWERK-STEUERUNG
 * ============================================================================ */

int ufi_drive_select(drive_type_t type) {
    // Alle Drives deselektieren
    HAL_GPIO_WritePin(PIN_FDD_DRV_SEL_A.port, PIN_FDD_DRV_SEL_A.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_FDD_DRV_SEL_B.port, PIN_FDD_DRV_SEL_B.pin, GPIO_PIN_SET);
    
    switch (type) {
        case DRIVE_SHUGART_A:
            HAL_GPIO_WritePin(PIN_FDD_DRV_SEL_A.port, PIN_FDD_DRV_SEL_A.pin, GPIO_PIN_RESET);
            break;
        case DRIVE_SHUGART_B:
            HAL_GPIO_WritePin(PIN_FDD_DRV_SEL_B.port, PIN_FDD_DRV_SEL_B.pin, GPIO_PIN_RESET);
            break;
        case DRIVE_APPLE_II:
        case DRIVE_AMIGA:
            // TODO: Spezifische Select-Logik
            break;
        case DRIVE_IEC:
            // IEC braucht kein Drive-Select
            break;
        default:
            g_active_drive = DRIVE_NONE;
            return -1;
    }
    
    g_active_drive = type;
    HAL_Delay(10);  // Settle time
    
    return 0;
}

int ufi_drive_motor(bool on) {
    gpio_pin_t motor_pin;
    
    switch (g_active_drive) {
        case DRIVE_SHUGART_A:
            motor_pin = PIN_FDD_MOTOR_A;
            break;
        case DRIVE_SHUGART_B:
            motor_pin = PIN_FDD_MOTOR_B;
            break;
        default:
            return -1;
    }
    
    HAL_GPIO_WritePin(motor_pin.port, motor_pin.pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    g_drive_status[g_active_drive].motor_on = on;
    
    if (on) {
        HAL_Delay(500);  // Motor Spin-up Zeit
    }
    
    return 0;
}

int ufi_drive_step(int direction) {
    // Direction setzen
    HAL_GPIO_WritePin(PIN_FDD_DIR.port, PIN_FDD_DIR.pin, 
                      (direction > 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_Delay(1);  // Setup time
    
    // Step-Puls (3µs minimum)
    HAL_GPIO_WritePin(PIN_FDD_STEP.port, PIN_FDD_STEP.pin, GPIO_PIN_RESET);
    for (volatile int i = 0; i < 100; i++);  // ~3µs @ 550MHz
    HAL_GPIO_WritePin(PIN_FDD_STEP.port, PIN_FDD_STEP.pin, GPIO_PIN_SET);
    
    // Step Rate: 3ms
    HAL_Delay(3);
    
    // Track-Counter aktualisieren
    if (direction > 0) {
        g_drive_status[g_active_drive].current_track++;
    } else if (g_drive_status[g_active_drive].current_track > 0) {
        g_drive_status[g_active_drive].current_track--;
    }
    
    return 0;
}

int ufi_drive_seek(uint8_t track) {
    drive_status_t* status = &g_drive_status[g_active_drive];
    
    // Zuerst recalibrate wenn Position unbekannt
    if (!status->track0 && status->current_track == 0) {
        ufi_drive_recalibrate();
    }
    
    // Steps berechnen
    int steps = (int)track - (int)status->current_track;
    int direction = (steps > 0) ? 1 : -1;
    steps = (steps > 0) ? steps : -steps;
    
    // Steppen
    for (int i = 0; i < steps; i++) {
        ufi_drive_step(direction);
    }
    
    // Head Settle Time
    HAL_Delay(15);
    
    return 0;
}

int ufi_drive_recalibrate(void) {
    // Bis Track 0 steppen (max 85 Steps)
    for (int i = 0; i < 85; i++) {
        // Track0 Signal prüfen
        if (HAL_GPIO_ReadPin(PIN_FDD_TRACK0.port, PIN_FDD_TRACK0.pin) == GPIO_PIN_RESET) {
            g_drive_status[g_active_drive].current_track = 0;
            g_drive_status[g_active_drive].track0 = true;
            HAL_Delay(15);  // Settle
            return 0;
        }
        ufi_drive_step(-1);  // Step out
    }
    
    return -1;  // Track 0 nicht gefunden
}

int ufi_drive_select_side(uint8_t side) {
    HAL_GPIO_WritePin(PIN_FDD_SIDE_SEL.port, PIN_FDD_SIDE_SEL.pin, 
                      (side == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    g_drive_status[g_active_drive].current_side = side;
    HAL_Delay(1);  // Settle
    return 0;
}

drive_status_t ufi_drive_get_status(void) {
    drive_status_t* status = &g_drive_status[g_active_drive];
    
    // Aktuelle Signale lesen
    status->track0 = (HAL_GPIO_ReadPin(PIN_FDD_TRACK0.port, PIN_FDD_TRACK0.pin) == GPIO_PIN_RESET);
    status->write_protected = (HAL_GPIO_ReadPin(PIN_FDD_WPROT.port, PIN_FDD_WPROT.pin) == GPIO_PIN_RESET);
    status->disk_changed = (HAL_GPIO_ReadPin(PIN_FDD_DKCHG.port, PIN_FDD_DKCHG.pin) == GPIO_PIN_RESET);
    
    return *status;
}

/* ============================================================================
 * IEC BUS (C64)
 * ============================================================================ */

// IEC Timing (µs)
#define IEC_T_NE    40      // Non-EOI Response
#define IEC_T_S     70      // Send
#define IEC_T_R     20      // Release

static void iec_delay_us(uint32_t us) {
    // Präzise Verzögerung mit DWT Cycle Counter
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SYSCLK_FREQ / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

static void iec_clk(bool low) {
    HAL_GPIO_WritePin(PIN_IEC_CLK.port, PIN_IEC_CLK.pin, low ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void iec_data(bool low) {
    HAL_GPIO_WritePin(PIN_IEC_DATA.port, PIN_IEC_DATA.pin, low ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static bool iec_read_clk(void) {
    return HAL_GPIO_ReadPin(PIN_IEC_CLK.port, PIN_IEC_CLK.pin) == GPIO_PIN_RESET;
}

static bool iec_read_data(void) {
    return HAL_GPIO_ReadPin(PIN_IEC_DATA.port, PIN_IEC_DATA.pin) == GPIO_PIN_RESET;
}

int ufi_iec_reset(void) {
    HAL_GPIO_WritePin(PIN_IEC_RESET.port, PIN_IEC_RESET.pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(PIN_IEC_RESET.port, PIN_IEC_RESET.pin, GPIO_PIN_SET);
    HAL_Delay(500);  // 1541 Boot-Zeit
    return 0;
}

int ufi_iec_atn(bool state) {
    HAL_GPIO_WritePin(PIN_IEC_ATN.port, PIN_IEC_ATN.pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return 0;
}

int ufi_iec_send_byte(uint8_t byte, bool eoi) {
    uint32_t timeout_start;
    
    // Listener bereit?
    if (!iec_read_data()) {
        return UFI_ERR_IEC_NRFD;
    }
    
    // EOI Handshake
    if (eoi) {
        iec_delay_us(200);
        
        // ⚠️ FIX #2: Timeout statt Endlosschleife!
        // Warte auf Listener EOI-ACK (DATA high)
        timeout_start = DWT->CYCCNT;
        while (!iec_read_data()) {
            if ((DWT->CYCCNT - timeout_start) > IEC_TIMEOUT_CYCLES)
                return UFI_ERR_TIMEOUT;
        }
        // Warte auf DATA low
        timeout_start = DWT->CYCCNT;
        while (iec_read_data()) {
            if ((DWT->CYCCNT - timeout_start) > IEC_TIMEOUT_CYCLES)
                return UFI_ERR_TIMEOUT;
        }
    }
    
    // 8 Bits senden
    for (int i = 0; i < 8; i++) {
        iec_clk(true);
        iec_data((byte & (1 << i)) == 0);  // Inverted!
        iec_delay_us(IEC_T_S);
        iec_clk(false);
        iec_delay_us(IEC_T_R);
    }
    
    iec_data(false);  // Release
    
    // Warte auf ACK
    iec_delay_us(1000);
    if (iec_read_data()) {
        return UFI_ERR_IEC_NOACK;
    }
    
    return UFI_OK;
}

int ufi_iec_receive_byte(uint8_t* byte) {
    uint32_t timeout_start;
    *byte = 0;
    
    // Ready to receive
    iec_data(false);
    
    // ⚠️ FIX #2: Timeout statt Endlosschleife!
    // Warte auf Talker (CLK high)
    timeout_start = DWT->CYCCNT;
    while (!iec_read_clk()) {
        if ((DWT->CYCCNT - timeout_start) > IEC_TIMEOUT_CYCLES)
            return UFI_ERR_TIMEOUT;
    }
    
    // 8 Bits empfangen
    for (int i = 0; i < 8; i++) {
        // Warte CLK low
        timeout_start = DWT->CYCCNT;
        while (iec_read_clk()) {
            if ((DWT->CYCCNT - timeout_start) > IEC_TIMEOUT_CYCLES)
                return UFI_ERR_TIMEOUT;
        }
        
        if (iec_read_data()) {
            *byte |= (1 << i);
        }
        
        // Warte CLK high
        timeout_start = DWT->CYCCNT;
        while (!iec_read_clk()) {
            if ((DWT->CYCCNT - timeout_start) > IEC_TIMEOUT_CYCLES)
                return UFI_ERR_TIMEOUT;
        }
    }
    
    // ACK senden
    iec_data(true);
    iec_delay_us(IEC_T_NE);
    iec_data(false);
    
    return UFI_OK;
}

/* ============================================================================
 * INDEX PULSE HANDLER (aufgerufen von stm32h7xx_it.c)
 * ============================================================================ */

void ufi_flux_index_handler(void) {
    // Index-Puls erkannt!
    uint32_t index_time = __HAL_TIM_GET_COUNTER(TIM2);
    
    // Write-Modus?
    write_state_t ws = ufi_write_get_state();
    if (ws == WRITE_WAITING_INDEX || ws == WRITE_ACTIVE) {
        ufi_write_index_handler();
        return;
    }
    
    // Read-Modus
    if (g_capture.state == CAPTURE_WAITING_INDEX) {
        // Capture starten
        __HAL_TIM_SET_COUNTER(TIM2, 0);
        g_capture.state = CAPTURE_RUNNING;
    }
    else if (g_capture.state == CAPTURE_RUNNING) {
        // ⚠️ FIX #1: DMA ERST STOPPEN vor memcpy!
        HAL_DMA_Abort(DMA1_Stream0);
        
        // Jetzt sicher Position lesen
        uint32_t dma_pos = MAX_FLUX_PER_REV - __HAL_DMA_GET_COUNTER(DMA1_Stream0);
        
        // Daten in Revolution-Buffer kopieren (jetzt sicher!)
        flux_revolution_t* rev = &g_capture.buffer[g_capture.revolutions_captured];
        rev->count = dma_pos;
        rev->index_time = index_time;
        rev->revolution = g_capture.revolutions_captured;
        memcpy(rev->samples, g_flux_dma_buffer, dma_pos * sizeof(uint32_t));
        
        g_capture.revolutions_captured++;
        
        // Mehr Revolutions nötig?
        if (g_capture.revolutions_captured >= g_capture.revolutions_requested) {
            g_capture.state = CAPTURE_COMPLETE;
            HAL_GPIO_WritePin(PIN_LED_FDD.port, PIN_LED_FDD.pin, GPIO_PIN_RESET);
        } else {
            // Nächste Revolution, Timer Reset
            __HAL_TIM_SET_COUNTER(TIM2, 0);
            // DMA neu starten für nächste Revolution
            HAL_DMA_Start(DMA1_Stream0, 
                          (uint32_t)&TIM2->CCR1, 
                          (uint32_t)g_flux_dma_buffer, 
                          MAX_FLUX_PER_REV);
        }
    }
}

/* ============================================================================
 * HAUPTSCHLEIFE
 * ============================================================================ */

void ufi_main_loop(void) {
    static uint32_t last_blink = 0;
    
    while (1) {
        // Watchdog füttern (Fix #9)
        UFI_WATCHDOG_FEED();
        
        // USB Befehle verarbeiten
        ufi_usb_process_command();
        
        // Write-Prozess (wenn aktiv)
        ufi_write_process();
        
        // Capture-Daten senden wenn fertig
        if (g_capture.state == CAPTURE_COMPLETE) {
            // Alle Revolutions an CM5 senden
            for (int i = 0; i < g_capture.revolutions_captured; i++) {
                flux_packet_header_t header = {
                    .track = g_capture.current_track,
                    .side = g_capture.current_side,
                    .revolution = i,
                    .flags = 0x01,  // Index gefunden
                    .index_time = g_capture.buffer[i].index_time,
                    .sample_count = g_capture.buffer[i].count
                };
                ufi_usb_send_flux(&header, g_capture.buffer[i].samples);
            }
            
            g_capture.state = CAPTURE_IDLE;
        }
        
        // Write Complete? Verify starten wenn angefordert
        if (ufi_write_get_state() == WRITE_COMPLETE) {
            // Automatisches Verify wenn angefordert wird im Command Handler gemacht
        }
        
        // Status-LED blinken wenn aktiv (Fix #12: mit Timer)
        if (g_capture.state == CAPTURE_RUNNING || 
            ufi_write_get_state() == WRITE_ACTIVE) {
            uint32_t now = HAL_GetTick();
            if (now - last_blink > 100) {
                HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
                last_blink = now;
            }
        }
    }
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    ufi_init();
    ufi_main_loop();
    return 0;
}
