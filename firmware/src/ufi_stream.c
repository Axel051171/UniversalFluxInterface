/**
 * UFI Flux Engine - streamed, compact capture transfer (UFI_CMD_READ_TRACK)
 *
 * READ_TRACK_RAW sends 4 bytes per flux transition after the capture.  Here the main loop
 * encodes what DMA has already stored while the disk is still turning, so USB transfer and
 * rotation overlap, and the encoding halves the data (lossless, full 275 MHz resolution).
 *
 * Stream bytes (deltas in TIM2 ticks, time 0 = first index pulse):
 *   0x01..0xEF            flux transition, delta = byte
 *   0xF0..0xFC, b         flux transition, delta = 240 + ((byte - 0xF0) << 8) + b  (240..3567)
 *   0xFD, u32 LE          index pulse, u32 = ticks after the previous flux transition
 *   0xFE, u32 LE          flux transition, delta = u32 (0 or > 3567)
 *   0x00, 0xFF            reserved
 * The bytes travel in {UFI_EVT_FLUX_STREAM, 0, len} messages (message borders carry no
 * meaning).  The stream ends right after index pulse n, followed by
 * {UFI_EVT_READ_DONE, status, 1} + [n] (n = complete revolutions in the stream).
 *
 * Pre-index lead-in (v2 READ flag bit6 only, docs/USB_Protokoll.md 4.25; never without it):
 *   0xFF 0x01, u32 LE     first element: lead-in of L ticks, the time base starts at -L
 *   0xFF 0x02, u32 LE     index 0 (time 0), u32 = ticks after the previous transition / -L
 * Between them every transition t with -L < t < 0, deltas as above, the first one from -L;
 * the delta after 0xFF 0x02 runs from the last lead-in transition, as after 0xFD.
 *
 * Greaseweazle mode (ufi_gw.c, CMD_READ_FLUX): the same pipeline in the Greaseweazle flux
 * code (cdc_acm_protocol.h of the Greaseweazle firmware, public domain): raw bytes, no
 * message headers, ticks at GW_SAMPLE_FREQ = 275 MHz / 4, terminated by a 0 byte; the
 * result goes to ufi_gw_read_done() instead of a READ_DONE message.
 *   1..249                flux transition, delta = byte
 *   250..254, b           delta = 250 + (byte - 250) * 255 + b - 1
 *   0xFF 1 N28            index pulse, N = ticks after the last transition
 *   0xFF 2 N28, 249       flux transition, delta = N + 249
 *
 * UFI v2 mode (ufi_v2.c, READ 0x20): the v1 stream code in DATA frames (type 4, seq of the
 * request, <= 4096 payload bytes, CRC); READ_DONE becomes the END frame.
 */

#include "ufi_firmware.h"
#include "ufi_evidence.h"
#include "ufi_composite.h"
#include "ufi_job.h"
#include "ufi_metrics.h"
#include "ufi_profile.h"
#include <string.h>

extern capture_context_t g_capture;

#define STREAM_BUF      8192u       /* payload bytes per message */
#define SEND_AT         4096u       /* hand a buffer to USB once it holds this much */
#define ROOM            32u         /* worst case per loop step: index markers + one delta */
#define STREAM_WORK_WORDS 1024u     /* bound one CPU/cache slice independently of backlog */
#define TS_MARGIN       2750        /* 10 us: the index IRQ for older samples has run */
#define DELTA_MAX       (1u << 28)  /* ~0.98 s; larger while running = not yet written */
#define STALL_MS        1000u       /* host stopped reading */
#define HDR             UFI_V2_HDR  /* room for a v2 frame header; v1 uses its last 4 bytes */

