/* Track survey (ANALYSIS_JOB op 46, USB_Protokoll 4.15): one revolution per track and side over a
 * range, classified with the ANALYZE histogram, kept as a table the host reads while the survey
 * still runs.  Session-local; a new start replaces the table.  The table lives in D2 SRAM1 next to
 * the capture evidence so the AXI reserve and the DTCM stack budget stay untouched. */
#include "ufi_firmware.h"
#include "ufi_analysis_job.h"
#include "ufi_cancel.h"
#include "ufi_job.h"
#include "ufi_media.h"
#include "ufi_capabilities.h"
#include <string.h>
extern capture_context_t g_capture;
#define SURVEY_TRACKS    84u
#define SURVEY_ENTRIES   (2u * SURVEY_TRACKS)
#define SURVEY_ENTRY     20u
#define SURVEY_HEADER    28u
#define SURVEY_STEP_MS   4000u     /* one step: seek, settle, one revolution */
#define SURVEY_MAX_FAIL  3u        /* consecutive step errors that end the survey */
enum { S_NONE, S_RUNNING, S_DONE, S_FAILED };
__attribute__((section(".sram1"))) static uint8_t entries[SURVEY_ENTRIES][SURVEY_ENTRY];
static struct {
    uint8_t state, error, first, last, sides, flags, failures;
    uint16_t cursor, total;
    uint32_t started, step_started, elapsed, config, media;
} sv;
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t* p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
bool ufi_survey_active(void) { return sv.state == S_RUNNING; }
static void survey_end(int ret)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) ufi_capture_abort();
    g_capture.state = CAPTURE_IDLE;
    sv.elapsed = HAL_GetTick() - sv.started;
    sv.error = (uint8_t)(ret < 0 ? -ret : ret);
    sv.state = ret ? S_FAILED : S_DONE;
}
void ufi_survey_abort(void) { if (ufi_survey_active()) survey_end(UFI_ERR_CANCELLED); }
/* entry order: track ascending, within the track the sides of the mask ascending */
static void position(uint16_t cursor, uint8_t* track, uint8_t* side)
{
    const unsigned per = sv.sides == 3u ? 2u : 1u;
    *track = (uint8_t)(sv.first + cursor / per);
    *side = sv.sides == 2u ? 1u : (uint8_t)(cursor % per);
}
/* Close the current step with its status; a successful capture is classified from revolution 0.
 * Returns false when the classification ended the whole survey (general ABORT). */
static bool step_done(int status)
{
    uint8_t* e = entries[sv.cursor];
    uint8_t track, side;
    position(sv.cursor, &track, &side);
    memset(e, 0, SURVEY_ENTRY);
    e[0] = track; e[1] = side;
    if (!status) {
        analyze_result_t r;
        memset(&r, 0, sizeof r);
        status = ufi_analyze_classify(ufi_capture_get_data(0), sv.step_started, (sv.flags & 1u) != 0, &r);
        if (status == UFI_ERR_CANCELLED) { survey_end(status); return false; }
        if (!status) {
            uint8_t peaks = 0;
            while (peaks < 4u && r.peak_ns[peaks]) peaks++;
            e[3] = r.encoding; wr16(e + 4, r.peak_ns[0]); e[6] = r.sectors; e[7] = peaks;
            wr32(e + 8, r.index_ticks); wr32(e + 12, r.transitions);
            wr16(e + 16, r.peak_ns[1]); wr16(e + 18, r.peak_ns[2]);
        }
    }
    e[2] = (uint8_t)(status < 0 ? -status : status);
    g_capture.state = CAPTURE_IDLE;
    sv.failures = status ? (uint8_t)(sv.failures + 1u) : 0u;
    sv.cursor++;
    return true;
}
/* Start steps until one capture runs; a start that fails is a step error of its own.
 * Returns 0 while the survey runs or finished clean, else the status that ended it. */
