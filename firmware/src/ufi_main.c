/**
 * UFI Flux Engine - STM32H723 Firmware
 * Main Implementation: init, capture orchestration, main loop.
 *
 * Board pins: board.h / board_headless.c; drive control: ufi_drive.c;
 * IEC bus: ufi_iec.c; flux engine: ufi_flux.c.
 */

#include "ufi_firmware.h"
#include "ufi_analysis_job.h"
#include "ufi_evidence.h"
#include "ufi_media.h"

/* ============================================================================
 * GLOBALE VARIABLEN (extern referenziert in ufi_flux.c, ufi_write.c)
 * ============================================================================ */

capture_context_t g_capture;
static uint32_t capture_media_token;    /* media generation the running/last capture was bound to */

uint32_t ufi_capture_media_token(void)
{
    return capture_media_token;
}

/* Single main-loop owner of the shared drive/store. ISR callbacks only publish state. */
bool ufi_operation_busy(void)
{
    return ufi_analysis_job_active() || ufi_survey_active() || ufi_dump_active() || ufi_stream_active() || ufi_iec_reset_active() ||
           g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING ||
           ufi_write_get_state() != WRITE_IDLE;
}

/* ============================================================================
 * SYSTEM-BOOTLOADER (USB-DFU im ROM)
 * ============================================================================ */

/* STM32H72x/H73x system memory bootloader (ST AN2606) */
#define SYSTEM_BOOTLOADER_ADDR  0x1FF09800UL
#define BOOTLOADER_MAGIC        0xB007DF00UL

/* DTCM is not cached and not cleared by the startup code (.dtcm is NOLOAD), so the
 * value survives NVIC_SystemReset() */
__attribute__((section(".dtcm"))) static volatile uint32_t g_boot_magic;

void ufi_request_bootloader(void)
{
    g_boot_magic = BOOTLOADER_MAGIC;
    __DSB();
    NVIC_SystemReset();
}

/* Called first thing in main(): jump to the ROM bootloader if it was requested */
static void ufi_check_bootloader(void)
{
    if (g_boot_magic != BOOTLOADER_MAGIC) {
        return;
    }
    g_boot_magic = 0;

    __disable_irq();
    SysTick->CTRL = 0;
    SCB_DisableICache();
    SCB_DisableDCache();
    for (uint32_t i = 0; i < 8; i++) {          /* all NVIC lines off and cleared */
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    SCB->VTOR = SYSTEM_BOOTLOADER_ADDR;
    __set_MSP(*(volatile uint32_t*)SYSTEM_BOOTLOADER_ADDR);
    __enable_irq();
    void (*boot)(void) = (void (*)(void))(*(volatile uint32_t*)(SYSTEM_BOOTLOADER_ADDR + 4u));
    boot();
    while (1) {
    }
}

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_init(void)
{
    ufi_stack_paint();          /* stack high-water mark (SELFTEST stack_free, 1.15) */
    HAL_Init();
    SystemClock_Config();       /* 550 MHz */
    UFI_DWT_INIT();             /* cycle counter for us delays (Fix #6) */
    UFI_WATCHDOG_INIT();        /* Fix #9 */

    board_gpio_init();          /* all pins in their released / idle state */
    ufi_drive_init();
    ufi_iec_init();
    ufi_flux_init();            /* TIM2 + DMA, PSRAM self-test */
    ufi_write_init();
    /* Buttons held at power-up (v0.5): A = ROM USB-DFU bootloader (recovery even when the
     * USB firmware is broken), B = safe mode: drive supplies stay off (BOARD_STATUS bit6) */
    HAL_Delay(5);               /* button pull-ups settle */
    if (HAL_GPIO_ReadPin(PIN_BTN_A.port, PIN_BTN_A.pin) == GPIO_PIN_RESET) {
        ufi_request_bootloader();
    }
    const bool safe_mode = HAL_GPIO_ReadPin(PIN_BTN_B.port, PIN_BTN_B.pin) == GPIO_PIN_RESET;
    ufi_board_init(!safe_mode); /* drive supplies on (5 V, then 12 V) unless safe mode */
    if (safe_mode) {
        led_set(&PIN_LED_ERR, true);
    }
    if (ufi_board_rev() >= 10u) {
        ufi_clock_rtc_init();   /* v1.0 (A9c): LSE 32.768 kHz + RTC on the CR2032; v0.x has no LSE crystal */
    }
    ufi_usb_init();
    ufi_sd_init();              /* v0.6 SD NAND; failure shows in SD_INFO / dump error 1 */
    ufi_config_load();          /* UFI.CFG: stand-alone drive (a/b/amiga/ds0-ds3), dump settings */
    ufi_crash_boot();           /* reset cause; fault record from DTCM -> CRASH.LOG (1.14) */
    ufi_mode_init();            /* mode switch on J13: flux / USB floppy / SD drive */

    /* PSRAM fitted but failed its self-test: 3 ERR blinks (flux store falls back to SRAM,
     * GET_INFO reports the result) */
    if (BOARD_HAS_PSRAM && !ufi_flux_store_is_psram()) {
        for (int i = 0; i < 3; i++) {
            led_set(&PIN_LED_ERR, true);
            HAL_Delay(150);
            led_set(&PIN_LED_ERR, false);
            HAL_Delay(150);
        }
    }

    g_capture.state = CAPTURE_IDLE;
}

/* ============================================================================
 * FLUX CAPTURE
 * ============================================================================ */

int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks)
{
    return ufi_capture_start_q(track, side, revolutions, period_ticks, 0);
}