/* payload at HDR; v2 appends its CRC after the payload */
static uint8_t buf[2][HDR + STREAM_BUF + 2u];
static uint8_t cur;                 /* buffer being filled (the other one may be on USB) */
static uint32_t fill;
static uint32_t cap, send_at;       /* payload limit / send threshold per message */
static bool active, started, done;
static uint32_t rd;                 /* next sample to encode */
static uint32_t t_last;             /* time of the last emitted transition / index 0 */
static uint8_t next_idx;            /* next index pulse to emit */
static uint8_t n_final;             /* revolutions in this stream, 0 until known */
static uint32_t tx_start_ms;
static bool gw;                     /* Greaseweazle flux code (ufi_gw.c) */
static uint32_t gw_rem;             /* 275 MHz ticks not yet passed on (below one GW tick) */
static bool v2;                     /* UFI v2 DATA frames (ufi_v2.c) */
static uint8_t v2_seq;              /* seq of the READ request */
/* Lead-in (4.25), DTCM to keep the AXI budget; every ufi_stream_begin() sets them */
#define LEAD_GUARD (FLUX_TIMER_FREQ / 1000u)   /* 1 ms bound for a late index ISR while trimming */
__attribute__((section(".dtcm"))) static uint32_t lead;        /* lead-in ticks, 0 = none */
__attribute__((section(".dtcm"))) static uint32_t lead_start;  /* window start: transitions after it */
__attribute__((section(".dtcm"))) static uint32_t lead_floor;  /* newest stamp trimmed before index 0 */
__attribute__((section(".dtcm"))) static bool lead_floored, lead_skip;

/* ============================================================================
 * ENCODER
 * ============================================================================ */

static void put_u32(uint8_t* o, uint32_t v)
{
    o[0] = (uint8_t)v;
    o[1] = (uint8_t)(v >> 8);
    o[2] = (uint8_t)(v >> 16);
    o[3] = (uint8_t)(v >> 24);
}

/* Greaseweazle N28: 28-bit value in 4 bytes, bit 0 of each byte set */
static void put_n28(uint8_t* o, uint32_t n)
{
    o[0] = (uint8_t)(1u | (n << 1));
    o[1] = (uint8_t)(1u | (n >> 6));
    o[2] = (uint8_t)(1u | (n >> 13));
    o[3] = (uint8_t)(1u | (n >> 20));
}

static void put_delta_gw(uint32_t d)
{
    const uint64_t acc = (uint64_t)d + gw_rem; /* include carry at timer wrap */
    uint32_t v = (uint32_t)(acc >> GW_TICK_SHIFT);
    gw_rem = acc & ((1u << GW_TICK_SHIFT) - 1u);
    uint8_t* o = &buf[cur][HDR + fill];
    while (v > 0x0FFFFFFFu) {
        o[0] = 0xFF; o[1] = 2;
        put_n28(&o[2], 0x0FFFFFFFu);
        fill += 6; o += 6;
        v -= 0x0FFFFFFFu;
    }
    if (v == 0u) {
        gw_rem = acc;                   /* < 1 GW tick: merge into the next transition */
    } else if (v < 250u) {
        o[0] = (uint8_t)v;
        fill += 1;
    } else if (v < 250u + 5u * 255u) {
        o[0] = (uint8_t)(250u + (v - 250u) / 255u);
        o[1] = (uint8_t)(1u + (v - 250u) % 255u);
        fill += 2;
    } else {
        o[0] = 0xFF;
        o[1] = 2;                       /* FLUXOP_SPACE */
        put_n28(&o[2], v - 249u);
        o[6] = 249;
        fill += 7;
    }
}

static void put_delta(uint32_t d)
{
    if (gw) {
        put_delta_gw(d);
        return;
    }
    uint8_t* o = &buf[cur][HDR + fill];
    if (d >= 1u && d <= 0xEFu) {
        o[0] = (uint8_t)d;
        fill += 1;
    } else if (d >= 240u && d <= 3567u) {
        const uint32_t v = d - 240u;
        o[0] = (uint8_t)(0xF0u + (v >> 8));
        o[1] = (uint8_t)v;
        fill += 2;
    } else {
        o[0] = 0xFE;
        put_u32(&o[1], d);
        fill += 5;
    }
}

