/**
 * UFI Flux Engine - STM32H723 Firmware
 * Main Implementation: init, capture orchestration, main loop.
 *
 * Board pins: board.h / board_headless.c; drive control: ufi_drive.c;
 * IEC bus: ufi_iec.c; flux engine: ufi_flux.c.
 */

#include "ufi_firmware.h"

/* ============================================================================
 * GLOBALE VARIABLEN (extern referenziert in ufi_flux.c, ufi_write.c)
 * ============================================================================ */

capture_context_t g_capture;

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
    HAL_Init();
    SystemClock_Config();       /* 550 MHz */
    UFI_DWT_INIT();             /* cycle counter for us delays (Fix #6) */
    UFI_WATCHDOG_INIT();        /* Fix #9 */

    board_gpio_init();          /* all pins in their released / idle state */
    ufi_drive_init();
    ufi_iec_init();
    ufi_flux_init();            /* TIM2 + DMA */
    ufi_write_init();
    ufi_usb_init();

    g_capture.state = CAPTURE_IDLE;
}

/* ============================================================================
 * FLUX CAPTURE
 * ============================================================================ */

int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (ufi_drive_get_current() == DRIVE_NONE) {
        return UFI_ERR_NO_DRIVE;
    }
    if (ufi_drive_seek(track) != 0) {
        return UFI_ERR_SEEK_FAIL;
    }
    ufi_drive_select_side(side);

    g_capture.current_track = track;
    g_capture.current_side = side;

    int ret = ufi_flux_capture_start(revolutions);
    if (ret == UFI_OK) {
        led_set(&PIN_LED_ERR, false);
        led_set(&PIN_LED_FDD, true);
    }
    return ret;
}

int ufi_capture_abort(void)
{
    ufi_flux_capture_stop();
    led_set(&PIN_LED_FDD, false);
    return UFI_OK;
}

capture_state_t ufi_capture_get_state(void)
{
    return ufi_flux_poll();
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
        ufi_usb_send_flux(&header, rev->samples);
    }
}

void ufi_main_loop(void)
{
    uint32_t last_blink = 0;

    while (1) {
        UFI_WATCHDOG_FEED();

        ufi_usb_process_command();
        ufi_write_process();
        ufi_write_service();

        switch (ufi_capture_get_state()) {
            case CAPTURE_COMPLETE:
                send_capture();
                g_capture.state = CAPTURE_IDLE;
                led_set(&PIN_LED_FDD, false);
                break;
            case CAPTURE_ERROR:
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
