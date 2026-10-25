/* Observations, not recovery policy. A clear bit is meaningful only if checked. */
#pragma once
#define SF_DATA_BAD       (1u << 0)
#define SF_DATA_UNCHECKED (1u << 1)
#define SF_TRACK          (1u << 2)
#define SF_SIDE           (1u << 3)
#define SF_LENGTH         (1u << 4)
#define SF_NUMBER         (1u << 5)
#define SF_TRUNCATED      (1u << 6)
#define SF_REJECTED       (1u << 7)
#define SF_EXTRA          (1u << 8)
#define SF_ENDING         (1u << 9)
#define SF_BLOCK_ID       (1u << 10)
#define SF_OFFSET         (1u << 11) /* AmigaDOS sectors-to-gap outside 1..11/22; rule 1 */
#define SF_PROTECTION     (1u << 12) /* reserved: no generic protection verdict */
#define SF_VARIANTS       (1u << 13)
#define SF_HEADER_BAD     (1u << 14)
#define SF_NO_HEADER      (1u << 15)
#define SF_CODING         (1u << 16)
#define SF_DATA_ABSENT    (1u << 17) /* reserved: no supported decoder establishes this */
#define SF_IMPLEMENTED    0x1efffu
/* Reasons: 0 completed field, 1 rejected header, 2 long gap, 3 invalid code,
 * 4 capture end, 5 new sync. Lengths for partial fields are decoder bytes,
 * except Amiga where they are raw longwords; record byte 23 identifies the unit. */