static void put_index(uint32_t offset)
{
    uint8_t* o = &buf[cur][HDR + fill];
    if (gw) {
        /* SPACE advances the code cursor without inventing a flux transition. */
        uint64_t ticks = (uint64_t)offset + gw_rem;
        while ((ticks >> GW_TICK_SHIFT) > 0x0FFFFFFFu) {
            o[0] = 0xFF; o[1] = 2;
            put_n28(&o[2], 0x0FFFFFFFu);
            fill += 6; o += 6;
            ticks -= (uint64_t)0x0FFFFFFFu << GW_TICK_SHIFT;
            t_last += 0x0FFFFFFFu << GW_TICK_SHIFT;
        }
        o[0] = 0xFF;
        o[1] = 1;                       /* FLUXOP_INDEX, cursor unchanged */
        put_n28(&o[2], (uint32_t)(ticks >> GW_TICK_SHIFT));
        fill += 6;
        return;
    }
    o[0] = 0xFD;
    put_u32(&o[1], offset);
    fill += 5;
}

/* Lead-in marks (4.25): kind 1 = lead-in of v ticks, kind 2 = index 0, v ticks after t_last */
static void put_lead(uint8_t kind, uint32_t v)
{
    uint8_t* o = &buf[cur][HDR + fill];
    o[0] = 0xFF;
    o[1] = kind;
    put_u32(&o[2], v);
    fill += 6;
}

/* Emit every known index pulse up to time ts (inclusive limit: index <= ts) */
static void put_indexes_before(uint32_t ts, bool all)
{
    const uint8_t known = ufi_flux_index_count();
    while (!done && fill <= cap - ROOM && next_idx < known &&
           (all || (int32_t)(ts - ufi_flux_index_time(next_idx)) >= 0)) {
        const uint32_t period = ufi_flux_index_time(next_idx) - t_last;
        if (lead && !next_idx) {
            put_lead(2, period);            /* index 0 closes the lead-in: no revolution, no event */
            next_idx++;
            continue;
        }
        put_index(period);
        next_idx++;
        if (v2 && ufi_v2_read_progress()) {     /* READ flag bit4 (1.18): one event per completed revolution */
            uint8_t ev[9];
            ev[0] = (uint8_t)(next_idx - 1u);
            memcpy(ev + 1, &period, 4);
            const uint32_t ms = HAL_GetTick() - tx_start_ms;
            memcpy(ev + 5, &ms, 4);
            ufi_v2_event(UFI_V2_EVT_READ_PROGRESS, ev, sizeof ev);
        }
        if (!gw && n_final && next_idx > n_final) {
            done = true;                    /* stream ends right after index n */
        }
    }
}

