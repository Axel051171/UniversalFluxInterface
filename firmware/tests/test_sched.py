"""Head-step schedule (ufi_sched.c, STEP_SCHEDULE 0x1F): entries later than one wrap of the
32-bit flux timer (15.6 s) still fall due, and a failed seek ends the running read or write
with the seek error instead of counting the entry as done.  Both were found on 2026-11-14;
the module had no test before."""
from test_regressions import ERRORS, function, run


def main():
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <string.h>\n' + ERRORS + r'''
#define SCHED_MAX 32u
#define FLUX_TIMER_FREQ 275000000u
typedef enum { CAPTURE_IDLE, CAPTURE_WAITING_INDEX, CAPTURE_RUNNING, CAPTURE_COMPLETE, CAPTURE_ERROR } capture_state_t;
typedef enum { WRITE_IDLE, WRITE_WAITING_INDEX, WRITE_ACTIVE, WRITE_COMPLETE, WRITE_ERROR } write_state_t;
static struct { capture_state_t state; uint8_t error_code; } g_capture;
static write_state_t wstate;
static struct { uint16_t at_ms; uint8_t pos; } items[SCHED_MAX];
static uint8_t count, next;
static bool quarter, armed, based;
static uint32_t t0;
static uint64_t t0_64;
static uint64_t clock64;                                /* the 64-bit flux time */
static uint64_t ufi_flux_now64(void) { return clock64; }
static write_state_t ufi_write_get_state(void) { return wstate; }
static unsigned seeks, fail_at = 99, capture_failed, write_failed;
static uint8_t seek_pos[40], seek_q[40];
static int write_err;
static int ufi_drive_seek(uint8_t t) { seek_pos[seeks] = t; seek_q[seeks] = 9; return seeks++ == fail_at ? UFI_ERR_SEEK_FAIL : UFI_OK; }
static int ufi_drive_seek_q(uint8_t t, uint8_t q) { seek_pos[seeks] = t; seek_q[seeks] = q; return seeks++ == fail_at ? UFI_ERR_SEEK_FAIL : UFI_OK; }
static void ufi_flux_capture_fail_seek(void) { capture_failed++; g_capture.error_code = 5; g_capture.state = CAPTURE_ERROR; }
static void ufi_write_fail(int err) { write_failed++; write_err = err; wstate = WRITE_ERROR; }
'''
    for name in ('ufi_sched_set', 'ufi_sched_pending', 'ufi_sched_arm', 'ufi_sched_clear', 'ufi_sched_service'):
        code += function('ufi_sched.c', name)
    code += r'''
#define MS(n) ((uint64_t)(n) * 275000u)
static void item(uint8_t* p, unsigned i, uint16_t ms, uint8_t pos) { p[3*i] = (uint8_t)ms; p[3*i+1] = (uint8_t)(ms >> 8); p[3*i+2] = pos; }
static void reset(void) { seeks = 0; fail_at = 99; capture_failed = write_failed = 0; write_err = 0; g_capture.state = CAPTURE_RUNNING; g_capture.error_code = 0; wstate = WRITE_IDLE; }

