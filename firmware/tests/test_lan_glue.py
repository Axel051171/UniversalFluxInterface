"""LAN glue of ufi_lan.c (P06): challenge values for AUTH, transmit helpers, write permission,
version bytes.

Production functions, extracted verbatim, with the real SHA-256 module; the hardware random
number generator, the cycle counter and the tick are registers / doubles of this test.

Covered: a challenge is never repeated - not with a dead random number generator, a stopped
cycle counter and a frozen tick, and not after the same peer connects again; the entropy pool
itself is never sent; the generator is switched on once, an error flag ends the wait and data
ready is taken; the reply status of the transmit helpers, the 2 s limit of the blocking one and
that it marks the session for closing; LAN writes need lan_write and an open lock switch.
"""
import re

from test_regressions import ERRORS, FW, function, run


def main():
    source = (FW / 'src/ufi_lan.c').read_text(encoding='utf-8')
    timeout = re.search(r'^#define LAN_TX_TIMEOUT_MS\s+\w+', source, re.M).group() + '\n'
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
    code += '#include "ufi_sha256.h"\n' + ERRORS + timeout
    code += r'''
#define LAN_NONCE_SIZE 32u
#define UFI_V2_FRAME_MAX 4106u
#define UFI_FW_VERSION version_text
enum { LAN_TX_OK = 0, LAN_TX_BUSY, LAN_TX_CLOSED };
static const char* version_text = "1.18-dev";
typedef struct { uint8_t peer[4]; uint16_t peer_port; bool drop; } session_t;
static struct {
    session_t sess;
    struct { bool write; } cfg;
    uint8_t pool[SHA256_SIZE];
    uint32_t nonces;
} lan;
static int dev;

/* registers */
static struct { uint32_t AHB2ENR; } rcc;
static struct { uint32_t CR, SR, DR; } rng;
static struct { uint32_t CYCCNT; } dwt;
#define RCC (&rcc)
#define RNG (&rng)
#define DWT (&dwt)
#define RCC_AHB2ENR_RNGEN 0x40u
#define RNG_CR_RNGEN 0x04u
#define RNG_SR_DRDY 0x01u
#define RNG_SR_CECS 0x02u
#define RNG_SR_SECS 0x04u
static unsigned clock_enables;
#define __HAL_RCC_RNG_CLK_ENABLE() (rcc.AHB2ENR |= RCC_AHB2ENR_RNGEN, clock_enables++)
#define UFI_WATCHDOG_FEED() (feeds++)
static unsigned feeds;
static uint32_t now, tick_step;
static uint32_t HAL_GetTick(void) { return now += tick_step; }

static int send_result; static unsigned sends; static uint16_t sent_len; static const uint8_t* sent_p;
static int ok_after;                        /* > 0: the n-th attempt succeeds */
static int lan_session_send(session_t* s, int* d, const uint8_t* p, uint16_t n)
{
    assert(s == &lan.sess && d == &dev);
    sends++; sent_len = n; sent_p = p;
    if (ok_after > 0 && (int)sends >= ok_after) return LAN_TX_OK;
    return send_result;
}
static bool ready;
static bool lan_session_ready(session_t* s, int* d, uint32_t room) { assert(s == &lan.sess && d == &dev && room == UFI_V2_FRAME_MAX); return ready; }
static bool locked;
static bool ufi_board_jumper_locked(void) { return locked; }
'''
    for name in ('hw_random', 'mix', 'cb_nonce', 'cb_connected', 'ufi_lan_tx_room', 'ufi_lan_tx_idle', 'ufi_lan_tx_start',
                 'ufi_lan_tx_blocking', 'ufi_lan_write_allowed', 'fw_version'):
        code += function('ufi_lan.c', name)
    code += r'''
enum { N = 300 };
static uint8_t seen[N][LAN_NONCE_SIZE];
static void distinct(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        assert(memcmp(seen[i], lan.pool, SHA256_SIZE));                     /* what goes out is never the pool */
        for (unsigned j = 0; j < i; j++) assert(memcmp(seen[i], seen[j], LAN_NONCE_SIZE));
    }
}

int main(void)
{
    /* ---- hardware random number generator ---- */
    rng.SR = RNG_SR_DRDY; rng.DR = 0x12345678u;
    assert(hw_random() == 0x12345678u && clock_enables == 1 && (rng.CR & RNG_CR_RNGEN) && (rcc.AHB2ENR & RCC_AHB2ENR_RNGEN));
    rng.DR = 0xCAFEF00Du; assert(hw_random() == 0xCAFEF00Du && clock_enables == 1);       /* switched on once */
    rng.SR = 0; assert(hw_random() == 0);                                               /* never ready: gives up */
    rng.SR = RNG_SR_SECS | RNG_SR_DRDY; assert(hw_random() == 0);                        /* seed error: the value is not trusted */
    rng.SR = RNG_SR_CECS | RNG_SR_DRDY; assert(hw_random() == 0);                        /* clock error */

    /* ---- challenges: 300 in a row with every source of change dead ---- */
    memset(&lan, 0, sizeof lan); rng.SR = 0; dwt.CYCCNT = 0; now = 0; tick_step = 0;
    for (unsigned i = 0; i < N; i++) cb_nonce(NULL, seen[i]);
    distinct(N); assert(lan.nonces == N);
    /* the same peer again and again, a connect in between */
    memset(&lan, 0, sizeof lan); lan.sess.peer[0] = 192; lan.sess.peer[3] = 7; lan.sess.peer_port = 50000;
    for (unsigned i = 0; i < N; i++) { if (i % 3u == 0u) cb_connected(NULL); cb_nonce(NULL, seen[i]); }
    distinct(N);
    /* every input reaches the pool: one changed source gives another first challenge */
    uint8_t base[LAN_NONCE_SIZE], other[LAN_NONCE_SIZE];
    memset(&lan, 0, sizeof lan); rng.SR = 0; dwt.CYCCNT = 0; now = 0; cb_nonce(NULL, base);
    memset(&lan, 0, sizeof lan); cb_nonce(NULL, other); assert(!memcmp(base, other, LAN_NONCE_SIZE));       /* the model is deterministic */
    memset(&lan, 0, sizeof lan); dwt.CYCCNT = 1; cb_nonce(NULL, other); assert(memcmp(base, other, LAN_NONCE_SIZE)); dwt.CYCCNT = 0;
    memset(&lan, 0, sizeof lan); now = 1; cb_nonce(NULL, other); assert(memcmp(base, other, LAN_NONCE_SIZE)); now = 0;
    memset(&lan, 0, sizeof lan); rng.SR = RNG_SR_DRDY; rng.DR = 1; cb_nonce(NULL, other); assert(memcmp(base, other, LAN_NONCE_SIZE)); rng.SR = 0;
    memset(&lan, 0, sizeof lan); lan.sess.peer[2] = 1; cb_nonce(NULL, other); assert(memcmp(base, other, LAN_NONCE_SIZE));
    memset(&lan, 0, sizeof lan); lan.pool[31] = 1; cb_nonce(NULL, other); assert(memcmp(base, other, LAN_NONCE_SIZE));
    memset(&lan, 0, sizeof lan); lan.sess.peer_port = 1; cb_connected(NULL); cb_nonce(NULL, other);
    uint8_t plain[LAN_NONCE_SIZE]; memset(&lan, 0, sizeof lan); cb_connected(NULL); cb_nonce(NULL, plain); assert(memcmp(plain, other, LAN_NONCE_SIZE));

    /* ---- transmit helpers ---- */
    static const uint8_t frame[16] = {0x55, 0xAA};
    memset(&lan, 0, sizeof lan); tick_step = 1; ok_after = 0;
    ready = true; assert(ufi_lan_tx_room() && ufi_lan_tx_idle()); ready = false; assert(!ufi_lan_tx_room() && !ufi_lan_tx_idle());
    sends = 0; send_result = LAN_TX_OK; assert(ufi_lan_tx_start(frame, 16) == UFI_OK && sends == 1 && sent_len == 16 && sent_p == frame);
    send_result = LAN_TX_BUSY; assert(ufi_lan_tx_start(frame, 16) == UFI_ERR_BUSY);
    send_result = LAN_TX_CLOSED; assert(ufi_lan_tx_start(frame, 16) == UFI_ERR_USB);
    sends = 0; send_result = LAN_TX_OK;
    assert(ufi_lan_tx_start(frame, UFI_V2_FRAME_MAX + 1u) == UFI_ERR_USB && ufi_lan_tx_blocking(frame, UFI_V2_FRAME_MAX + 1u) == UFI_ERR_USB && !sends);
    assert(ufi_lan_tx_start(frame, UFI_V2_FRAME_MAX) == UFI_OK && sent_len == UFI_V2_FRAME_MAX);
    /* blocking: retries while busy, ends at once on a closed session, gives up after 2 s and marks the session */
    sends = 0; send_result = LAN_TX_BUSY; ok_after = 5; feeds = 0;
    assert(ufi_lan_tx_blocking(frame, 16) == UFI_OK && sends == 5 && feeds == 4 && !lan.sess.drop);
    sends = 0; ok_after = 0; send_result = LAN_TX_CLOSED; assert(ufi_lan_tx_blocking(frame, 16) == UFI_ERR_USB && sends == 1 && !lan.sess.drop);
    sends = 0; send_result = LAN_TX_BUSY; now = 0xFFFFFC00u;                         /* the tick wraps during the wait */
    const uint32_t t0 = now;
    assert(ufi_lan_tx_blocking(frame, 16) == UFI_ERR_USB && lan.sess.drop);
    assert(now - t0 >= LAN_TX_TIMEOUT_MS && now - t0 <= LAN_TX_TIMEOUT_MS + 2u && sends >= LAN_TX_TIMEOUT_MS - 1u && LAN_TX_TIMEOUT_MS == 2000u);

    /* ---- LAN writes: lan_write=1 and the lock switch open ---- */
    lan.cfg.write = false; locked = false; assert(!ufi_lan_write_allowed());
    lan.cfg.write = true; assert(ufi_lan_write_allowed());
    locked = true; assert(!ufi_lan_write_allowed());
    lan.cfg.write = false; assert(!ufi_lan_write_allowed());

    /* ---- version bytes for the discovery answer ---- */
    static const struct { const char* text; uint8_t major, minor; } v[] = {
        {"1.18-dev", 1, 18}, {"1.8", 1, 8}, {"2.0", 2, 0}, {"12.34.56", 12, 34}, {"fuzz", 0, 0}, {"", 0, 0}, {"3", 3, 0}, {"1.x", 1, 0}, {"1.2a3", 1, 2}};     /* the number ends at the first other character */
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        uint8_t major = 0xEE, minor = 0xEE;
        version_text = v[i].text; fw_version(&major, &minor);
        assert(major == v[i].major && minor == v[i].minor);
    }
    puts("lan glue: challenges never repeat and never show the pool, random number generator states, transmit helpers, write permission, version");
    return 0;
}
'''
    run('regression_lan_glue', code, sources=('src/ufi_sha256.c',))


if __name__ == '__main__':
    main()