int ufi_capture_start_q(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks,
                        uint8_t quarter)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING ||
        ufi_stream_active() || (ufi_write_get_state() != WRITE_IDLE &&
                               ufi_write_get_state() != WRITE_VERIFYING)) {
        return UFI_ERR_BUSY;
    }
    if (ufi_drive_get_current() == DRIVE_NONE) {
        return UFI_ERR_NO_DRIVE;
    }
    uint32_t media_token=ufi_drive_media_token();
    const int sret = ufi_drive_seek_q(track, quarter);
    if (sret != 0) {
        return sret == UFI_ERR_BAD_ARGS ? UFI_ERR_BAD_ARGS : UFI_ERR_SEEK_FAIL;
    }
    int side_ret=ufi_drive_select_side(side);if(side_ret)return side_ret;
    if(!ufi_drive_media_matches(media_token))return UFI_ERR_MEDIA_CHANGED;
    capture_media_token=media_token;        /* READ END generation block (4.5) */

    g_capture.current_track = track;
    g_capture.current_side = side;
    g_capture.current_quarter = quarter ? (uint8_t)(0x80u | (quarter << 4)) : 0u;   /* analysis entry points refine this */

    /* Apple Disk II: RDDATA input; without a sync sensor (J19, UFI.CFG apple_sync=1) there
     * is no index pulse -> revolutions are 200 ms slices */
    const bool apple = ufi_drive_is_apple();
    ufi_flux_select_apple(apple);
    if (apple && period_ticks == 0 && !ufi_config_apple_sync()) {
        period_ticks = FLUX_TIMER_FREQ / 5u;
    }

    ufi_evidence_begin(track,side,revolutions,period_ticks);
    int ret = ufi_flux_capture_start(revolutions, period_ticks);
    if(ret)ufi_evidence_finish();
    if (ret == UFI_OK) {
        led_set(&PIN_LED_ERR, false);
        led_set(&PIN_LED_FDD, true);
    }
    return ret;
}

int ufi_capture_abort(void)
{
    ufi_stream_abort();
    ufi_flux_capture_stop();
    led_set(&PIN_LED_FDD, false);
    return UFI_OK;
}

capture_state_t ufi_capture_get_state(void)
{
    capture_state_t state=ufi_flux_poll();
    ufi_evidence_service();return state;
}

flux_revolution_t* ufi_capture_get_data(uint8_t revolution)
{
    return ufi_flux_get_revolution(revolution);
}

/* ============================================================================
 * HAUPTSCHLEIFE
 * ============================================================================ */

static void send_capture(void)
{
    const uint8_t n = ufi_flux_get_revolution_count();
    for (uint8_t i = 0; i < n; i++) {
        flux_revolution_t* rev = ufi_flux_get_revolution(i);
        flux_packet_header_t header = {
            .track = g_capture.current_track,
            .side = g_capture.current_side,
            .revolution = i,
            .flags = (uint8_t)(0x01 | (g_capture.error_code == 1 ? 0x02 : 0x00)),
            .index_time = rev->index_time,
            .sample_count = rev->count,
        };
        if (ufi_usb_send_flux(&header, rev->samples) != UFI_OK) {
            return;                     /* host gone: no READ_DONE either */
        }
    }
    ufi_usb_send_read_done(g_capture.error_code == 1 ? UFI_ERR_BUFFER_FULL : UFI_OK, n);
}

void ufi_main_loop(void)
{
    uint32_t last_blink = 0;

    while (1) {
        UFI_WATCHDOG_FEED();

        ufi_usb_process_command();
        ufi_write_process();
        ufi_write_service();
        ufi_sched_service();            /* head steps on a schedule during read / write */
        ufi_board_service();            /* overcurrent, USB loss, motor timeout */

        ufi_buttons_service();          /* A hold = dump, B = abort / USB mass storage */
        ufi_mode_service();             /* mode switch, mode blink code */
        ufi_usb_poll();                 /* USB floppy mode: USB stack runs here */
        ufi_floppy_service();           /* USB floppy mode: write back idle tracks */

        const capture_state_t cs = ufi_capture_get_state();   /* also runs the index timeout */
        if (ufi_analysis_job_active()) {
            ufi_analysis_job_service();
        } else if (ufi_survey_active()) {
            ufi_survey_service();           /* track survey (op 46): one revolution per track */
        } else if (ufi_dump_active()) {
            ufi_dump_service();             /* stand-alone dump owns the capture */
        } else if (ufi_stream_active()) {
            ufi_stream_service();           /* READ_TRACK: sends while the disk turns */
        } else switch (cs) {
            case CAPTURE_COMPLETE:
                send_capture();
                g_capture.state = CAPTURE_IDLE;
                led_set(&PIN_LED_FDD, false);
                break;
            case CAPTURE_ERROR:
                ufi_usb_send_read_done(g_capture.error_code == 4 ? UFI_ERR_MEDIA_CHANGED : g_capture.error_code == 2 ? UFI_ERR_DMA :
                                       g_capture.error_code == 3 ? UFI_ERR_NO_INDEX : UFI_ERR_BUFFER_FULL, 0);
                g_capture.state = CAPTURE_IDLE;
                led_set(&PIN_LED_FDD, false);
                led_set(&PIN_LED_ERR, true);
                break;
            default:
                break;
        }

        /* Activity LED blinks while reading or writing (Fix #12) */
        if (g_capture.state == CAPTURE_RUNNING || ufi_write_get_state() == WRITE_ACTIVE) {
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

int main(void)
{
    ufi_check_bootloader();
    ufi_init();
    ufi_main_loop();
    return 0;
}
