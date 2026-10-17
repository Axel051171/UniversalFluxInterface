/* LibFuzzer harness for the drive control module (ufi_drive.c compiled as a whole with
 * the real media, capabilities and extra-timing modules; HAL GPIO, time and the SD file
 * API are doubles here, everything else is a generated stub, see fuzz_stubs.py).
 *
 * Input: byte 0 = drive type of the first SELECT, then an operation script; each byte
 * picks an operation (select, motor, seek, step, recalibrate, side, timings, presets,
 * operating profiles, RPM line, media profile, port report, probe, seek test, safe state,
 * profile file round trip) whose arguments come from the oracle tail of the same input.
 *
 * The TRK00 sensor follows a modelled head position that moves on STEP edges in the
 * DIR direction; the oracle injects rare sensor faults.  Checks: ASan/UBSan, libFuzzer
 * timeout, and for operations without an injected fault: a successful seek leaves the
 * module and the model on the requested physical track, a successful recalibrate on
 * track 0, a stored profile reads back unchanged. */
#include "ufi_firmware.h"
#include "board.h"
#include "ufi_drive_extra.h"
#include "ufi_media.h"
#include "ufi_capabilities.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FUZZ_TRACE=1: one line per operation and per modelled step (fuzz_drive.exe <artifact>) */
static int trace = -1;
static void tr(const char* what, int a, int b, int c)
{
    if (trace < 0) trace = getenv("FUZZ_TRACE") != NULL;
    if (trace) fprintf(stderr, "  %-10s %4d %4d %4d\n", what, a, b, c);
}

static const uint8_t* tail;
static size_t tail_n;
static uint32_t prng;
static uint8_t zero_buf[8192];

uint8_t fuzz_byte(void)
{
    if (tail_n) return tail[--tail_n];
    prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5;
    return (uint8_t)prng;
}
bool fuzz_bit(void) { return (fuzz_byte() & 1u) != 0; }
uint16_t fuzz_u16(void) { return (uint16_t)fuzz_u32(); }
uint32_t fuzz_u32(void)
{
    const uint8_t b = fuzz_byte();
    switch (b >> 6) {
        case 0: return b & 63u;
        case 1: return 0xFFFFFFFFu - (b & 63u);
        case 2: return (uint32_t)(b & 63u) << 8;
        default: return 1u << (b & 31u);
    }
}
int fuzz_err(void)
{
    const uint8_t b = fuzz_byte();
    return b < 22u ? -(int)b : 0;
}
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- pins the model knows (board v0.7 numbering); the rest are generated zeros ------ */
const gpio_pin_t PIN_FDD_MOTOR_A = {GPIOE, 1u << 7};
const gpio_pin_t PIN_FDD_MOTOR_B = {GPIOE, 1u << 8};
const gpio_pin_t PIN_FDD_DRV_SEL_A = {GPIOE, 1u << 9};
const gpio_pin_t PIN_FDD_DRV_SEL_B = {GPIOE, 1u << 10};
const gpio_pin_t PIN_FDD_STEP = {GPIOE, 1u << 11};
const gpio_pin_t PIN_FDD_DIR = {GPIOE, 1u << 12};
const gpio_pin_t PIN_FDD_SIDE_SEL = {GPIOE, 1u << 13};
const gpio_pin_t PIN_FDD_TRACK0 = {GPIOF, 1u << 0};
const gpio_pin_t PIN_FDD_WPROT = {GPIOF, 1u << 1};
const gpio_pin_t PIN_FDD_DKCHG = {GPIOF, 1u << 2};
const gpio_pin_t PIN_FDD_READY = {GPIOF, 1u << 3};

/* ---- doubles with behaviour ------------------------------------------------- */
static uint32_t ticks;
static int head_pos[DRIVE_TYPE_COUNT + 1];  /* modelled physical track per drive, 0..90 */
static bool step_asserted;
static unsigned faults;             /* sensor faults injected during the current operation */
static bool tainted[DRIVE_TYPE_COUNT + 1];  /* a sensor lie may still sit in that drive's position */
static bool* taint(void) { return &tainted[ufi_drive_get_current() % (DRIVE_TYPE_COUNT + 1u)]; }

static int* pos(void) { return &head_pos[ufi_drive_get_current() % (DRIVE_TYPE_COUNT + 1u)]; }

uint32_t HAL_GetTick(void)
{
    ticks += 1u;
    if (fuzz_byte() == 0x7Fu) ticks += 20000u;
    return ticks;
}
void HAL_Delay(uint32_t ms) { ticks += ms; }

