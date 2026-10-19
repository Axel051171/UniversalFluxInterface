/**
 * UFI Flux Engine - STM32H723 Firmware
 * 
 * Aufgabe: Nur Echtzeit-kritische Flux-Erfassung und Laufwerk-Steuerung
 * Keine Dekodierung, keine Analyse - das macht CM5!
 */

#ifndef UFI_FIRMWARE_H
#define UFI_FIRMWARE_H

#include "ufi_fixes.h"  /* MUSS zuerst! */
#include "ufi_write_codec.h"
#include "board.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * HARDWARE KONFIGURATION
 * ============================================================================ */

// Clock: 550 MHz (max für STM32H723)
#define SYSCLK_FREQ         550000000UL
#define FLUX_TIMER_FREQ     275000000UL  // TIM2/TIM5 @ 275 MHz
#define FLUX_RESOLUTION_NS  3.6          // ~3.6ns pro Tick

// Timer für Flux-Capture (Pins/Kanäle in board.h)
#define FLUX_TIMER          TIM2         // 32-bit Timer: CH1 = RDATA (DMA), CH2 = INDEX
#define FLUX_DMA            DMA1_Stream0

// USB High-Speed
#define USB_HS_BUFFER_SIZE  (64 * 1024)  // 64 KB Ring-Buffer
#define USB_BULK_EP_SIZE    512          // USB HS Bulk max

/* ============================================================================
 * FLUX CAPTURE
 * ============================================================================ */

// Flux-Daten Format (Raw Timing)
typedef struct __packed {
    uint32_t timestamp;     // Timer-Wert bei Flanke (32-bit)
} flux_sample_t;

// Flux store, shared by capture and write: the 8 MB QSPI PSRAM (2M samples, memory-mapped)
// when fitted and working, else a 224 KB AXI SRAM fallback (56k samples, >= 2 HD revs).
#define FLUX_STORE_FALLBACK_WORDS   (56 * 1024)
#define REVOLUTIONS_BUFFER  20          // Max Umdrehungen pro Capture (READ_TRACK_RAW)
#define REVOLUTIONS_STREAM  200         // READ_TRACK: the store is a ring buffer, more revolutions

