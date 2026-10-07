/**
 * UFI Flux Engine - board v0.5 extras and safety
 *
 * Drive supplies: FDD_5V / FDD_12V are switched by P-FETs (PE2/PE3 high = on; the 100k
 * pull-downs keep them off while the MCU is in reset) and measured through 0.1R shunts by
 * INA180A1 amplifiers (20 V/V -> 2 mV per mA) on ADC1.  A rail drawing more than
 * OVERCURRENT_MA for OVERCURRENT_SAMPLES samples in a row is switched off and flagged
 * until the host switches it on again (BOARD_STATUS).
 *
 * Safety: USB unconfigured/suspended for USB_LOST_MS (cable pulled, host asleep) ->
 * transfers aborted, motor off, drive deselected, write lines released.  A motor that
 * stays idle for drive_timing_t.motor_off_s is switched off.
 *
 * WRITE LOCK jumper (J11): blocks WGATE in hardware (74LVC1G32), WLOCK lets the firmware
 * refuse writes with UFI_ERR_WRITE_PROT instead of writing into the void.
 */

#include "ufi_firmware.h"
#include "usbd_core.h"

extern USBD_HandleTypeDef hUsbDevice;
extern capture_context_t g_capture;

#define SAMPLE_MS               10u
#define OVERCURRENT_MA          1500u   /* polyfuses hold 1.1 A; INA180 saturates ~1.6 A */
#define OVERCURRENT_SAMPLES     5u      /* 50 ms: motor start-up peaks pass */
#define USB_LOST_MS             300u
#define RAIL_STAGGER_MS         50u     /* 5 V first, then 12 V */

static uint8_t power_mask;
static bool safe_mode;                  /* button B at start-up: supplies stay off */
static uint8_t trip_mask;
static uint8_t oc_count[2];
static uint16_t rail_ma[2];
static uint32_t last_sample_ms, last_activity_ms, usb_ok_ms;
static bool usb_seen, usb_lost;

static const gpio_pin_t* const rail_en[2] = {&PIN_FDD5_EN, &PIN_FDD12_EN};
static const uint32_t rail_adc[2] = {ADC_CH_I_FDD5, ADC_CH_I_FDD12};

/* ============================================================================
 * DRIVE SUPPLIES
 * ============================================================================ */

void ufi_board_power(uint8_t mask)
{
    mask &= 0x03u;
    trip_mask &= (uint8_t)~mask;            /* switching a rail on clears its trip */
    for (int r = 0; r < 2; r++) {
        const bool on = (mask >> r) & 1u;
        HAL_GPIO_WritePin(rail_en[r]->port, rail_en[r]->pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
        oc_count[r] = 0;
    }
    power_mask = mask;
}

void ufi_board_init(bool power_on)
{
    last_activity_ms = HAL_GetTick();
    safe_mode = !power_on;
    if (safe_mode) {
        ufi_board_power(0x00);              /* e.g. a shorted drive: host switches on */
        return;
    }
    ufi_board_power(0x01);
    HAL_Delay(RAIL_STAGGER_MS);
    ufi_board_power(0x03);
}

static void sample_rails(void)
{
    for (int r = 0; r < 2; r++) {
        rail_ma[r] = (uint16_t)(ufi_adc_mv(rail_adc[r]) / 2u);
        if (!((power_mask >> r) & 1u)) {
            oc_count[r] = 0;
            continue;
        }
        if (rail_ma[r] > OVERCURRENT_MA) {
            if (++oc_count[r] >= OVERCURRENT_SAMPLES) {
                HAL_GPIO_WritePin(rail_en[r]->port, rail_en[r]->pin, GPIO_PIN_RESET);
                power_mask &= (uint8_t)~(1u << r);
                trip_mask |= (uint8_t)(1u << r);
                led_set(&PIN_LED_ERR, true);
                const board_status_t s = ufi_board_status();
                ufi_v2_event(UFI_V2_EVT_POWER, &s, sizeof(s));
            }
        } else {
            oc_count[r] = 0;
        }
    }
}

/* ============================================================================
 * STATUS
 * ============================================================================ */

bool ufi_board_write_locked(void)
{
    return HAL_GPIO_ReadPin(PIN_WLOCK.port, PIN_WLOCK.pin) == GPIO_PIN_SET;
}

board_status_t ufi_board_status(void)
{
    sample_rails();
    board_status_t s = {0};
    s.power = power_mask;
    s.flags = (uint8_t)((ufi_board_write_locked() ? 0x01u : 0u) | (uint8_t)(trip_mask << 1) |
                        (ufi_sd_present() ? 0x08u : 0u) |
                        (HAL_GPIO_ReadPin(PIN_BTN_A.port, PIN_BTN_A.pin) == GPIO_PIN_RESET ? 0x10u : 0u) |
                        (HAL_GPIO_ReadPin(PIN_BTN_B.port, PIN_BTN_B.pin) == GPIO_PIN_RESET ? 0x20u : 0u) |
                        (safe_mode ? 0x40u : 0u));
    s.i5_ma = rail_ma[0];
    s.i12_ma = rail_ma[1];
    s.board_id_mv = ufi_adc_mv(ADC_CH_BOARD_ID);
    return s;
}

/* v0.7: Apple Disk II port fitted (board ID divider 10k/2.2k); read once */
bool ufi_board_has_apple(void)
{
    static int8_t has = -1;
    if (has < 0) {
        const uint16_t id = ufi_adc_mv(ADC_CH_BOARD_ID);
        has = (id + 200u > BOARD_ID_V07_MV && id < BOARD_ID_V07_MV + 200u) ? 1 : 0;
    }
    return has == 1;
}

/* ============================================================================
 * SAFETY SERVICE (main loop)
 * ============================================================================ */

void ufi_board_activity(void)
{
    last_activity_ms = HAL_GetTick();
}

void ufi_board_service(void)
{
    const uint32_t now = HAL_GetTick();

    /* USB gone: leave the drive in a state that cannot harm the disk */
    if (hUsbDevice.dev_state == USBD_STATE_CONFIGURED) {
        usb_seen = true;
        usb_lost = false;
        usb_ok_ms = now;
    } else if (usb_seen && !usb_lost && now - usb_ok_ms > USB_LOST_MS) {
        usb_lost = true;
        if (ufi_dump_active()) {
            return;                             /* stand-alone dump: USB not needed */
        }
        ufi_capture_abort();
        ufi_write_abort();
        ufi_drive_safe_state();
    }

    /* transfers count as activity; idle motor goes off after motor_off_s */
    const capture_state_t cs = *(volatile capture_state_t*)&g_capture.state;
    if (cs == CAPTURE_WAITING_INDEX || cs == CAPTURE_RUNNING || ufi_stream_active() ||
        ufi_write_get_state() != WRITE_IDLE) {
        last_activity_ms = now;
    }
    const uint16_t off_s = ufi_drive_get_timing().motor_off_s;
    if (off_s && ufi_drive_motor_is_on() && now - last_activity_ms > (uint32_t)off_s * 1000u) {
        ufi_drive_motor(false);
    }

    if (now - last_sample_ms >= SAMPLE_MS) {
        last_sample_ms = now;
        sample_rails();
    }
}