static int next(void)
{
    while (sv.cursor < sv.total) {
        if (sv.failures >= SURVEY_MAX_FAIL) {
            const int last = -(int)entries[sv.cursor - 1u][2];
            survey_end(last); return last;
        }
        uint8_t track, side;
        position(sv.cursor, &track, &side);
        sv.step_started = HAL_GetTick();
        const int ret = ufi_capture_start_q(track, side, 1u, 0u, 0u);
        if (!ret) return UFI_OK;
        if (ret == UFI_ERR_MEDIA_CHANGED) { survey_end(ret); return ret; }
        (void)step_done(ret);
    }
    survey_end(UFI_OK);
    return UFI_OK;
}
void ufi_survey_service(void)
{
    if (!ufi_survey_active()) return;
    if (!ufi_drive_media_matches(sv.media)) { survey_end(UFI_ERR_MEDIA_CHANGED); return; }
    if (ufi_cancel_requested()) { survey_end(UFI_ERR_CANCELLED); return; }
    const capture_state_t cs = ufi_capture_get_state();
    int status;
    if (cs == CAPTURE_ERROR) {
        status = g_capture.error_code == 4 ? UFI_ERR_MEDIA_CHANGED : g_capture.error_code == 3 ? UFI_ERR_NO_INDEX :
                 g_capture.error_code == 2 ? UFI_ERR_DMA : UFI_ERR_BUFFER_FULL;
    } else if (cs != CAPTURE_COMPLETE) {
        if (HAL_GetTick() - sv.step_started <= SURVEY_STEP_MS) return;
        ufi_capture_abort();
        status = UFI_ERR_TIMEOUT;
    } else {
        status = ufi_flux_get_revolution_count() ? UFI_OK : UFI_ERR_NO_INDEX;
    }
    if (status == UFI_ERR_MEDIA_CHANGED) { survey_end(status); return; }
    if (!step_done(status)) return;
    (void)next();
}
static uint16_t header(uint8_t* out)
{
    memset(out, 0, SURVEY_HEADER);
    out[0] = 1; out[1] = SURVEY_HEADER; out[2] = sv.state; out[3] = sv.error;
    out[4] = sv.first; out[5] = sv.last; out[6] = sv.sides; out[7] = sv.flags; out[8] = SURVEY_ENTRY;
    wr16(out + 10, sv.cursor); wr16(out + 12, sv.total);
    wr32(out + 16, ufi_survey_active() ? HAL_GetTick() - sv.started : sv.elapsed);
    wr32(out + 20, sv.config); wr32(out + 24, sv.media);
    return SURVEY_HEADER;
}
/* [46] reads (never BUSY), [46, 1, first, last, sides, flags] starts, [46, 0] aborts */
int ufi_survey_command(const uint8_t* p, uint16_t n, uint8_t* out, uint16_t* size)
{
    if (n == 1) {
        *size = header(out);
        memcpy(out + SURVEY_HEADER, entries, (size_t)sv.cursor * SURVEY_ENTRY);
        *size = (uint16_t)(*size + sv.cursor * SURVEY_ENTRY);
        return UFI_OK;
    }
    if (n == 2 && p[1] == 0) { ufi_survey_abort(); *size = header(out); return UFI_OK; }
    if (n == 6 && p[1] == 1) {
        if (p[2] > p[3] || p[3] >= SURVEY_TRACKS || !p[4] || p[4] > 3u || (p[5] & ~1u)) return UFI_ERR_BAD_ARGS;
        if (ufi_survey_active() || ufi_operation_busy() || ufi_job_live()) return UFI_ERR_BUSY;
        if (ufi_drive_get_current() == DRIVE_NONE) return UFI_ERR_NO_DRIVE;
        memset(&sv, 0, sizeof sv);
        sv.first = p[2]; sv.last = p[3]; sv.sides = p[4]; sv.flags = p[5];
        sv.total = (uint16_t)((p[3] - p[2] + 1u) * (p[4] == 3u ? 2u : 1u));
        sv.config = ufi_config_generation(); sv.media = ufi_drive_media_token();
        sv.started = HAL_GetTick(); sv.state = S_RUNNING;
        ufi_cancel_clear();
        const int ret = next();
        *size = header(out);
        return ret;
    }
    return UFI_ERR_BAD_ARGS;
}