// One revolution = slice of the flux store between two index pulses.
// Timestamps are TIM2 ticks relative to the index pulse that starts the revolution.
typedef struct {
    flux_sample_t* samples;
    uint32_t count;
    uint32_t index_time;    // Dauer der Umdrehung (Ticks Index -> Index)
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
    volatile capture_state_t state;
    uint8_t current_track;
    uint8_t current_side;
    uint8_t current_quarter;    /* position byte: 0 whole track, 0x80 | quarter << 4 (Apple, 1.18) */
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
    DRIVE_IEC,              // C64 1541/1571 (DIN-6)
    // 34-pin Shugart bus (straight cable, drives jumpered DS0-DS3, shared MOTOR ON pin 16):
    // DS0 pin 10, DS1 pin 12, DS2 pin 14, DS3 pin 6 (only with solder jumper JP1)
    DRIVE_SHUGART_DS0,
    DRIVE_SHUGART_DS1,
    DRIVE_SHUGART_DS2,
    DRIVE_SHUGART_DS3,
    DRIVE_AMIGA2,           // second Amiga drive DF2: J7 pin 9 SEL2B = DRV_SEL_A (board v0.7, JP3)
    DRIVE_APPLE2,           // second Apple drive: /ENABLE on J15 (board v0.7); DRIVE_APPLE_II = J14
    DRIVE_TYPE_COUNT
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
    UFI_CMD_CHECK_DISK      = 0x16, // -> [changed, present]
    UFI_CMD_DRIVE_TIMING    = 0x17, // no args: get; + drive_timing_t (14 bytes): set
    UFI_CMD_AMIGA_ID        = 0x18, // -> u32 drive ID
    UFI_CMD_USB_POWER       = 0x19, // -> usb_power_t (CC1/CC2 mV, allowed source mA)
    UFI_CMD_SET_LINES       = 0x1A, // [density, drate]: assert J6 pin 2 / pin 6 (drive dependent)
    UFI_CMD_BOARD_STATUS    = 0x1B, // [power mask] optional: set drive supplies -> board_status_t
    UFI_CMD_PROBE_TRACKS    = 0x1C, // -> u8 highest reachable track (steps to the end stop!)
    UFI_CMD_SD_INFO         = 0x1D, // -> sd_info_t (card detect + init)
    UFI_CMD_SEEK_TEST       = 0x1E, // [track_a, track_b, cycles]: seek back and forth (diagnostics)
    
    // Flux-Capture
    UFI_CMD_READ_TRACK      = 0x20, // streamed, compact: UFI_EVT_FLUX_STREAM messages (ufi_stream.c)
    UFI_CMD_READ_TRACK_RAW  = 0x21, // after the capture: UFI_EVT_FLUX + u32 samples per revolution
    UFI_CMD_ABORT_READ      = 0x2F,
    UFI_EVT_FLUX            = 0x2E, // device -> host: flux_packet_header_t + samples
    UFI_EVT_READ_DONE       = 0x2D, // device -> host: payload [revolutions sent]
    UFI_EVT_FLUX_STREAM     = 0x2C, // device -> host: encoded flux stream bytes (ufi_stream.c)
    
    // Flux-Write (für Disk-Erstellung)
    UFI_CMD_WRITE_TRACK         = 0x30,
    UFI_CMD_WRITE_TRACK_VERIFY  = 0x32,  // Write mit Verify
    UFI_CMD_ERASE_TRACK         = 0x31,
    UFI_CMD_WRITE_TRACK_C       = 0x33,  // [track, side, flux_count u32, byte_count u32, verify]
                                         // + byte_count bytes in the READ_TRACK stream code
    UFI_CMD_WRITE_PATTERN       = 0x34,  // [track, side, interval_ns u16, duration_ms u16]: constant flux
    
    // IEC Bus (C64)
    UFI_CMD_IEC_RESET       = 0x40,
    UFI_CMD_IEC_SEND        = 0x41,
    UFI_CMD_IEC_RECEIVE     = 0x42,

    // SD NAND storage (v0.6, ufi_dump.c / ufi_msc.c)
    UFI_CMD_DUMP_START      = 0x50, // [drive, tracks, sides, revs] optional: dump to DUMPnnnn.SCP
    UFI_CMD_DUMP_STATUS     = 0x51, // -> dump_status_t
    UFI_CMD_DUMP_ABORT      = 0x52,
    UFI_CMD_USB_MSC         = 0x53, // reply, then re-enumerate as USB mass storage (SD NAND)
    UFI_CMD_COPY_START      = 0x54, // disk to disk copy (UFI.CFG copy_from / copy_to, tracks, sides)

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

// Drive timing / options (wire format = in-memory: 11 x u16 little endian).  A shorter
// DRIVE_TIMING payload (e.g. the original 7 or 10 fields) only overwrites the leading fields.
typedef struct __packed {
    uint16_t step_pulse_us;
    uint16_t step_rate_us;
    uint16_t settle_us;         // head settle after a seek
    uint16_t dir_change_us;     // extra settle when the step direction reverses
    uint16_t side_settle_us;
    uint16_t spinup_ms;
    uint16_t select_settle_us;
    uint16_t motor_off_s;       // motor off after this idle time, 0 = never
    uint16_t double_step;       // 1 = two steps per track (40-track disk in an 80-track drive)
    uint16_t precomp_ns;        // write precompensation: 0xFFFF = by data rate, 0 = off
    uint16_t tg43;              // 1 = assert DENSITY (pin 2 = TG43 on 8" drives) from track 43 (1.16)
} drive_timing_t;
#define PRECOMP_AUTO    0xFFFFu

// BOARD_STATUS payload (8 bytes)
typedef struct __packed {
    uint8_t power;              // bit0 FDD_5V on, bit1 FDD_12V on
    uint8_t flags;              // bit0 write lock jumper, bit1/2 5V/12V overcurrent trip,
                                // bit3 SD NAND ready, bit4/5 button A/B pressed,
                                // bit6 safe mode (button B held at start-up)
    uint16_t i5_ma;             // drive supply currents
    uint16_t i12_ma;
    uint16_t board_id_mv;       // BOARD_ID divider (1650 = v0.5)
} board_status_t;

// DUMP_START arguments (defaults: drive A, 80 tracks, 2 sides, 3 revolutions)
typedef struct __packed {
    uint8_t drive;              // drive_type_t
    uint8_t tracks;             // cylinders, 1..84
    uint8_t sides;              // 1 or 2
    uint8_t revs;               // 1..REVOLUTIONS_BUFFER
} dump_config_t;

// DUMP_STATUS payload (8 bytes)
typedef struct __packed {
    uint8_t state;              // 0 idle, 1-5 running, 6 done, 7 error, 8 copy, 9 IEC dump, 10-12 write from SD (1.16)
    uint8_t error;              // 1 storage, 2 drive/read, 3 disk full/write, 4 aborted, 5 format not recognised (img/adf)
    uint8_t track;              // cylinder / head in progress (or where it stopped)
    uint8_t side;
    uint16_t file_no;           // DUMPnnnn.SCP
    uint8_t fs_result;          // last FatFs mount result (FRESULT)
    uint8_t storage_ready;      // SD NAND initialised
} dump_status_t;

// SD_INFO payload (8 bytes)
typedef struct __packed {
    uint8_t present;            // SD NAND initialised (v0.6; v0.5: card detect switch)
    uint8_t status;             // 0 = initialised, else UFI error code (positive)
    uint8_t card_type;          // HAL CardType (0 SDSC, 1 SDHC/SDXC)
    uint8_t bus_width;          // 1 or 4
    uint32_t capacity_mb;
} sd_info_t;

// USB_POWER payload (6 bytes); current_ma 0 = no Type-C source detected
typedef struct __packed {
    uint16_t cc1_mv;
    uint16_t cc2_mv;
    uint16_t current_ma;
} usb_power_t;
int ufi_usb_power(usb_power_t* p);   // ufi_power.c

// GET_STATUS payload (wire format, 10 bytes)
typedef struct __packed {
    uint8_t type;           // drive_type_t
    uint8_t motor_on;
    uint8_t write_protected;
    uint8_t track0;
    uint8_t disk_changed;
    uint8_t ready;
    uint8_t current_track;
    uint8_t current_side;
    uint16_t rpm;
} drive_status_wire_t;

// Flux-Daten Paket: every device message starts with ufi_response_header_t;
// a revolution is {UFI_EVT_FLUX, 0, 12} + this header + sample_count x u32,
// a capture ends with {UFI_EVT_READ_DONE, status, 1} + [revolutions sent]
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
void ufi_request_bootloader(void);   // reset into the ROM USB-DFU bootloader
void ufi_flux_init(void);  // Timer + DMA (in ufi_flux.c)
void ufi_drive_init(void);
void ufi_iec_init(void);
void ufi_usb_init(void);

// Hauptschleife
void ufi_main_loop(void);

// Flux-Capture (high level, ufi_main.c: seek + side + capture)
int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks);
bool ufi_operation_busy(void);
int ufi_iec_read_block_status(uint8_t device, uint8_t track, uint8_t sector,
                              uint8_t* buffer, uint16_t* len, uint8_t* dos);
