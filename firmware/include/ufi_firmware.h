/**
 * UFI Flux Engine - STM32H723 Firmware
 * 
 * Aufgabe: Nur Echtzeit-kritische Flux-Erfassung und Laufwerk-Steuerung
 * Keine Dekodierung, keine Analyse - das macht CM5!
 */

#ifndef UFI_FIRMWARE_H
#define UFI_FIRMWARE_H

#include "ufi_fixes.h"  /* MUSS zuerst! */
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * HARDWARE KONFIGURATION
 * ============================================================================ */

// Clock: 550 MHz (max für STM32H723)
#define SYSCLK_FREQ         550000000UL
#define FLUX_TIMER_FREQ     275000000UL  // TIM2/TIM5 @ 275 MHz
#define FLUX_RESOLUTION_NS  3.6          // ~3.6ns pro Tick

// Timer für Flux-Capture
#define FLUX_TIMER          TIM2         // 32-bit Timer
#define FLUX_TIMER_CH       TIM_CHANNEL_1
#define FLUX_DMA            DMA1_Stream0

// USB High-Speed
#define USB_HS_BUFFER_SIZE  (64 * 1024)  // 64 KB Ring-Buffer
#define USB_BULK_EP_SIZE    512          // USB HS Bulk max

// Laufwerk-Steuerung GPIOs
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} gpio_pin_t;

/* ============================================================================
 * FLUX CAPTURE
 * ============================================================================ */

// Flux-Daten Format (Raw Timing)
typedef struct __packed {
    uint32_t timestamp;     // Timer-Wert bei Flanke (32-bit)
} flux_sample_t;

// Track-Buffer
#define MAX_FLUX_PER_REV    50000       // Max Flux-Übergänge pro Umdrehung
#define REVOLUTIONS_BUFFER  5           // Puffer für 5 Umdrehungen

typedef struct {
    flux_sample_t samples[MAX_FLUX_PER_REV];
    uint32_t count;
    uint32_t index_time;    // Zeitpunkt des Index-Pulses
    uint8_t revolution;     // Umdrehungs-Nummer
} flux_revolution_t;

// Capture State Machine
typedef enum {
    CAPTURE_IDLE,
    CAPTURE_WAITING_INDEX,  // Warte auf Index-Puls
    CAPTURE_RUNNING,        // Erfasse Flux-Daten
    CAPTURE_COMPLETE,       // Track fertig
    CAPTURE_ERROR
} capture_state_t;

typedef struct {
    capture_state_t state;
    uint8_t current_track;
    uint8_t current_side;
    uint8_t revolutions_requested;
    uint8_t revolutions_captured;
    flux_revolution_t* buffer;
    uint32_t error_code;
} capture_context_t;

/* ============================================================================
 * LAUFWERK-STEUERUNG
 * ============================================================================ */

// Unterstützte Laufwerk-Typen
typedef enum {
    DRIVE_NONE = 0,
    DRIVE_SHUGART_A,        // FDD1 (34-pin)
    DRIVE_SHUGART_B,        // FDD2 (34-pin)
    DRIVE_APPLE_II,         // Apple Disk II (19-pin)
    DRIVE_AMIGA,            // Amiga External (via Header)
    DRIVE_IEC               // C64 1541/1571 (DIN-6)
} drive_type_t;

// Laufwerk-Status
typedef struct {
    drive_type_t type;
    bool motor_on;
    bool write_protected;
    bool track0;
    bool disk_changed;
    bool ready;
    uint8_t current_track;
    uint8_t current_side;
    uint16_t rpm;           // Gemessene Drehzahl
} drive_status_t;

// Laufwerk-Befehle
typedef enum {
    CMD_MOTOR_ON,
    CMD_MOTOR_OFF,
    CMD_STEP_IN,            // Track++ (zur Mitte)
    CMD_STEP_OUT,           // Track-- (nach außen)
    CMD_SELECT_SIDE,        // 0 oder 1
    CMD_SELECT_DRIVE,       // Drive A/B
    CMD_SEEK_TRACK,         // Zu Track N fahren
    CMD_RECALIBRATE         // Zu Track 0 fahren
} drive_command_t;

/* ============================================================================
 * USB PROTOKOLL (zu CM5)
 * ============================================================================ */

// USB Endpoints
#define EP_CONTROL          0x00
#define EP_BULK_IN          0x81    // Flux-Daten → CM5
#define EP_BULK_OUT         0x02    // Befehle ← CM5

// Befehle von CM5
typedef enum {
    UFI_CMD_NOP             = 0x00,
    UFI_CMD_GET_INFO        = 0x01,
    UFI_CMD_GET_STATUS      = 0x02,
    
    // Laufwerk-Steuerung
    UFI_CMD_SELECT_DRIVE    = 0x10,
    UFI_CMD_MOTOR_ON        = 0x11,
    UFI_CMD_MOTOR_OFF       = 0x12,
    UFI_CMD_SEEK            = 0x13,
    UFI_CMD_RECALIBRATE     = 0x14,
    UFI_CMD_SELECT_SIDE     = 0x15,
    
    // Flux-Capture
    UFI_CMD_READ_TRACK      = 0x20,
    UFI_CMD_READ_TRACK_RAW  = 0x21, // Mehrere Umdrehungen
    UFI_CMD_ABORT_READ      = 0x2F,
    
    // Flux-Write (für Disk-Erstellung)
    UFI_CMD_WRITE_TRACK         = 0x30,
    UFI_CMD_WRITE_TRACK_VERIFY  = 0x32,  // Write mit Verify
    UFI_CMD_ERASE_TRACK         = 0x31,
    
    // IEC Bus (C64)
    UFI_CMD_IEC_RESET       = 0x40,
    UFI_CMD_IEC_SEND        = 0x41,
    UFI_CMD_IEC_RECEIVE     = 0x42,
    
    // Debug
    UFI_CMD_DEBUG_GPIO      = 0xD0,
    UFI_CMD_DEBUG_TIMER     = 0xD1,
    
    // System
    UFI_CMD_RESET           = 0xF0,
    UFI_CMD_BOOTLOADER      = 0xFF
} ufi_command_t;

