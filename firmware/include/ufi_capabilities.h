#pragma once
#include <stdint.h>
#include <stdbool.h>
#define UFI_CAP_MATRIX_SIZE 224u
#define UFI_CAP_UNCONFIRMED 1u
#define UFI_WRITE_UNQUALIFIED 2u
#define UFI_PROFILE_CHANGED 4u
#define UFI_CAP_NO_FLUX_PORT 8u
#define UFI_CAP_BUSY 16u
#define UFI_CAP_LINE_CONFLICT 32u
/* Request flag masks the v2 parsers accept; ANALYSIS_JOB op 38 reports them (4.7, 1.18). */
#define UFI_V2_READ_FLAG_MASK    0x07u   /* READ: no index, hard sectors, quarter tracks */
#define UFI_V2_WRITE_FLAG_MASK   0xFFu   /* WRITE: every bit assigned (bit 7 = EXPECT_SET) */
#define UFI_V2_WRITE_FLAGS2_MASK 0x03u   /* WRITE flags2: erase remainder, start offset u32 follows */
#define UFI_V2_ANALYZE_FLAG_MASK 0xFFu   /* analysis flags; bits 1..7 need DETAIL (bit 0) */
#define UFI_V2_POSITION_MASK     0xB1u   /* analysis byte 1: side, quarter mode, quarter 0..3 */
#define UFI_V2_FORMAT_HINT_MAX   7u
/* Conservative session-local invalidation epoch, not a hash of settings. */
void ufi_config_changed(void);
uint32_t ufi_config_generation(void);
void ufi_capability_matrix(const uint8_t port[24], bool busy, uint8_t out[UFI_CAP_MATRIX_SIZE]);