static void encode(bool halted)
{
    uint32_t wr = ufi_flux_written();
    if (!halted && wr > 0) {
        wr--;                               /* the newest word may still be on its way */
    }
    const uint32_t now = ufi_flux_now();
    uint32_t words;
    const volatile uint32_t* s = ufi_flux_store(&words);

    /* The store is a ring here: logical sample n sits at n % words */
    uint32_t phys = rd % words;
    uint32_t cache_end=phys,work=0;
    while (!done && rd < wr && fill <= cap - ROOM && work<STREAM_WORK_WORDS) {
        if(phys==cache_end){
            uint32_t run=wr-rd;
            if(run>words-phys)run=words-phys;
            if(run>STREAM_WORK_WORDS-work)run=STREAM_WORK_WORDS-work;
            uint32_t from=phys&~7u;
            uint32_t bytes=((run+phys-from)*4u+31u)&~31u;
            SCB_InvalidateDCache_by_Addr((void*)&s[from],(int32_t)bytes);
            ufi_metric_add(&ufi_metrics.cache_bytes,bytes);cache_end=phys+run;
        }
        const uint32_t ts = s[phys];
        if (lead_skip && (int32_t)(ts - lead_start) <= 0) {
            /* older than the lead-in window (4.25): consumed, never sent */
        } else {
            if (lead_skip) {
                lead_skip = false;
                ufi_flux_lead_in_base(rd);  /* stream order of the loss windows (4.24) */
            }
            if (gw && ufi_flux_gw_past_end(ts)) {
                if (halted) rd = wr;         /* discard DMA samples after the deadline */
                break;
            }
            const uint32_t d = ts - t_last;
            if (!halted && ((int32_t)(now - ts) < TS_MARGIN || (!gw && d >= DELTA_MAX))) {
                break;                      /* too fresh, or stale data: retry next call */
            }
            put_indexes_before(ts, false);
            if (done || fill > cap - ROOM) {
                break;
            }
            put_delta(ts - t_last);         /* an index SPACE may advance the cursor */
            t_last = ts;
        }
        rd++;
        work++;
        if (++phys == words) {
            phys = cache_end = 0;
        }
    }
    ufi_metric_add(&ufi_metrics.stream_samples,work);
    ufi_flux_stream_consumed(rd);           /* frees ring chunks for the DMA */
    /* Advance a silent GW cursor regularly so even a gap spanning timer wraps is
     * represented by SPACE records. Leave a margin for in-flight input DMA. */
    if (gw && !halted && rd>=ufi_flux_written() && fill<=cap-ROOM) {
        const uint32_t safe=ufi_flux_gw_cursor()-TS_MARGIN;
        if ((uint32_t)(safe-t_last)>FLUX_TIMER_FREQ && (int32_t)(safe-t_last)>0) {
            put_indexes_before(safe,false);
            if(fill<=cap-ROOM) {
                uint32_t ticks=(safe-t_last)>>GW_TICK_SHIFT;
                if(ticks>0x0fffffffu)ticks=0x0fffffffu;
                uint8_t *out=buf[cur]+HDR+fill;
                out[0]=0xff;out[1]=2;put_n28(out+2,ticks);fill+=6;
                t_last+=ticks<<GW_TICK_SHIFT;
            }
        }
    }
    /* Capture over and every stored sample used: the last index pulses follow no flux */
    if (halted && !done && rd >= wr && fill <= cap - ROOM) {
        put_indexes_before(0, true);
        if (gw && next_idx >= ufi_flux_index_count()) done = true;
    }
}

/* Waiting for index 0 with a lead-in (4.25): index 0 lies in the future, so a transition more
 * than lead + LEAD_GUARD ticks old can never fall into its window and is consumed to keep the
 * ring free.  The newest trimmed stamp is noted: should index 0 still be older than the guard
 * assumes, the window starts there and the header reports the shorter lead-in. */
static void lead_trim(void)
{
    const uint32_t now = ufi_flux_now();
    uint32_t wr = ufi_flux_written();
    if (g_capture.state != CAPTURE_WAITING_INDEX) {
        return;                             /* index 0 arrived meanwhile: lead_open() decides */
    }
    if (wr > 0) {
        wr--;                               /* the newest word may still be on its way */
    }
    uint32_t words;
    const volatile uint32_t* s = ufi_flux_store(&words);
    uint32_t work = 0;
    while (rd < wr && work < STREAM_WORK_WORDS) {
        const uint32_t phys = rd % words;
        if (!work || !(phys & 7u)) {
            SCB_InvalidateDCache_by_Addr((void*)&s[phys & ~7u], 32);
            ufi_metric_add(&ufi_metrics.cache_bytes, 32);
        }
        const uint32_t ts = s[phys];
        if ((int32_t)(now - ts) <= (int32_t)(lead + LEAD_GUARD)) {
            break;
        }
        lead_floor = ts;
        lead_floored = true;
        rd++;
        work++;
    }
    ufi_flux_stream_consumed(rd);
}