int ufi_capture_start_q(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks,
                        uint8_t quarter);   // 1.14: Apple quarter track offset
void ufi_write_set_quarter(uint8_t quarter);    // after ufi_write_prepare*(): seek offset (1.14)
void ufi_write_set_scale(uint16_t per_mille);   // after ufi_write_prepare*(): time scale, 1000 = 1.0 (1.14)
void ufi_write_set_clamp(bool allow);           // after ufi_write_prepare*(): clamp out-of-range periods (1.15)
uint32_t ufi_write_clamped(void);               // periods clamped by the last write
void ufi_write_receive_failed(void);            // v1 data path: abort + error event
// track analysis (ufi_analyze.c, 1.14): ANALYZE 0x25, blocking capture of `revs` revolutions
typedef struct {
    uint8_t revs;
    uint8_t encoding;           // 0 unknown, 1 FM, 2 MFM, 3 GCR
    uint16_t bitcell_ns;
    uint16_t rpm_x100;
    uint16_t peak_ns[4];        // interval histogram peaks, ascending (0 = none)
    uint32_t index_ticks;       // revolution time
    uint32_t transitions;       // revolution 0
    uint32_t bitcells;          // track length in bit cells (0 = unknown encoding)
    uint8_t sectors;            // IBM MFM sectors with good CRC (revolution 0)
    uint8_t weak_count;         // 1/64-revolution windows whose counts differ between revs
    uint32_t first_sector_us;   // index -> first good sector ID
    uint64_t weak_mask;         // bit w = window w weak
} analyze_result_t;             // 40 bytes, packed little endian on the wire
int ufi_analyze_track(uint8_t track, uint8_t side, uint8_t revs, analyze_result_t* r);
/* position: 0 = whole track, 0x80 | quarter << 4 = Apple quarter track (1.18) */
int ufi_analyze_track_q(uint8_t track, uint8_t side, uint8_t revs, uint8_t position, analyze_result_t* r);
/* histogram classification of one revolution (shared with the track survey, op 46): rpm, peaks, encoding,
 * bit cells, optional IBM MFM sector count; t0 = HAL tick the caller started at */
int ufi_analyze_classify(const flux_revolution_t* rev, uint32_t t0, bool sectors, analyze_result_t* r);
/* op 42 (1.18): schema 1, revs, track, side/position byte, then 64 x max-min transition count per window */
uint16_t ufi_analyze_weak_report(uint8_t* out);
uint16_t ufi_analyze_weak_positions(uint8_t threshold, uint8_t* out);   // op 44 (4.13): fine windows above threshold, <= 266 bytes
#define UFI_PROVENANCE_SIZE 96u                  // ANALYSIS_JOB op 43 (4.12): provenance block, schema 1
uint16_t ufi_provenance_block(uint8_t* out);    // ufi_v2.c: board, firmware, drive setting, generations, last capture
bool ufi_v2_read_provenance(void);              // ufi_v2.c: the open READ asked for the block in its END (flag bit3)
bool ufi_v2_read_progress(void);                // ufi_v2.c: the open READ asked for one event per revolution (flag bit4)
uint16_t ufi_quality_report(uint8_t* out, uint16_t capacity);
void ufi_quality_clear(void);
int ufi_quality_analyze(uint8_t format, uint32_t rate, bool adaptive);
void ufi_write_set_source_index(uint32_t ticks);
void ufi_write_set_expected(uint64_t sectors,uint8_t c,uint8_t h,uint8_t n);
uint16_t ufi_write_coverage(uint8_t* out);
// head steps on a schedule during the next READ / WRITE (ufi_sched.c, 1.14)
int ufi_sched_set(const uint8_t* items, uint8_t n, bool quarter_units);   // n x (at_ms u16, pos u8)
uint8_t ufi_sched_pending(void);
void ufi_sched_arm(uint32_t t);             // operation start (flux-timer time)
void ufi_sched_clear(void);
void ufi_sched_service(void);               // main loop
int ufi_capture_abort(void);
capture_state_t ufi_capture_get_state(void);
flux_revolution_t* ufi_capture_get_data(uint8_t revolution);

