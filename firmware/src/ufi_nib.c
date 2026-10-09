/**
 * UFI Flux Engine - 1541 raw GCR track chunks over the serial bus (firmware 1.14)
 *
 * The drive runs a small routine (tools/iec_nib.s, bytes in include/iec_nib.h) that steps
 * the head in half tracks, finds the track origin (sync after the longest non-sync stretch)
 * and copies 512 raw GCR bytes from a chosen sync into its RAM; the host fetches them with
 * M-R and stitches the chunks of a track (software/ufi_host: ufi iec-nib).
 *
 * Code upload: M-W in 32-byte pieces, once per session (or on request).  The routine runs
 * with interrupts off, so after M-E the bus is polled until the drive answers ATN again.
 */

#include <string.h>
#include "ufi_firmware.h"
#include "iec_nib.h"

#define NIB_PARAM       0x0300u     /* P_TRACK, P_SYNC, P_MODE, P_DIR */
#define NIB_RESULT      0x0304u     /* R_STATUS, R_SYNCLEN, R_MAXLO, R_MAXHI, V_CUR */
#define NIB_BUF         0x0600u     /* 512 data bytes */
#define NIB_EXEC_MS     6000u       /* step + spin-up + scan + read: < 3 s normally */

static uint8_t uploaded_dev;        /* device holding the code, 0 = none */

int ufi_nib_upload(uint8_t dev)
{
    for (uint32_t off = 0; off < IEC_NIB_LEN; off += 32u) {
        const uint32_t n = (IEC_NIB_LEN - off < 32u) ? IEC_NIB_LEN - off : 32u;
        const int ret = ufi_iec_mem_write(dev, (uint16_t)(IEC_NIB_ORG + off), &iec_nib_code[off],
                                          (uint8_t)n);
        if (ret != UFI_OK) {
            uploaded_dev = 0;
            return ret;
        }
    }
    uploaded_dev = dev;
    return UFI_OK;
}

void ufi_nib_invalidate(void)
{
    uploaded_dev = 0;
}

int ufi_nib_chunk(uint8_t dev, uint8_t halftrack, uint8_t sync, uint8_t mode, uint8_t dir,
                  bool force, nib_result_t* r)
{
    if (halftrack < 2u || halftrack > 84u || mode > 2u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (uploaded_dev != dev || force) {
        const int ret = ufi_nib_upload(dev);
        if (ret != UFI_OK) {
            return ret;
        }
    }
    const uint8_t params[4] = {halftrack, sync, mode, dir ? dir : 1u};
    int ret = ufi_iec_mem_write(dev, NIB_PARAM, params, sizeof(params));
    if (ret != UFI_OK) {
        return ret;
    }
    ret = ufi_iec_mem_exec(dev, IEC_NIB_ORG);
    if (ret != UFI_OK) {
        return ret;
    }
    ret = ufi_iec_wait_ready(dev, NIB_EXEC_MS);
    if (ret != UFI_OK) {
        uploaded_dev = 0;                   /* drive hung or reset: upload again next time */
        return ret;
    }
    uint8_t res[5];
    ret = ufi_iec_mem_read(dev, NIB_RESULT, res, sizeof(res));
    if (ret != UFI_OK) {
        return ret;
    }
    memset(r, 0, sizeof(*r));
    r->status = res[0];
    r->synclen = res[1];
    r->maxlen = (uint16_t)(res[2] | (res[3] << 8));
    r->halftrack = res[4];
    if (mode == 2u || r->status == 1u || r->status == 2u) {
        return UFI_OK;                      /* no data */
    }
    for (uint32_t off = 0; off < sizeof(r->data); off += 128u) {
        ret = ufi_iec_mem_read(dev, (uint16_t)(NIB_BUF + off), &r->data[off], 128u);
        if (ret != UFI_OK) {
            return ret;
        }
    }
    return UFI_OK;
}