void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    if (!port) return;                                  /* generated zero pin */
    if (state == GPIO_PIN_SET) port->ODR |= pin; else port->ODR &= (uint32_t)~pin;
    if (port == GPIOE && pin == PIN_FDD_STEP.pin) {
        const bool assert_now = state == BUS_OUT_ASSERT;
        if (assert_now && !step_asserted) {             /* falling edge on the cable = one step */
            const bool inwards = (GPIOE->ODR & PIN_FDD_DIR.pin) == 0;   /* DIR asserted = in */
            int* p = pos();
            if (inwards) { if (*p < 90) (*p)++; }
            else if (*p > 0) (*p)--;
            tr("step", inwards, *p, (int)ufi_drive_get_current());
        }
        step_asserted = assert_now;
    }
}
void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin)
{
    if (port) HAL_GPIO_WritePin(port, pin, (port->ODR & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin)
{
    if (port == GPIOF && pin == PIN_FDD_TRACK0.pin) {
        bool at0 = *pos() == 0;
        if (fuzz_byte() == 0x11u) { at0 = !at0; faults++; *taint() = true; }  /* rare sensor fault */
        return at0 ? BUS_IN_ASSERTED : (BUS_IN_ASSERTED == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
    return fuzz_bit() ? GPIO_PIN_SET : GPIO_PIN_RESET;  /* WPROT, DKCHG, READY, Apple inputs, ... */
}

/* SD file API (ufi_dump.c is not linked): one file image, occasionally failing or bit-flipped */
static uint8_t file_img[1024];
static uint16_t file_len;
int ufi_file_read(const char* name, uint32_t offset, uint8_t* out, uint16_t len, uint16_t* got)
{
    (void)name;
    *got = 0;
    const int e = fuzz_err();
    if (e) return e;
    if (offset >= file_len) return UFI_OK;
    const uint16_t n = (uint16_t)(len < file_len - offset ? len : file_len - offset);
    memcpy(out, file_img + offset, n);
    if (n && fuzz_byte() < 16u) out[fuzz_u16() % n] ^= (uint8_t)(1u << (fuzz_byte() & 7u));
    *got = n;
    return UFI_OK;
}
int ufi_file_write(const char* name, uint32_t offset, bool create, const uint8_t* data, uint16_t len, uint16_t* written)
{
    (void)name;
    *written = 0;
    const int e = fuzz_err();
    if (e) return e;
    if (create) file_len = 0;
    if (offset > sizeof file_img) return UFI_ERR_STORAGE;
    uint16_t n = (uint16_t)(len < sizeof file_img - offset ? len : sizeof file_img - offset);
    if (fuzz_byte() == 0x22u && n) n = (uint16_t)(fuzz_u16() % n);      /* volume full */
    memcpy(file_img + offset, data, n);
    if (offset + n > file_len) file_len = (uint16_t)(offset + n);
    *written = n;
    return n == len ? UFI_OK : UFI_ERR_STORAGE;
}

/* ---- one input -------------------------------------------------------------- */
static drive_timing_t random_timing(void)
{
    drive_timing_t t = ufi_drive_get_timing();
    uint16_t* f = (uint16_t*)&t;
    for (unsigned i = 0; i < sizeof t / 2u; i++) {
        if (fuzz_byte() < 96u) f[i] = fuzz_u16();
    }
    if (fuzz_bit()) t.double_step &= 1u;
    if (fuzz_bit()) t.tg43 &= 1u;
    return t;
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (n < 2) return 0;
    tail = d; tail_n = n; prng = 0x9E3779B9u ^ (uint32_t)n; ticks = 0;
    memset(zero_buf, 0, sizeof zero_buf);
    memset(stub_gpio, 0, sizeof stub_gpio);
    /* the module keeps per-drive positions in statics across inputs; forget them so that
     * every input starts with unknown positions and a freshly randomised head model */
    ufi_drive_position_invalidate();
    for (unsigned t = 0; t <= DRIVE_TYPE_COUNT; t++) head_pos[t] = (int)(fuzz_byte() % 84u);
    step_asserted = false;
    memset(tainted, 0, sizeof tainted);
    file_len = 0;
    ufi_drive_init();
    ufi_drive_safe_state();
    (void)ufi_drive_select((drive_type_t)(d[0] % (DRIVE_TYPE_COUNT + 1u)));

    const size_t ops = n - 1u < 96u ? n - 1u : 96u;
    for (size_t i = 0; i < ops; i++) {
        const uint8_t op = d[1 + i];
        faults = 0;
        uint8_t out40[40];
        tr("op", op % 24u, (int)ufi_drive_get_current(), *pos());
        switch (op % 24u) {
            case 0: (void)ufi_drive_select((drive_type_t)(fuzz_byte() % (DRIVE_TYPE_COUNT + 1u))); break;
            case 1: (void)ufi_drive_motor(fuzz_bit()); break;
            case 2: {
                const uint8_t track = fuzz_byte() % 92u;
                const bool dstep = ufi_drive_get_timing().double_step != 0;
                const int ret = ufi_drive_seek(track);
                const drive_status_t s = ufi_drive_get_status();   /* samples TRK00 itself */
                if (ret == UFI_OK && !ufi_drive_is_apple() && !faults && !*taint()) {
                    const unsigned phys = dstep ? 2u * track : track;
                    assert(s.current_track == phys);
                    assert(*pos() == (int)phys);
                }
                break;
            }
            case 3: (void)ufi_drive_step(fuzz_bit() ? 1 : -1); break;
            case 4: {
                const int ret = ufi_drive_recalibrate();
                const drive_status_t s = ufi_drive_get_status();   /* samples TRK00 itself */
                if (ret == UFI_OK && !ufi_drive_is_apple() && !faults) {
                    *taint() = false;                   /* a clean recalibrate re-syncs this drive */
                    assert(s.current_track == 0 && s.track0);
                    assert(*pos() == 0);
                }
                break;
            }
            case 5: (void)ufi_drive_select_side(fuzz_byte() & 3u); break;
            case 6: { drive_timing_t t = random_timing(); if (fuzz_bit()) (void)ufi_drive_timing_checked(&t); else ufi_drive_set_timing(&t); break; }
            case 7: {
                drive_extra_t e = ufi_drive_extra_get();
                uint16_t* f = (uint16_t*)&e;
                f[fuzz_byte() % 6u] = fuzz_bit() ? fuzz_u16() : (uint16_t)(fuzz_byte() * 100u);
                (void)ufi_drive_extra_set(&e);
                break;
            }
            case 8: (void)ufi_drive_operating_preset(fuzz_byte() % 4u, out40); break;
            case 9: {
                uint8_t profile[40];
                if (ufi_drive_operating_profile(NULL, profile) == UFI_OK) {
                    profile[39] = 0;
                    if (fuzz_byte() < 64u) profile[fuzz_byte() % 40u] = fuzz_byte();
                    const int ret = ufi_drive_operating_profile(profile, out40);
                    if (ret == UFI_OK) {
                        assert(out40[39] == 1 && !memcmp(out40 + 1, profile + 1, 34));
                        (void)ufi_drive_profiles_save();
                    }
                }
                break;
            }
            case 10: (void)ufi_drive_operating_profile(NULL, out40); break;
            case 11: (void)ufi_drive_select_native((drive_type_t)(fuzz_byte() % (DRIVE_TYPE_COUNT + 1u))); break;
            case 12: ufi_drive_rpm_config(fuzz_byte() % 4u, fuzz_bit()); (void)ufi_drive_set_rpm(fuzz_bit() ? (fuzz_bit() ? 300u : 360u) : fuzz_u16()); break;
            case 13: { uint8_t rep[24]; ufi_drive_port_report(rep); assert(rep[0] == 1); break; }
            case 14: {
                uint8_t p[12];
                for (unsigned k = 0; k < sizeof p; k++) p[k] = fuzz_byte();
                if (fuzz_bit()) (void)ufi_drive_profile_set(p); else (void)ufi_drive_media_set(p);
                ufi_drive_media_status(out40);
                if (fuzz_bit()) ufi_drive_media_notify();
                break;
            }
            case 15: {                                  /* file round trip, with the file double's faults */
                uint8_t before[40], after[40];
                const bool have = ufi_drive_operating_profile(NULL, before) == UFI_OK;
                const int ret = ufi_drive_profiles_save();
                ufi_drive_profiles_load();
                if (have && ret == UFI_OK && !faults && ufi_drive_operating_profile(NULL, after) == UFI_OK) {
                    assert(before[39] == after[39]);    /* a stored slot survives a clean round trip */
                }
                break;
            }
            case 16: { uint8_t highest = 0; (void)ufi_drive_probe_tracks(&highest); break; }
            case 17: (void)ufi_drive_seek_test(fuzz_byte() % 90u, fuzz_byte() % 90u, fuzz_byte() & 3u); break;
            case 18: ufi_drive_safe_state(); break;
            case 19: if (fuzz_bit()) (void)ufi_drive_lines_set(fuzz_bit(), fuzz_bit()); else (void)ufi_drive_density_line(fuzz_bit()); break;
            case 20: (void)ufi_drive_seek_q(fuzz_byte() % 40u, fuzz_byte() & 3u); break;
            case 21: (void)ufi_drive_apple_step(fuzz_bit() ? 1 : -1); break;
            case 22: { bool c, p; (void)ufi_drive_check_disk(&c, &p); (void)ufi_drive_write_protected(); (void)ufi_drive_disk_changed(); (void)ufi_drive_ready(); break; }
            default: {
                uint32_t id;
                (void)ufi_drive_amiga_id(&id); (void)ufi_drive_motor_is_on(); (void)ufi_drive_get_rpm(); (void)ufi_drive_rpm_line();
                ufi_drive_media_observed(fuzz_byte(), fuzz_u32());      /* format/rate learnt from a capture */
                (void)ufi_drive_media_matches(fuzz_bit() ? ufi_drive_media_token() : fuzz_u32());
                break;
            }
        }
    }
    ufi_drive_safe_state();
    return 0;
}