// Flux engine (ufi_flux.c)
int ufi_flux_capture_start(uint8_t revolutions, uint32_t period_ticks);  // period 0 = index pulses
int ufi_flux_capture_gw(uint32_t ticks, uint16_t indexes, uint32_t linger);
void ufi_flux_index_mask(uint16_t us);
void ufi_flux_evidence_context(uint8_t out[6]);
uint32_t ufi_flux_gw_origin(void);
bool ufi_flux_gw_past_end(uint32_t t);
int ufi_flux_capture_stop(void);
capture_state_t ufi_flux_poll(void);           // finalises a completed capture
flux_revolution_t* ufi_flux_get_revolution(uint8_t index);
uint8_t ufi_flux_get_revolution_count(void);
uint32_t* ufi_flux_store(uint32_t* words);      // shared buffer (write path uses it too)
bool ufi_flux_store_is_psram(void);
int ufi_psram_init(void);                       // ufi_psram.c
uint32_t ufi_flux_now(void);                    // free-running TIM2 counter
uint64_t ufi_flux_now64(void);
void ufi_write_set_extended(bool on);
uint32_t ufi_flux_gw_cursor(void);
void ufi_flux_tim2_irq(void);
void ufi_flux_dma_irq(void);
// live access for streaming (thread context)
void ufi_flux_set_streaming(bool on);           // on: no in-place finalisation
uint32_t ufi_flux_written(void);                // samples DMA has stored so far
uint8_t ufi_flux_index_count(void);             // index pulses seen (index 0 = start)
uint32_t ufi_flux_index_time(uint8_t k);        // TIM2 time of index pulse k
void ufi_flux_stream_consumed(uint32_t samples); // ring buffer: samples the streamer has read
const char* ufi_psram_result(void);              // PSRAM self-test result text

// Board extras v0.5 (ufi_board.c): drive supplies, currents, write lock, board ID, safety
void ufi_board_init(bool power_on);              // false = safe mode (button B at start-up)
void ufi_board_service(void);                   // main loop: overcurrent, USB loss, motor timeout
void ufi_board_activity(void);                  // host command / transfer seen
void ufi_board_power(uint8_t mask);
board_status_t ufi_board_status(void);
bool ufi_board_write_locked(void);              // WRITE-LOCK jumper or session lock: WGATE must stay off
bool ufi_board_jumper_locked(void);             // WRITE-LOCK jumper only (board_status bit 0)
void ufi_board_soft_lock(void);                 // session write lock (op 37, 1.18): engage-only, cleared by MCU reset
bool ufi_board_soft_locked(void);               // v2 write starts refuse with status 22 while engaged
bool ufi_board_has_apple(void);                 // v0.7 board: Apple Disk II port J14
uint16_t ufi_adc_mv(uint32_t channel);          // ufi_power.c, 0 if the ADC is not available
int ufi_sd_info(sd_info_t* info);               // ufi_sd.c
bool ufi_sd_present(void);                      // SD NAND initialised
int ufi_sd_init(void);
uint32_t ufi_sd_blocks(void);                   // 512-byte blocks
int ufi_sd_read(uint8_t* buf, uint32_t lba, uint32_t count);
int ufi_sd_write(const uint8_t* buf, uint32_t lba, uint32_t count);

// Stand-alone dump to the SD NAND (ufi_dump.c) and USB mass storage mode (ufi_usb.c)
int ufi_dump_start(const dump_config_t* cfg);   // NULL = last / default configuration
int ufi_dump_start_fmt(const dump_config_t* cfg, uint8_t fmt);   // fmt 0 SCP, 1 IMG, 2 ADF (1.16)
int ufi_sdwrite_start(const char* name);        // image from the SD NAND onto the disk; NULL = UFI.CFG write_file (1.16)
void ufi_dump_abort(void);
bool ufi_dump_active(void);
dump_status_t ufi_dump_status(void);
void ufi_dump_service(void);                    // main loop
void ufi_buttons_service(void);                 // main loop: A hold = dump, B = abort / MSC
void ufi_config_load(void);                     // UFI.CFG on the SD NAND (start-up)
int ufi_copy_start(void);                       // disk to disk: UFI.CFG copy_from -> copy_to
uint8_t ufi_standalone_drive(void);             // drive for dump + USB floppy (drive_type_t)
bool ufi_usb_msc_active(void);                  // true = no CDC interface (SD or floppy)
int ufi_usb_set_msc(bool on);                   // re-enumerates the USB device

