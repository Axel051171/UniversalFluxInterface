/**
 * UFI Flux Engine - board self-test (UFI v2 SELFTEST 0x07, firmware 1.14)
 *
 * Bring-up aid for a freshly built board: every item answers (id, status, value) and
 * the same table goes to SELFTEST.LOG on the SD NAND.  Status 0 ok, 1 fail, 2 not
 * testable here (skipped), 3 warning.  Runs about half a second; the drive lines are only
 * read, the IEC transceivers are driven in a loopback, the index simulation is pulsed on
 * J9 pin 6 (a wire to the INDEX input of J6 shows the whole index path).
 */

#include "ufi_firmware.h"
#include <stdio.h>
#include <string.h>

extern capture_context_t g_capture;
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

enum {
    ST_OK = 0, ST_FAIL = 1, ST_SKIP = 2, ST_WARN = 3,
};

typedef struct {
    uint8_t id;
    uint8_t status;
    uint32_t value;
} st_item_t;

static const char* const item_names[] = {
    "", "timer", "usb_sof", "psram", "sd", "rail_5v", "rail_12v", "board_id", "usbc_cc",
    "fdd_lines", "iec_loop", "index_loop", "last_reset", "log",
};

/* ---- single items ------------------------------------------------------- */

/* TIM2 (275 MHz) against the SysTick millisecond: catches a wrong PLL / prescaler */
static void t_timer(st_item_t* it)
{
    const uint32_t t0 = TIM2->CNT;
    const uint32_t m0 = HAL_GetTick();
    while (HAL_GetTick() - m0 < 20u) {
    }
    const uint32_t per_ms = (TIM2->CNT - t0) / 20u;
    it->value = per_ms;
    const uint32_t nominal = FLUX_TIMER_FREQ / 1000u;
    it->status = (per_ms > nominal - nominal / 50u && per_ms < nominal + nominal / 50u) ? ST_OK : ST_FAIL;
}

/* crystal accuracy: TIM2 against 128 USB start-of-frame pulses (1 kHz from the host) */
static void t_usb_sof(st_item_t* it)
{
    USB_OTG_DeviceTypeDef* dev =
        (USB_OTG_DeviceTypeDef*)((uint32_t)hpcd_USB_OTG_HS.Instance + USB_OTG_DEVICE_BASE);
    uint32_t fn = (dev->DSTS & USB_OTG_DSTS_FNSOF) >> 8;
    const uint32_t m0 = HAL_GetTick();
    while (((dev->DSTS & USB_OTG_DSTS_FNSOF) >> 8) == fn) {
        if (HAL_GetTick() - m0 > 20u) {
            it->status = ST_SKIP;           /* no host frames (not enumerated) */
            return;
        }
    }
    const uint32_t t0 = TIM2->CNT;
    fn = (dev->DSTS & USB_OTG_DSTS_FNSOF) >> 8;
    uint32_t frames = 0;
    while (frames < 128u) {
        const uint32_t f = (dev->DSTS & USB_OTG_DSTS_FNSOF) >> 8;
        if (f != fn) {
            frames += (f - fn) & 0x3FFFu;
            fn = f;
        }
        if (HAL_GetTick() - m0 > 400u) {
            it->status = ST_SKIP;
            return;
        }
    }
    const uint32_t ticks = TIM2->CNT - t0;
    const int64_t nominal = (int64_t)frames * (FLUX_TIMER_FREQ / 1000u);
    const int32_t ppm = (int32_t)(((int64_t)ticks - nominal) * 1000000 / nominal);
    it->value = (uint32_t)ppm;
    it->status = (ppm > -500 && ppm < 500) ? ST_OK : ST_WARN;
}

/* PSRAM: boot result plus a fresh pattern over the top 64 KB of the flux store */
static void t_psram(st_item_t* it)
{
    uint32_t words = 0;
    uint32_t* store = ufi_flux_store(&words);
    it->value = words * 4u;
    if (!ufi_flux_store_is_psram()) {
        it->status = BOARD_HAS_PSRAM ? ST_FAIL : ST_SKIP;
        return;
    }
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING ||
        ufi_write_get_state() == WRITE_ACTIVE || words < 16384u) {
        it->status = ST_SKIP;
        return;
    }
    volatile uint32_t* p = store + words - 16384u;
    for (uint32_t i = 0; i < 16384u; i++) {
        p[i] = (i * 0x9E3779B9u) ^ 0xA5A5A5A5u;
    }
    SCB_CleanInvalidateDCache();
    for (uint32_t i = 0; i < 16384u; i++) {
        if (p[i] != ((i * 0x9E3779B9u) ^ 0xA5A5A5A5u)) {
            it->status = ST_FAIL;
            it->value = i * 4u;
            return;
        }
    }
    it->status = ST_OK;
}

static void t_sd(st_item_t* it)
{
    sd_info_t info;
    memset(&info, 0, sizeof(info));
    if (ufi_sd_info(&info) != UFI_OK || !info.present) {
        it->status = ST_FAIL;
        return;
    }
    it->value = info.capacity_mb;
    it->status = ST_OK;
}

static void t_rail(st_item_t* it, int rail)
{
    const board_status_t b = ufi_board_status();
    it->value = rail ? b.i12_ma : b.i5_ma;
    if (b.flags & (rail ? 0x04u : 0x02u)) {
        it->status = ST_FAIL;               /* overcurrent trip */
    } else if (!((b.power >> rail) & 1u)) {
        it->status = ST_SKIP;               /* rail off (safe mode) */
    } else {
        it->status = ST_OK;
    }
}

