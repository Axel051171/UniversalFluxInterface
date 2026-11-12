"""Reset cause and fault record of ufi_crash.c against stub registers and a stub file system.

The whole source file is compiled.  Covered: RCC_RSR is captured and cleared once, every cause
is named and implied flags are not (bench value 0x01FA0000), the fault handler stores the
record and resets without touching the cache (there is deliberately no cache function in
the stubs: a clean in the handler would not compile), CRASH.LOG saved once, and a record
that could not be saved is kept for two more starts and then dropped.
"""
from test_regressions import BASE, ERRORS, run

CODE = r'''
#define UFI_FIRMWARE_H
#include <setjmp.h>
#define UFI_FW_VERSION "test"
#define RCC_RSR_BORRSTF   0x00200000u
#define RCC_RSR_PINRSTF   0x00400000u
#define RCC_RSR_PORRSTF   0x00800000u
#define RCC_RSR_SFTRSTF   0x01000000u
#define RCC_RSR_IWDG1RSTF 0x04000000u
#define RCC_RSR_WWDG1RSTF 0x10000000u
#define RCC_RSR_LPWRRSTF  0x40000000u
static struct { uint32_t RSR; } rcc;
#define RCC (&rcc)
#define __HAL_RCC_CLEAR_RESET_FLAGS() (rcc.RSR = 0)
static struct { uint32_t CFSR, HFSR, MMFAR, BFAR; } scb;
#define SCB (&scb)
static uint32_t HAL_GetTick(void) { return 1234; }
static jmp_buf reset_jump;
static unsigned resets;
static void NVIC_SystemReset(void) { resets++; longjmp(reset_jump, 1); }
uint32_t _edtcm;
static uintptr_t __get_MSP(void) { return 0; }       /* pointer sized on the host; stack paint is not run */

static int file_result;                     /* what the SD NAND answers */
static unsigned file_writes;
static char file_text[400];
static int ufi_file_write(const char* name, uint32_t offset, bool create, const uint8_t* data, uint16_t len, uint16_t* written)
{
    assert(!strcmp(name, "CRASH.LOG") && offset == 0 && create && len < sizeof(file_text));
    file_writes++;
    if (file_result != UFI_OK) { *written = 0; return file_result; }
    memcpy(file_text, data, len); file_text[len] = 0; *written = len;
    return UFI_OK;
}
void ufi_crash_capture_reset(void); uint32_t ufi_crash_reset_flags(void); void ufi_crash_boot(void);
const char* ufi_crash_last(void); uint8_t ufi_crash_kind(void);
void ufi_crash_fault(const uint32_t* frame, uint32_t kind);
void ufi_stack_paint(void); uint32_t ufi_stack_free(void);
#include "../src/ufi_crash.c"

static void start(uint32_t rsr) { rcc.RSR = rsr; ufi_crash_capture_reset(); ufi_crash_boot(); }
static bool is(const char* text) { return !strcmp(ufi_crash_last(), text); }
static bool has(const char* text) { return strstr(ufi_crash_last(), text) != NULL; }

int main(void)
{
    memset(&rec, 0xA5, sizeof(rec));            /* DTCM after power-on: garbage, no record */

    /* causes: captured once, flags cleared, implied flags not named */
    start(0x00FA0000u);                         /* bench: plain power-on */
    assert(is("reset: power-on (rsr 00FA0000)") && rcc.RSR == 0 && ufi_crash_reset_flags() == 0x00FA0000u);
    assert(ufi_crash_kind() == 0 && file_writes == 0);
    start(0x01FA0000u);                         /* bench 2026-11-11: fault loop after power-on */
    assert(is("reset: power-on+software (rsr 01FA0000)"));
    start(RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF);   assert(is("reset: software (rsr 01400000)"));
    start(RCC_RSR_IWDG1RSTF | RCC_RSR_PINRSTF); assert(is("reset: watchdog (rsr 04400000)"));
    start(RCC_RSR_PINRSTF);                     assert(is("reset: pin (rsr 00400000)"));
    start(RCC_RSR_BORRSTF | RCC_RSR_PINRSTF);   assert(is("reset: brown-out (rsr 00600000)"));
    start(RCC_RSR_IWDG1RSTF | RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF);
    assert(is("reset: watchdog+software (rsr 05400000)"));
    start(RCC_RSR_WWDG1RSTF | RCC_RSR_LPWRRSTF); assert(has("window watchdog+low-power"));
    start(0);                                   assert(is("reset: unknown (rsr 00000000)"));

    /* fault: record stored, reset at once (no cache operation exists in this build) */
    const uint32_t frame[8] = {1, 2, 3, 4, 12, 0x08001111u, 0x08002222u, 0x61000000u};
    scb.CFSR = 0x400; scb.HFSR = 0x40000000u;
    if (!setjmp(reset_jump)) ufi_crash_fault(frame, 1);
    assert(resets == 1 && rec.magic == CRASH_MAGIC && rec.sum == checksum(&rec));

    /* next start: saved once, then gone */
    file_result = UFI_OK;
    start(RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF);
    assert(is("reset: HardFault pc=08002222 lr=08001111 cfsr=00000400 (CRASH.LOG)") && ufi_crash_kind() == 1);
    assert(file_writes == 1 && strstr(file_text, "HardFault after 1234 ms") && strstr(file_text, "reset software rsr=01400000"));
    assert(rec.magic == 0);
    start(RCC_RSR_PINRSTF);
    assert(is("reset: pin (rsr 00400000)") && file_writes == 1);

    /* storage not ready: kept for two more starts, saved as soon as it works */
    if (!setjmp(reset_jump)) ufi_crash_fault(frame, 3);
    file_result = UFI_ERR_STORAGE; file_writes = 0;
    start(RCC_RSR_SFTRSTF);
    assert(has("reset: BusFault pc=08002222") && has("(CRASH.LOG not written, kept)") && rec.magic == CRASH_MAGIC);
    assert(rec.sum == checksum(&rec) && ufi_crash_kind() == 3);
    file_result = UFI_OK;
    start(RCC_RSR_PINRSTF);
    assert(has("reset: earlier BusFault") && has("(CRASH.LOG)") && rec.magic == 0 && ufi_crash_kind() == 3);
    assert(strstr(file_text, "BusFault after 1234 ms"));

    /* storage never works: reported on three starts in all, then dropped */
    if (!setjmp(reset_jump)) ufi_crash_fault(frame, 4);
    file_result = UFI_ERR_STORAGE; file_writes = 0;
    start(RCC_RSR_SFTRSTF); assert(has("UsageFault") && has("kept") && !has("earlier"));
    start(RCC_RSR_PINRSTF); assert(has("earlier UsageFault") && has("kept"));
    start(RCC_RSR_PINRSTF); assert(has("earlier UsageFault") && has("(CRASH.LOG not written)") && rec.magic == 0);
    start(RCC_RSR_PINRSTF); assert(is("reset: pin (rsr 00400000)") && file_writes == 3 && ufi_crash_kind() == 0);

    /* a damaged record is no record */
    if (!setjmp(reset_jump)) ufi_crash_fault(frame, 1);
    rec.r[6] ^= 1u;
    start(RCC_RSR_SFTRSTF); assert(is("reset: software (rsr 01000000)"));
    puts("crash boot: every reset cause, record saved once, kept while CRASH.LOG fails, dropped after three starts passed");
    return 0;
}
'''


def main():
    run('regression_crash_boot', BASE + ERRORS + CODE)


if __name__ == '__main__':
    main()