// USB personalities (v0.6): mode switch on J9 (ufi_mode.c), buttons, USB_MSC [mode]
// UFI_USB_GW: Greaseweazle-compatible flux device (UFI.CFG protocol=gw, switch in middle)
enum { UFI_USB_FLUX = 0, UFI_USB_SD = 1, UFI_USB_FLOPPY = 2, UFI_USB_GW = 3, UFI_USB_COMPOSITE = 4 };
int ufi_usb_set_mode(uint8_t mode);
uint8_t ufi_usb_get_mode(void);
uint8_t ufi_usb_flux_mode(void);                // UFI_USB_FLUX or UFI_USB_GW per UFI.CFG
bool ufi_config_protocol_gw(void);              // UFI.CFG protocol=gw
bool ufi_config_protocol_composite(void);
bool ufi_config_apple_sync(void);              // UFI.CFG apple_sync=1: Disk II sync sensor on J19
int ufi_usb_tx_blocking(const uint8_t* p, uint32_t len);

// Greaseweazle protocol (ufi_gw.c)
#define GW_TICK_SHIFT   2                       // GW sample clock = 275 MHz / 4 = 68.75 MHz
#define GW_SAMPLE_FREQ  (FLUX_TIMER_FREQ >> GW_TICK_SHIFT)
void ufi_gw_begin(void);                        // entering GW mode: reset protocol state
void ufi_gw_end(void);
bool ufi_gw_rx(const uint8_t* buf, uint32_t len);   // USB IRQ; false = pause reception
void ufi_gw_clear_comms(void);                  // USB IRQ: SET_LINE_CODING 10000 baud
void ufi_gw_service(void);                      // main loop
void ufi_gw_read_done(int result);              // ufi_stream.c: end of a CMD_READ_FLUX
void ufi_usb_poll(void);                        // main loop: USB stack in floppy mode
void ufi_mode_init(void);                       // start-up: follow the switch
void ufi_mode_service(void);                    // main loop: switch changes, mode blink
void ufi_mode_button_b(void);                   // B >= 2 s: SD drive on/off (switch in middle)

// USB floppy mode (ufi_floppy.c): PC disks in drive A as USB mass storage
void ufi_floppy_begin(void);
int ufi_floppy_end(void);                       // writes back cached tracks
int ufi_floppy_ready(void);                     // disk change, format detection
uint32_t ufi_floppy_blocks(void);
bool ufi_floppy_write_protected(void);
int ufi_floppy_read(uint8_t* buf, uint32_t lba, uint32_t count);
int ufi_floppy_write(const uint8_t* buf, uint32_t lba, uint32_t count);
int ufi_floppy_flush(void);
void ufi_floppy_service(void);                  // main loop: idle write-back

// UFI v2 protocol (ufi_v2.c, docs/USB_Protokoll.md section 3): framed requests in the
// UFI flux personality; a packet starting with 55 AA switches the session to v2
#define UFI_V2_HDR          8u                  // 55 AA type cmd seq status len16
#define UFI_V2_MAX_PAYLOAD  4096u
#define UFI_V2_FRAME_MAX    (UFI_V2_HDR + UFI_V2_MAX_PAYLOAD + 2u)
enum { UFI_V2_REQUEST = 1, UFI_V2_REPLY = 2, UFI_V2_EVENT = 3, UFI_V2_DATA = 4, UFI_V2_END = 5 };
enum { UFI_V2_READ = 0x20, UFI_V2_WRITE = 0x22 };
enum { UFI_V2_EVT_DISK = 0x80, UFI_V2_EVT_BUTTON = 0x81, UFI_V2_EVT_DUMP = 0x82,
       UFI_V2_EVT_POWER = 0x83, UFI_V2_EVT_MODE = 0x84,
       UFI_V2_EVT_READ_PROGRESS = 0x85 };    /* 1.18: READ flag bit4, one per completed revolution (2.3) */
bool ufi_v2_active(void);
bool ufi_v2_write_pending(void);                // a v2 WRITE owns the write path
bool ufi_v2_rx(const uint8_t* buf, uint32_t len);   // USB IRQ; false = pause reception
void ufi_v2_clear_comms(void);                  // USB IRQ: SET_LINE_CODING 10000 baud
void ufi_v2_reset(void);                        // re-enumeration: back to v1
void ufi_v2_service(void);                      // main loop (UFI flux personality)
uint32_t ufi_v2_frame(uint8_t* f, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t status,
                      uint16_t len);            // header + CRC around f[8..8+len), total size
bool ufi_v2_end(uint8_t cmd, int result, const uint8_t* payload, uint16_t len);  // false: not v2
void ufi_v2_event(uint8_t code, const void* payload, uint16_t len);      // queued (mask)
void ufi_v2_event_now(uint8_t code, const void* payload, uint16_t len);  // blocking (mode switch)

// File access on the SD NAND for UFI v2 (ufi_dump.c); names are NUL-terminated
int ufi_file_dir(const char* path, uint16_t start, uint8_t* out, uint16_t max, uint16_t* len);
int ufi_file_read(const char* name, uint32_t offset, uint8_t* out, uint16_t len, uint16_t* got);
int ufi_file_write(const char* name, uint32_t offset, bool create, const uint8_t* data,
                   uint16_t len, uint16_t* written);