static void t_board_id(st_item_t* it)
{
    const uint16_t mv = ufi_adc_mv(ADC_CH_BOARD_ID);
    it->value = mv;
    const bool known = (mv + 200u > BOARD_ID_V05_MV && mv < BOARD_ID_V05_MV + 200u) ||
                       (mv + 200u > BOARD_ID_V06_MV && mv < BOARD_ID_V06_MV + 200u) ||
                       (mv + 200u > BOARD_ID_V07_MV && mv < BOARD_ID_V07_MV + 200u);
    it->status = known ? ST_OK : ST_WARN;
}

static void t_usbc(st_item_t* it)
{
    usb_power_t p;
    if (ufi_usb_power(&p) != UFI_OK) {
        it->status = ST_SKIP;
        return;
    }
    it->value = p.current_ma;
    it->status = p.current_ma ? ST_OK : ST_WARN;    /* no CC: USB-A cable, 500 mA default */
}

/* drive bus inputs with nothing selected: pull-ups must read high (bit set = low) */
static void t_fdd_lines(st_item_t* it)
{
    if (ufi_drive_get_current() != DRIVE_NONE) {
        it->status = ST_SKIP;
        return;
    }
    const gpio_pin_t* pins[] = {&PIN_FDD_INDEX, &PIN_FDD_TRACK0, &PIN_FDD_WPROT, &PIN_FDD_DKCHG,
                                &PIN_FDD_READY, &PIN_FDD_RDATA};
    uint32_t low = 0;
    for (uint32_t i = 0; i < 6u; i++) {
        if (HAL_GPIO_ReadPin(pins[i]->port, pins[i]->pin) == GPIO_PIN_RESET) {
            low |= 1u << i;
        }
    }
    it->value = low;
    it->status = low ? ST_WARN : ST_OK;     /* a connected drive may hold TRK0 etc. low */
}

static void t_iec_loop(st_item_t* it)
{
    const uint8_t m = ufi_iec_loopback();
    it->value = m;
    it->status = (m & 0x0Fu) ? ST_FAIL : (m & 0xF0u) ? ST_WARN : ST_OK;
}

/* index simulation on J9 pin 6 wired to INDEX (J6 pin 8): the pulse must arrive */
static void t_index_loop(st_item_t* it)
{
    if (ufi_drive_get_current() != DRIVE_NONE) {
        it->status = ST_SKIP;
        return;
    }
    const uint16_t prev_rpm = ufi_index_sim_rpm();
    const uint8_t prev_mode = ufi_index_sim_mode();
    if (ufi_index_sim_set(300, 2000, INDEX_SIM_PIN) != UFI_OK) {
        it->status = ST_SKIP;               /* mode switch on J13 uses the pin */
        return;
    }
    const uint32_t m0 = HAL_GetTick();
    bool seen = false;
    while (HAL_GetTick() - m0 < 250u) {
        if (HAL_GPIO_ReadPin(PIN_FDD_INDEX.port, PIN_FDD_INDEX.pin) == GPIO_PIN_RESET) {
            seen = true;
            break;
        }
    }
    ufi_index_sim_set(prev_rpm, 0, prev_mode);
    it->value = seen ? 1u : 0u;
    it->status = seen ? ST_OK : ST_SKIP;    /* no wire: nothing to judge */
}

static void t_last_reset(st_item_t* it)
{
    it->value = ufi_crash_kind();
    it->status = it->value ? ST_WARN : ST_OK;
}

/* ---- the run ---------------------------------------------------------- */

uint8_t ufi_selftest(uint8_t* out, uint8_t max_items)
{
    st_item_t items[14];
    uint8_t n = 0;
    memset(items, 0, sizeof(items));
#define ITEM(k, call) do { items[n].id = (k); call; n++; } while (0)
    ITEM(1, t_timer(&items[n]));
    ITEM(2, t_usb_sof(&items[n]));
    ITEM(3, t_psram(&items[n]));
    ITEM(4, t_sd(&items[n]));
    ITEM(5, t_rail(&items[n], 0));
    ITEM(6, t_rail(&items[n], 1));
    ITEM(7, t_board_id(&items[n]));
    ITEM(8, t_usbc(&items[n]));
    ITEM(9, t_fdd_lines(&items[n]));
    ITEM(10, t_iec_loop(&items[n]));
    ITEM(11, t_index_loop(&items[n]));
    ITEM(12, t_last_reset(&items[n]));
#undef ITEM

    /* SELFTEST.LOG: the table as text; its result is the last item */
    char text[640];
    int len = snprintf(text, sizeof(text), "UFI selftest firmware %s\r\n", UFI_FW_VERSION);
    for (uint8_t i = 0; i < n && len > 0 && len < (int)sizeof(text) - 40; i++) {
        len += snprintf(text + len, sizeof(text) - (size_t)len, "%-11s %s %lu\r\n",
                        item_names[items[i].id],
                        items[i].status == ST_OK ? "ok  " : items[i].status == ST_FAIL ? "FAIL" :
                        items[i].status == ST_SKIP ? "skip" : "warn",
                        (unsigned long)items[i].value);
    }
    uint16_t written = 0;
    const int wr = (len > 0) ? ufi_file_write("SELFTEST.LOG", 0, true, (const uint8_t*)text,
                                               (uint16_t)len, &written) : UFI_ERR_STORAGE;
    items[n].id = 13;
    items[n].status = (wr == UFI_OK) ? ST_OK : ST_FAIL;
    items[n].value = written;
    n++;

    uint8_t k = 0;
    for (; k < n && k < max_items; k++) {
        out[k * 6u] = items[k].id;
        out[k * 6u + 1u] = items[k].status;
        memcpy(&out[k * 6u + 2u], &items[k].value, 4);
    }
    return k;
}