// Antwort-Header
typedef struct __packed {
    uint8_t command;        // Echo des Befehls
    uint8_t status;         // 0=OK, sonst Fehler
    uint16_t length;        // Länge der Daten
} ufi_response_header_t;

// Flux-Daten Paket
typedef struct __packed {
    uint8_t track;
    uint8_t side;
    uint8_t revolution;
    uint8_t flags;          // Bit0: Index gefunden, Bit1: Überlauf
    uint32_t index_time;    // Zeit des Index-Pulses
    uint32_t sample_count;  // Anzahl Flux-Samples
    // Danach: flux_sample_t samples[]
} flux_packet_header_t;

/* ============================================================================
 * FIRMWARE FUNKTIONEN
 * ============================================================================ */

// Initialisierung
void ufi_init(void);
void ufi_gpio_init(void);
void ufi_flux_init(void);  // Timer + DMA (in ufi_flux.c)
void ufi_usb_init(void);

// Hauptschleife
void ufi_main_loop(void);

// Flux-Capture
int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions);
int ufi_capture_abort(void);
capture_state_t ufi_capture_get_state(void);
flux_revolution_t* ufi_capture_get_data(uint8_t revolution);

// Laufwerk-Steuerung
int ufi_drive_select(drive_type_t type);
int ufi_drive_motor(bool on);
int ufi_drive_step(int direction);  // +1 = in, -1 = out
int ufi_drive_seek(uint8_t track);
int ufi_drive_recalibrate(void);
int ufi_drive_select_side(uint8_t side);
drive_status_t ufi_drive_get_status(void);

// IEC Bus (C64)
int ufi_iec_reset(void);
int ufi_iec_send_byte(uint8_t byte, bool eoi);
int ufi_iec_receive_byte(uint8_t* byte);
int ufi_iec_atn(bool state);

// USB Kommunikation
int ufi_usb_send_flux(flux_packet_header_t* header, flux_sample_t* data);
int ufi_usb_process_command(void);

/* ============================================================================
 * WRITE SUPPORT (ufi_write.c)
 * ============================================================================ */

// Write State Machine
typedef enum {
    WRITE_IDLE,
    WRITE_RECEIVING,
    WRITE_WAITING_INDEX,
    WRITE_ACTIVE,
    WRITE_COMPLETE,
    WRITE_VERIFYING,
    WRITE_ERROR
} write_state_t;

// Write Funktionen
void ufi_write_init(void);
int ufi_write_prepare(uint8_t track, uint8_t side, uint32_t flux_count, bool verify);
int ufi_write_receive_chunk(uint8_t* data, uint32_t len);
bool ufi_write_data_complete(void);
int ufi_write_start(void);
void ufi_write_index_handler(void);
void ufi_write_process(void);
int ufi_erase_track(uint8_t track, uint8_t side);
int ufi_write_verify(void);
write_state_t ufi_write_get_state(void);
uint32_t ufi_write_get_progress(void);
void ufi_write_abort(void);
void ufi_write_set_precomp(bool enable);

/* ============================================================================
 * DEBUG FUNKTIONEN (ufi_debug.c)
 * ============================================================================ */

// GPIO Debug Strukturen
typedef struct __packed {
    uint16_t fdd_inputs;
    uint16_t fdd_outputs;
    uint16_t iec_signals;
    uint16_t leds;
} gpio_status_t;

typedef struct __packed {
    uint32_t tim2_counter;
    uint32_t tim2_prescaler;
    uint32_t tim2_period;
    uint32_t sysclk_freq;
    uint32_t hclk_freq;
    uint32_t pclk1_freq;
    uint32_t uptime_ms;
} timer_status_t;

typedef struct __packed {
    uint32_t flash_size;
    uint32_t ram_size;
    uint32_t unique_id[3];
    uint16_t revision;
    uint16_t device_id;
} memory_info_t;

// Debug Funktionen
gpio_status_t ufi_debug_gpio_read(void);
int ufi_debug_gpio_set(uint8_t gpio_id, uint8_t state);
timer_status_t ufi_debug_timer_read(void);
uint32_t ufi_debug_measure_index(void);
uint16_t ufi_debug_measure_rpm(void);
memory_info_t ufi_debug_memory_read(void);
void ufi_debug_led_test(void);
uint8_t ufi_debug_selftest(void);

/* ============================================================================
 * INTERRUPT HANDLER
 * ============================================================================ */

// Flux-Timer Capture (höchste Priorität!)
void TIM2_IRQHandler(void);

// DMA Transfer Complete
void DMA1_Stream0_IRQHandler(void);

// Index-Puls Interrupt
void EXTI0_IRQHandler(void);

// USB High-Speed
void OTG_HS_IRQHandler(void);

/* ============================================================================
 * SYSTEM FUNCTIONS
 * ============================================================================ */

// Clock Konfiguration (ufi_clock.c)
void SystemClock_Config(void);
void Error_Handler(void);

#endif // UFI_FIRMWARE_H