int ufi_file_delete(const char* name);

// Streamed capture transfer (ufi_stream.c)
void ufi_stream_begin(void);
void ufi_stream_begin_gw(void);                 // Greaseweazle flux code (ufi_gw.c)
void ufi_stream_begin_v2(uint8_t seq);          // UFI v2 DATA frames (ufi_v2.c)
void ufi_stream_abort(void);
bool ufi_stream_active(void);
void ufi_stream_service(void);                  // main loop: encode + send, READ_DONE at the end

// Laufwerk-Steuerung
int ufi_drive_select(drive_type_t type);
int ufi_drive_motor(bool on);
int ufi_drive_step(int direction);  // +1 = in, -1 = out
int ufi_drive_seek(uint8_t track);
int ufi_drive_seek_q(uint8_t track, uint8_t quarter);   // 1.14: Apple quarter tracks (0-3), else BAD_ARGS
// 3-mode drives (1.14): speed-select line 0 none / 1 DENSITY / 2 DRATE; low_is_360 = asserted -> 360 rpm
void ufi_drive_rpm_config(uint8_t line, bool low_is_360);
uint8_t ufi_drive_rpm_line(void);
int ufi_drive_set_rpm(uint16_t rpm);        // 300 / 360; NOT_IMPL without a configured line
uint16_t ufi_drive_get_rpm(void);           // 0 = not set
int ufi_drive_recalibrate(void);
int ufi_drive_select_side(uint8_t side);
drive_status_t ufi_drive_get_status(void);
drive_type_t ufi_drive_get_current(void);
bool ufi_drive_at_track0(void);
bool ufi_drive_write_protected(void);
bool ufi_drive_disk_changed(void);
bool ufi_drive_ready(void);              // Shugart bus: J6 pin 34 (READY)
bool ufi_drive_is_shugart_bus(void);    // current drive is DS0-DS3 (no DSKCHG line)
int ufi_drive_density_line(bool assert);   // 34-pin pin 2; meaning is drive dependent
int ufi_drive_lines_set(bool density,bool drate);
int ufi_drive_select_native(drive_type_t type);
int ufi_drive_timing_checked(const drive_timing_t* t);
drive_timing_t ufi_drive_get_timing(void);
void ufi_drive_set_timing(const drive_timing_t* t);
int ufi_drive_check_disk(bool* changed, bool* present);
int ufi_drive_amiga_id(uint32_t* id);
bool ufi_drive_motor_is_on(void);
int ufi_drive_probe_tracks(uint8_t* highest);
void ufi_drive_safe_state(void);    // motor off, deselect, all write lines released
int ufi_drive_seek_test(uint8_t a, uint8_t b, uint8_t cycles);
int ufi_drive_apple_step(int direction);    // one track in/out (two half steps)
bool ufi_drive_is_apple(void);              // current drive is on the Apple port J14/J15
bool ufi_drive_quarter_ok(uint8_t quarter); // quarter form allowed: Apple 0..3, Shugart half track (2) with double_step (1.18)
void ufi_flux_select_apple(bool apple);     // capture RDDATA (TIM2_CH3) instead of RDATA

// IEC Bus (C64)
int ufi_iec_reset(void);
bool ufi_iec_reset_active(void);
int ufi_iec_reset_begin(void);
int ufi_iec_reset_step(void);
int ufi_iec_command(uint8_t device, const char* cmd, uint8_t len);   // command channel 15
// drive memory (1.14): M-W (<= 32 bytes), M-R, M-E; wait until the drive answers ATN again
int ufi_iec_mem_write(uint8_t device, uint16_t addr, const uint8_t* data, uint8_t n);
int ufi_iec_mem_read(uint8_t device, uint16_t addr, uint8_t* buf, uint8_t n);
int ufi_iec_mem_exec(uint8_t device, uint16_t addr);
int ufi_iec_wait_ready(uint8_t device, uint32_t timeout_ms);
int ufi_iec_read_status(uint8_t device, char* buffer, uint8_t max_len);   // channel 15, returns length
// 1541 raw GCR chunks (ufi_nib.c, drive code tools/iec_nib.s)
typedef struct {
    uint8_t status;         // 0 ok, 1 no sync / no bytes, 2 origin not found, 3 raw (mode 1)
    uint8_t synclen;        // sync length in ~11 us units
    uint8_t halftrack;      // drive's own position counter after the step
    uint16_t maxlen;        // longest non-sync stretch (bytes)
    uint8_t data[512];
} nib_result_t;
int ufi_nib_chunk(uint8_t dev, uint8_t halftrack, uint8_t sync, uint8_t mode, uint8_t dir,
                  bool force, nib_result_t* r);   // mode 0 chunk at sync, 1 raw, 2 step only
int ufi_nib_upload(uint8_t dev);
void ufi_nib_invalidate(void);
uint8_t ufi_config_iec_device(void);        // UFI.CFG iec_device (default 8)
int ufi_iec_read_block(uint8_t device, uint8_t track, uint8_t sector,
                       uint8_t* buffer, uint16_t* len);              // "#" + U1, 256 bytes