/* Index 0 is known: the lead-in header opens the stream, transitions up to lead_start are
 * skipped by encode() */
static void lead_open(void)
{
    const uint32_t t0 = ufi_flux_index_time(0);
    lead_start = t0 - lead;
    if (lead_floored && (int32_t)(lead_floor - lead_start) > 0) {
        lead_start = (int32_t)(lead_floor - t0) < 0 ? lead_floor : t0;
    }
    put_lead(1, t0 - lead_start);
    t_last = lead_start;
    next_idx = 0;                           /* index 0 is sent as 0xFF 0x02 */
    lead_skip = true;
}

/* ============================================================================
 * SEND / FINISH
 * ============================================================================ */

static void stop(void)
{
    ufi_evidence_finish();
    active = false;
    ufi_flux_set_streaming(false);
    g_capture.state = CAPTURE_IDLE;
    led_set(&PIN_LED_FDD, false);
}

static void finish(int result)
{
    if (!(v2 ? ufi_native_tx_idle() : ufi_usb_tx_idle())) {
        return;                             /* last data message still on its way */
    }
    if (gw) {
        /* end of stream: one 0 byte, then the status for CMD_GET_FLUX_STATUS */
        static const uint8_t eos = 0;
        ufi_usb_tx_start(&eos, 1);
        stop();
        ufi_gw_read_done(result);
        return;
    }
    const uint8_t revs = (next_idx > 0) ? (uint8_t)(next_idx - 1u) : 0u;
    ufi_usb_send_read_done(result, revs);
    stop();
}

/* Returns false if the host stopped reading (stream aborted) */
static bool pump(void)
{
    if (fill == 0 || (fill < send_at && !done)) {
        return true;
    }
    if (!(v2 ? ufi_native_tx_idle() : ufi_usb_tx_idle())) {
        if (HAL_GetTick() - tx_start_ms > STALL_MS) {
            ufi_flux_capture_stop();
            stop();
            if (gw) {
                ufi_gw_read_done(UFI_ERR_BUFFER_FULL);
            }
            return false;
        }
        if(v2)ufi_evidence_usb(0,true);
        return true;
    }
    const uint8_t* p;
    uint32_t n;
    if (v2) {                               /* DATA frame: header + payload + CRC */
        if (ufi_job_live()) {
            memmove(buf[cur]+HDR+UFI_JOB_HEADER,buf[cur]+HDR,fill);
            if(!ufi_job_emit(buf[cur]+HDR,buf[cur]+HDR+UFI_JOB_HEADER,fill,HAL_GetTick())) {
                ufi_capture_abort();stop();return false;
            }
            fill+=UFI_JOB_HEADER;
        }
        n = ufi_v2_frame(buf[cur], UFI_V2_DATA, UFI_V2_READ, v2_seq, 0, (uint16_t)fill);
        p = buf[cur];
    } else if (gw) {                        /* Greaseweazle: raw bytes, no header */
        n = fill;
        p = buf[cur] + HDR;
    } else {
        const ufi_response_header_t h = {.command = UFI_EVT_FLUX_STREAM, .status = 0,
                                         .length = (uint16_t)fill};
        memcpy(buf[cur] + HDR - sizeof(h), &h, sizeof(h));
        n = sizeof(h) + fill;
        p = buf[cur] + HDR - sizeof(h);
    }
    if ((v2 ? ufi_native_tx_start(p, n) : ufi_usb_tx_start(p, n)) != UFI_OK) {
        ufi_flux_capture_stop();
        stop();
        if (gw) {
            ufi_gw_read_done(UFI_ERR_USB);
        }
        return false;
    }
    tx_start_ms = HAL_GetTick();
    if(v2)ufi_evidence_usb(n,false);
    cur ^= 1u;
    fill = 0;
    return true;
}

/* ============================================================================
 * API
 * ============================================================================ */

