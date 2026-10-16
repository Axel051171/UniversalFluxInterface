/* LibFuzzer harness for the Greaseweazle-compatible protocol (ufi_gw.c compiled as a
 * whole with the real resource and cancel modules; drivers are generated stubs, see
 * fuzz_stubs.py).
 *
 * Input: byte 0 = mode (bit0: the native protocol owns the drive resources), then the
 * CDC byte stream exactly as a host would send it.  The stream is delivered in chunks
 * whose sizes, the interleaved main-loop calls, read completions and comms clears come
 * from the oracle tail of the same input.  Checks: ASan/UBSan, every reply is
 * [cmd, ack] plus at most 32 bytes, no hang (libFuzzer -timeout). */
#include "ufi_firmware.h"
#include "ufi_cancel.h"
#include "ufi_resources.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

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

/* ---- doubles with behaviour ------------------------------------------------- */
static uint32_t ticks;
static uint64_t flux_now;
static uint32_t tx_budget;
static uint32_t flux_words[2048];

uint32_t HAL_GetTick(void)
{
    ticks += 1u;
    if (fuzz_byte() == 0x7Fu) ticks += 20000u;      /* watchdog expiry */
    return ticks;
}
void HAL_Delay(uint32_t ms) { ticks += ms; }
uint64_t ufi_flux_now64(void) { return flux_now += 1u << 20; }
uint32_t* ufi_flux_store(uint32_t* words)
{
    *words = (uint32_t)(fuzz_u16() % 2049u);
    return flux_words;
}
int ufi_usb_tx_blocking(const uint8_t* p, uint32_t n)
{
    assert(p && n >= 1u && n <= 512u);               /* reply, status byte or SOURCE chunk */
    if (n > tx_budget) return UFI_ERR_USB;            /* bounds SOURCE_BYTES of 4 GiB */
    tx_budget -= n;
    return fuzz_byte() == 0xFFu ? UFI_ERR_USB : UFI_OK;
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (n < 2) return 0;
    tail = d; tail_n = n; prng = 0x2545F491u ^ (uint32_t)n; ticks = 0; flux_now = 0;
    tx_budget = 1u << 16;
    memset(zero_buf, 0, sizeof zero_buf);
    ufi_resources_reset();
    ufi_cancel_clear();
    ufi_gw_end();
    ufi_gw_begin();
    if (d[0] & 1u) ufi_resources_acquire(UFI_OWNER_NATIVE);

    size_t i = 1;
    while (i < n) {
        uint32_t chunk = 1u + (fuzz_byte() & 63u);
        if (chunk > n - i) chunk = (uint32_t)(n - i);
        ufi_gw_rx(d + i, chunk);
        i += chunk;
        const uint8_t act = fuzz_byte();
        ufi_gw_service();
        if ((act & 7u) == 0u) ufi_gw_read_done(fuzz_err());  /* capture finished (ufi_stream.c) */
        if ((act & 63u) == 1u) ufi_gw_clear_comms();         /* host set 10000 baud */
        if (act & 8u) ufi_gw_service();
    }
    for (int k = 0; k < 3; k++) {
        ufi_gw_service();
        ufi_gw_read_done(fuzz_err());
    }
    ufi_gw_end();
    return 0;
}