int ufi_iec_write_block(uint8_t device, uint8_t track, uint8_t sector,
                        const uint8_t* data, uint8_t* dos);          // "#" + B-P + U2; dos = DOS error code (1.16)
int ufi_iec_send_byte(uint8_t byte, bool eoi);
int ufi_iec_receive_byte(uint8_t* byte, bool* eoi);
int ufi_iec_atn(bool state);

// USB Kommunikation
int ufi_usb_send_flux(flux_packet_header_t* header, flux_sample_t* data);
int ufi_usb_process_command(void);
int ufi_usb_send_event(uint8_t command, int result);
int ufi_usb_send_read_done(int result, uint8_t revolutions);
bool ufi_usb_tx_idle(void);
int ufi_usb_tx_start(const uint8_t* p, uint32_t len);  // non-blocking; p must stay valid

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
int ufi_write_prepare_compact(uint8_t track, uint8_t side, uint32_t flux_count,
                              uint32_t byte_count, bool verify);
int ufi_write_receive_chunk(uint8_t* data, uint32_t len);
bool ufi_write_data_complete(void);
int ufi_write_start(void);
void ufi_write_index_handler(uint32_t index_time);
void ufi_write_process(void);
int ufi_erase_track(uint8_t track, uint8_t side);
int ufi_erase_track_ms(uint8_t track, uint8_t side, uint32_t ms);   // gate on for ms (1..5000), 1.15
void ufi_write_set_pre_post(uint16_t pre_us, uint16_t post_us);     // gw pre/post_write, 0 = off (1.15)
int ufi_write_verify(void);
write_state_t ufi_write_get_state(void);
uint32_t ufi_write_get_progress(void);
void ufi_write_abort(void);
void ufi_write_set_precomp(bool enable);
void ufi_write_service(void);
int ufi_write_pattern(uint8_t track, uint8_t side, uint16_t interval_ns, uint16_t duration_ms);
int ufi_write_local(uint8_t track, uint8_t side, uint32_t flux_count, bool clamp);  // deltas in the flux store; clamp = tolerate out-of-range periods
int ufi_write_local_extended(uint8_t track,uint8_t side,uint32_t count,uint32_t index_ticks);
int ufi_write_local_checked(uint8_t track,uint8_t side,uint32_t count,bool extended,
                            uint32_t index_ticks,uint64_t expected,uint8_t expected_head,uint8_t* level);
int ufi_write_local_gw(uint8_t track, uint8_t side, uint32_t flux_count,
                       bool cue_index, bool terminate_index, uint16_t pre_us);
void ufi_write_result_payload(uint8_t out[5]);  // WRITE END payload: verify level u8, clamped u32 (1.15)
uint32_t ufi_write_media_token(void);           // media generation the native WRITE was bound to (END block, 1.18)
void ufi_write_set_erase_rest(bool on);         // flags2 bit0 (1.18): gate on, WDATA idle after the last interval until the index
uint32_t ufi_write_erased_ticks(void);          // END payload: length of the erased tail in flux-timer ticks, 0 = none
void ufi_write_set_start_offset(uint32_t ticks);     // flags2 bit1 (1.18): gate opens at index + ticks (native WRITE)
uint32_t ufi_write_start_applied(void);              // END payload: measured gate-open time minus index time, 0 = at the index
void ufi_write_set_weak(int32_t jitter, uint32_t seed);   // flags2 bit3 (4.16): weak-cell markers, jitter ticks (< 0 off), seed 0 = board picks
void ufi_write_set_raw_check(bool on);               // flags2 bit4 (4.17): raw fingerprint read-back (op 32 report), extended path with index_ticks
void ufi_write_weak_result(uint32_t* applied, uint32_t* seed);   // END payload: transitions displaced or dropped, seed of this write
void ufi_write_delayed_start(uint32_t t);            // TIM2 compare channel 4 reached index + start_offset (ufi_flux.c ISR)
void ufi_write_set_speed_check(uint16_t permille);   // op 41 (1.18): tolerance around the source index period, 0 = off
void ufi_write_speed_state(uint16_t* permille, uint32_t* measured, uint32_t* source);  // op 41 reply: last check
uint32_t ufi_write_precomp_select(uint8_t track);       // 1.18: override / profile zone / TIMING+rule, physical track
void ufi_write_set_precomp_ns(int32_t ns);              // flags2 bit2: 0..2000 for this write, < 0 = no override
int ufi_write_precomp_profile_set(const uint8_t* zones, uint8_t n);   // op 45: n x (from_phys u8, ns u16), 0 clears
uint16_t ufi_write_precomp_profile(uint8_t* out);       // op 45 reply, 19 bytes
uint32_t ufi_capture_media_token(void);         // media generation the capture was bound to (ufi_main.c, END block)
#define UFI_V2_GEN_BLOCK 17u                    // END generation block: schema u8, config/media start/media end/capture id u32
uint16_t ufi_v2_generations(uint8_t* out, uint32_t media_start, uint32_t capture_id);  // ufi_v2.c, writes UFI_V2_GEN_BLOCK bytes
void ufi_v2_last_refusal(uint8_t out[6]);       // op 39 (4.8): schema 1, cmd, seq, status, field, detail of the last 16/21 refusal
void ufi_write_tim3_irq(void);
void ufi_write_dma_irq(void);

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