void ufi_stream_begin(void)
{
    gw = false;
    v2 = false;
    cap = STREAM_BUF;
    send_at = SEND_AT;
    cur = 0;
    fill = 0;
    rd = 0;
    t_last = 0;
    next_idx = 1;
    n_final = 0;
    started = false;
    done = false;
    lead = 0;
    lead_floored = lead_skip = false;
    tx_start_ms = HAL_GetTick();
    ufi_flux_set_streaming(true);
    active = true;
}

/* Greaseweazle CMD_READ_FLUX (ufi_gw.c): same capture, Greaseweazle flux code */
void ufi_stream_begin_gw(void)
{
    ufi_stream_begin();
    gw = true;
    gw_rem = 0;
    next_idx = 0;                       /* gw counts the first index pulse as well */
}

/* UFI v2 READ (ufi_v2.c): v1 stream code in DATA frames of <= UFI_V2_MAX_PAYLOAD bytes;
 * the END frame comes from ufi_usb_send_read_done() -> ufi_v2_end() */
void ufi_stream_begin_v2(uint8_t seq)
{
    ufi_stream_begin();
    v2 = true;
    v2_seq = seq;
    cap = UFI_V2_MAX_PAYLOAD;
    if (ufi_job_live()) cap-=UFI_JOB_HEADER;
    send_at = cap - ROOM + 1u;
    lead = ufi_flux_lead_in_ticks();        /* READ flag bit6 (4.25), latched by the capture start */
}

void ufi_stream_abort(void)
{
    if (active) {
        active = false;
        ufi_flux_set_streaming(false);
    }
}

bool ufi_stream_active(void)
{
    return active;
}

static void stream_service(void)
{
    if (!active) {
        return;
    }
    const capture_state_t st = gw ? ufi_flux_poll() : g_capture.state;

    if (st == CAPTURE_ERROR) {
        /* DMA error, no index or overflow before 2 index pulses: drop the partial data */
        fill = 0;
        const int err = g_capture.error_code == 4 ? UFI_ERR_MEDIA_CHANGED : g_capture.error_code == 2 ? UFI_ERR_DMA :
                        g_capture.error_code == 3 ? UFI_ERR_NO_INDEX : UFI_ERR_BUFFER_FULL;
        if (gw) {
            finish(err);                    /* 0 byte + status, ERR LED by the host */
            return;
        }
        if ((v2 ? ufi_native_tx_idle() : ufi_usb_tx_idle())) {
            ufi_usb_send_read_done(err, 0);
            stop();
            led_set(&PIN_LED_ERR, true);
        }
        return;
    }
    if (st == CAPTURE_WAITING_INDEX && lead) {
        lead_trim();                        /* lead-in: keep only the window before index 0 */
        return;
    }
    if (st != CAPTURE_RUNNING && st != CAPTURE_COMPLETE) {
        return;                             /* waiting for the first index pulse */
    }
    if (!started) {
        t_last = gw ? ufi_flux_gw_origin() : ufi_flux_index_time(0);
        if (lead) {
            lead_open();
        }
        started = true;
    }
    const bool halted = (st == CAPTURE_COMPLETE);
    if (!gw && halted && n_final == 0) {
        uint8_t n = (uint8_t)(ufi_flux_index_count() - 1u);
        if (n > g_capture.revolutions_requested) {
            n = g_capture.revolutions_requested;
        }
        n_final = n;
        if (next_idx > n_final) {
            done = true;
        }
    }

    if (!done) {
        encode(halted);
    }
    if (!pump()) {
        return;
    }
    if (done && fill == 0) {
        finish(g_capture.error_code == 1 ? UFI_ERR_BUFFER_FULL : UFI_OK);
    }
}

void ufi_stream_service(void)
{
    ufi_profile_mark_t profile=ufi_profile_begin();
    stream_service();
    ufi_profile_end(UFI_CYCLE_STREAM,profile);
}