int main(void)
{
    uint8_t p[3 * 33];
    /* set: order, count, whole tracks above 83 refused; nothing armed before the operation starts */
    item(p, 0, 100, 10); item(p, 1, 50, 11);
    assert(ufi_sched_set(p, 2, false) == UFI_ERR_BAD_ARGS && !ufi_sched_pending());
    assert(ufi_sched_set(p, 33, false) == UFI_ERR_BAD_ARGS);
    item(p, 0, 100, 84);
    assert(ufi_sched_set(p, 1, false) == UFI_ERR_BAD_ARGS && !ufi_sched_pending());
    assert(ufi_sched_set(p, 1, true) == UFI_OK && ufi_sched_pending() == 1);   /* quarter units: 84 = track 21.0 */
    reset(); clock64 = MS(5); ufi_sched_service(); assert(!seeks && ufi_sched_pending() == 1);

    /* the start stamp lies shortly before a wrap of the 32-bit timer; entries at 50 ms, 16 s and 60 s */
    reset();
    item(p, 0, 50, 10); item(p, 1, 16000, 11); item(p, 2, 60000, 12);
    assert(ufi_sched_set(p, 3, false) == UFI_OK);
    const uint64_t start = ((uint64_t)7 << 32) - MS(20);            /* 20 ms before the 7th wrap */
    clock64 = start + MS(1);
    ufi_sched_arm((uint32_t)start);
    ufi_sched_service(); assert(!seeks && ufi_sched_pending() == 3);
    clock64 = start + MS(49); ufi_sched_service(); assert(!seeks);
    clock64 = start + MS(50); ufi_sched_service(); assert(seeks == 1 && seek_pos[0] == 10 && seek_q[0] == 9);
    clock64 = start + MS(15600); ufi_sched_service(); assert(seeks == 1);      /* just before one full wrap */
    clock64 = start + MS(15999); ufi_sched_service(); assert(seeks == 1);
    clock64 = start + MS(16000); ufi_sched_service(); assert(seeks == 2 && seek_pos[1] == 11);   /* was never reached */
    clock64 = start + MS(20000); ufi_sched_service(); assert(seeks == 2 && ufi_sched_pending() == 1);
    clock64 = start + MS(60000); ufi_sched_service(); assert(seeks == 3 && seek_pos[2] == 12 && !ufi_sched_pending());
    ufi_sched_service(); assert(seeks == 3);                                    /* consumed: one-shot */

    /* several entries due at once run in order; quarter units split the position */
    reset(); item(p, 0, 10, 4 * 17 + 1); item(p, 1, 20, 4 * 17 + 2);
    assert(ufi_sched_set(p, 2, true) == UFI_OK);
    clock64 = MS(1000); ufi_sched_arm((uint32_t)clock64); clock64 += MS(30); ufi_sched_service();
    assert(seeks == 2 && seek_pos[0] == 17 && seek_q[0] == 1 && seek_q[1] == 2 && !ufi_sched_pending());

    /* a failed seek during a read: capture ended with the seek error, the rest dropped, no further step */
    reset(); item(p, 0, 10, 20); item(p, 1, 20, 21); item(p, 2, 30, 22);
    assert(ufi_sched_set(p, 3, false) == UFI_OK);
    clock64 = MS(2000); ufi_sched_arm((uint32_t)clock64); fail_at = 1; clock64 += MS(40); ufi_sched_service();
    assert(seeks == 2 && capture_failed == 1 && !write_failed && g_capture.state == CAPTURE_ERROR && g_capture.error_code == 5);
    assert(!ufi_sched_pending());
    clock64 += MS(100); ufi_sched_service(); assert(seeks == 2);

    /* the same during a write */
    reset(); g_capture.state = CAPTURE_IDLE; wstate = WRITE_ACTIVE;
    assert(ufi_sched_set(p, 3, false) == UFI_OK);
    clock64 = MS(3000); ufi_sched_arm((uint32_t)clock64); fail_at = 0; clock64 += MS(15); ufi_sched_service();
    assert(seeks == 1 && write_failed == 1 && write_err == UFI_ERR_SEEK_FAIL && !capture_failed && !ufi_sched_pending());

    /* operation over before an entry fell due: the rest is dropped without a step */
    reset(); assert(ufi_sched_set(p, 3, false) == UFI_OK);
    clock64 = MS(4000); ufi_sched_arm((uint32_t)clock64); g_capture.state = CAPTURE_COMPLETE; clock64 += MS(500);
    ufi_sched_service(); assert(!seeks && !ufi_sched_pending());
    /* arming without a schedule does nothing */
    reset(); ufi_sched_arm(123); clock64 += MS(500); ufi_sched_service(); assert(!seeks);
    return 0;
}
'''
    run('regression_sched', code)
    print('sched: entries past the 15.6 s timer wrap, order, quarter units, failed seek ends read and write passed')


if __name__ == '__main__':
    main()