/* Drive diagnostics and index simulation (ufi_diag.c, firmware 1.13) */
typedef struct {
    uint8_t revs;           // revolutions measured
    uint32_t period_min;    // revolution time in FLUX_TIMER_FREQ ticks
    uint32_t period_avg;
    uint32_t period_max;
    uint32_t pulse_avg;     // index pulse width in ticks
    uint16_t rpm_x100;      // rpm * 100 from period_avg
} diag_rpm_t;

#define DIAG_TRACK0     0x01u   // recalibration found TRK0: drive answers
#define DIAG_INDEX      0x02u   // index pulses: disk inserted and spinning
#define DIAG_WPROT      0x04u   // write protect asserted

int ufi_diag_rpm(uint8_t revs, diag_rpm_t* r);

// FORMAT (1.14, ufi_format.c): the firmware encodes and writes one MFM / Amiga track
#define FORMAT_VERIFY       0x01u
#define FORMAT_AMIGA        0x02u
#define FORMAT_HEAD_SWAP    0x04u   // Commodore 1581: ID side = 1 - physical head
typedef struct __packed {
    uint8_t track, side;
    uint8_t flags;              // FORMAT_*
    uint16_t rate_kbps;         // 250, 300, 500, 1000
    uint8_t spt;                // 512-byte sectors per track (Amiga: 11, HD 22)
    uint8_t gap3;               // 0 = by layout
    uint8_t fill;               // data byte
} format_req_t;
int ufi_format_track(const format_req_t* q, uint32_t* flux_count, uint8_t* good);  // good 0xFF = not verified
int ufi_format_revolution(uint32_t* ticks);     // measured revolution of the selected drive (cached 60 s)

// SELFTEST (1.14, ufi_selftest.c): items of (id u8, status u8, value u32), also SELFTEST.LOG
uint8_t ufi_selftest(uint8_t* out, uint8_t max_items);
uint8_t ufi_selftest_selected(uint8_t* out,uint8_t max_items,uint16_t mask);
uint8_t ufi_iec_loopback(void);         // ufi_iec.c: bit0-3 ATN/CLK/DATA/SRQ not asserted, bit4-7 not released

// fault record / reset cause (1.14, ufi_crash.c)
void ufi_crash_fault(const uint32_t* frame, uint32_t kind);   // from the fault handlers
void ufi_crash_boot(void);              // start-up: CRASH.LOG, reset cause text
const char* ufi_crash_last(void);       // "reset: power-on" / "reset: HardFault pc=..."
uint8_t ufi_board_rev(void);            // 5/6/7 from the BOARD_ID divider, 0 = unknown (ufi_v2.c)
uint8_t ufi_crash_kind(void);           // 0 = no fault record at this start-up
void ufi_stack_paint(void);             // first thing in ufi_init(): paint the free stack
uint32_t ufi_stack_free(void);          // bytes of stack never used so far
int ufi_diag_scan(bool shugart_bus, uint8_t* out, uint8_t* n);   // out: 4 x (type, flags)
#define INDEX_SIM_PIN       0x01u   // pulses on J9 pin 6 (PE1, open drain)
#define INDEX_SIM_INTERNAL  0x02u   // pulses replace the drive's INDEX line in capture/write
int ufi_index_sim_set(uint16_t rpm, uint16_t pulse_us, uint8_t mode);   // rpm 0 = off, mode 0 = PIN
uint16_t ufi_index_sim_rpm(void);
uint8_t ufi_index_sim_mode(void);
bool ufi_index_sim_pulse(void);         // simulated index currently asserted
uint16_t ufi_config_index_sim(void);    // UFI.CFG index_sim=300|360 (0 = off)
uint8_t ufi_config_index_sim_mode(void);    // UFI.CFG index_sim_mode=pin|internal|both
void ufi_flux_index_event(uint32_t t);  // ufi_flux.c: one index pulse at flux-timer time t
void ufi_flux_hard_sectors(uint8_t n);  // 0 = soft-sectored; n sector holes + 1 index hole (1.14)
void ufi_flux_index_source(bool internal);
bool ufi_flux_index_asserted(void);     // INDEX line, or the simulation when internal

/* ============================================================================
 * INTERRUPT HANDLER
 * ============================================================================ */

// Flux-Timer: Index-Capture CH2 (höchste Priorität!)
void TIM2_IRQHandler(void);

// DMA Transfer Complete (Flux-Store voll)
void DMA1_Stream0_IRQHandler(void);

// USB High-Speed
void OTG_HS_IRQHandler(void);

/* ============================================================================
 * SYSTEM FUNCTIONS
 * ============================================================================ */

// Clock Konfiguration (ufi_clock.c)
void SystemClock_Config(void);
void Error_Handler(void);

#endif // UFI_FIRMWARE_H
