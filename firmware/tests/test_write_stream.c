/* Native test of the write streaming bookkeeping (firmware/src/ufi_write_stream.c): a model
 * of the circular DMA + timer plays streams of many lengths through the real arithmetic
 * and checks that exactly the n items come out in order and the timer stops two updates
 * after the last one, also with late interrupts. */
#include "ufi_write_stream.h"
#include "check.h"
#include <string.h>

#define MAXN 300000u
#define MAXHALF 2048u
static uint16_t src[MAXN], out[MAXN + 16];
static uint16_t ring[2 * MAXHALF];

typedef struct { uint32_t at, due; bool tc; } ev_t;

/* returns the update at which the timer finished (0 = never), fills *got with items played */
static uint32_t simulate(uint32_t n, uint32_t half, uint32_t latency, uint32_t* got, bool* fw_underrun,
                         wstream_t* s_out)
{
    wstream_t s;
    wstream_fill_t f[2];
    const uint32_t ringn = 2u * half;
    for (uint32_t i = 0; i < n; i++) src[i] = (uint16_t)(1u + (i % 60000u));
    memset(ring, 0xEE, sizeof(ring));
    wstream_init(&s, n, half, f);
    for (int i = 0; i < 2; i++) {
        memcpy(&ring[f[i].half * half], &src[f[i].from], f[i].count * 2u);
    }
    ev_t pending[4];
    int npend = 0;
    bool ude = true, uie = s.tail;
    uint32_t transferred = 0, ndtr = ringn, finished = 0;
    *got = 0;
    *fw_underrun = false;
    for (uint32_t u = 1; u < n + 50u && !finished; u++) {
        /* the update: DMA transfer of item (u-1) from the ring, then the interrupts */
        if (ude) {
            out[(*got)++] = ring[(u - 1u) % ringn];
            transferred++;
            ndtr = ringn - (transferred % ringn);
            if (transferred % half == 0u) {
                pending[npend].at = u;
                pending[npend].due = u + latency;
                pending[npend].tc = (transferred % ringn) == 0u;
                npend++;
            }
        }
        /* DMA interrupt: events whose latency has elapsed, in order */
        while (npend && pending[0].due <= u) {
            if (npend > 1 && pending[1].at <= u) {
                *fw_underrun = true;        /* two flags pending at once: a half was missed */
            }
            wstream_fill_t r = wstream_event(&s, pending[0].tc);
            if (r.count) {
                memcpy(&ring[r.half * half], &src[r.from], r.count * 2u);
            }
            if (s.tail) uie = true;
            memmove(&pending[0], &pending[1], (size_t)(npend - 1) * sizeof(ev_t));
            npend--;
            if (s.underrun) *fw_underrun = true;
        }
        /* timer update interrupt in the tail phase */
        if (uie) {
            const int act = wstream_update(&s, ndtr);
            if (act == WSTREAM_STOP_DMA) ude = false;
            if (act == WSTREAM_FINISH) finished = u;
        }
    }
    if (s_out) *s_out = s;
    return finished;
}

static void run_case(uint32_t n, uint32_t half, uint32_t latency)
{
    uint32_t got;
    bool under;
    const uint32_t fin = simulate(n, half, latency, &got, &under, NULL);
    const bool order = got >= n && !memcmp(out, src, n * 2u);
    CHECK(fin == n + 2u && got == n && order && !under,
          "n=%u half=%u latency=%u: finished at %u (want %u), items %u, order %d, underrun %d",
          n, half, latency, fin, n + 2u, got, order, under);
}

int main(void)
{
    const uint32_t small_n[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 24, 25, 100, 1001};
    for (uint32_t i = 0; i < sizeof(small_n) / sizeof(small_n[0]); i++) {
        for (uint32_t lat = 0; lat < 3; lat++) {
            run_case(small_n[i], 8, lat);
        }
    }
    const uint32_t big_n[] = {2047, 2048, 2049, 4095, 4096, 4097, 6149, 37000, 65535, 65536, 65537, 65538, 200000};
    for (uint32_t i = 0; i < sizeof(big_n) / sizeof(big_n[0]); i++) {
        run_case(big_n[i], WSTREAM_HALF, 0);
        run_case(big_n[i], WSTREAM_HALF, 5);
        run_case(big_n[i], WSTREAM_HALF, 700);
    }

    /* a late update interrupt: the last item went in one update ago -> only one trailing
     * update is left; two ago -> finish at once */
    wstream_t s;
    wstream_fill_t f[2];
    wstream_init(&s, 10, 8, f);
    CHECK(s.tail, "n=10 <= 2 halves: counting from the start");
    wstream_event(&s, false);                       /* half 1 (items 8..9) playing */
    CHECK(wstream_update(&s, 16u - 9u) == WSTREAM_GO, "9 of 10 transferred: go");
    CHECK(wstream_update(&s, 16u - 11u) == WSTREAM_STOP_DMA && s.updates_left == 1, "late by one: one trailing update");
    CHECK(wstream_update(&s, 0) == WSTREAM_FINISH, "then finish");
    wstream_init(&s, 10, 8, f);
    wstream_event(&s, false);
    CHECK(wstream_update(&s, 16u - 12u) == WSTREAM_FINISH, "late by two: finish at once");

    /* wrap not yet processed: NDTR reloaded, TC event still pending */
    wstream_init(&s, 40, 8, f);
    wstream_event(&s, false);                       /* k=1: 8 done */
    CHECK(wstream_done(&s, 5u) == 11u, "done inside the cycle (%u)", wstream_done(&s, 5u));
    CHECK(wstream_done(&s, 1u) == 15u, "done at the end of the cycle");
    CHECK(wstream_done(&s, 16u) == 16u, "counter reloaded before the TC was processed -> 16");
    CHECK(wstream_done(&s, 14u) == 18u, "ambiguity resolved towards the known events");
    wstream_init(&s, 1000, 8, f);
    for (int k = 0; k < 10; k++) wstream_event(&s, k & 1);   /* 10 events: 80 transferred */
    CHECK(!s.tail, "no counting far from the end");
    for (int k = 10; k < 123; k++) wstream_event(&s, k & 1);  /* k0 = 999/8 = 124: tail at 123 */
    CHECK(s.tail && s.events == 123, "counting starts one half before the last (events %u)", s.events);

    /* underrun: the refill interrupt comes a whole half late */
    uint32_t got;
    bool under;
    simulate(100, 8, 9, &got, &under, &s);
    CHECK(under, "latency of a whole half is reported as underrun");
    CHECK_SUMMARY("test_write_stream");
}
