#pragma once
#include <stdint.h>
#include <stdbool.h>
#define MEDIA_STATUS_SIZE 40u
#define MEDIA_CHECK_SIZE 32u
#define MEDIA_CHECK_RPM_MISMATCH 1u
#define MEDIA_CHECK_PROFILE_CONFLICT 2u
#define MEDIA_CHECK_INSUFFICIENT_EVIDENCE 4u
#define MEDIA_CHECK_LINE_CONFLICT 8u
#define MEDIA_CHECK_UNSUPPORTED_CONFIGURATION 16u
#define MEDIA_CHECK_ZONE_MISMATCH 32u
#define MEDIA_CHECK_ALIGNMENT_UNCERTAIN 64u
#define MEDIA_CHECK_HEADER_POSITION_MISMATCH 128u
void ufi_drive_port_report(uint8_t out[24]);
int ufi_drive_operating_profile(const uint8_t* input,uint8_t out[40]);
/* Reasons: step=1, geometry=2, format=4, rate=8, missing profile=16,
 * missing physical index=32, missing decode=64, missing expected RPM=128,
 * missing capture settings=256. */
void media_check(const uint8_t status[40], uint8_t step, uint8_t format,
                 uint32_t rate, uint32_t period, uint32_t timer_hz,
                 uint8_t out[MEDIA_CHECK_SIZE]);
typedef struct {
    uint32_t generation;
    uint8_t drive[8],media[12];
    bool position_valid,change_asserted;
    uint8_t reason;
} media_state_t;
void media_init(media_state_t* s,uint8_t max_track,uint8_t heads,bool change);
void media_invalidate(media_state_t* s,uint8_t reason);
bool media_observe(media_state_t* s,bool asserted);
bool media_drive_valid(const uint8_t p[8]);
bool media_profile_valid(const uint8_t p[12]);
void media_pack(const media_state_t* s,uint8_t drive,uint8_t track,uint8_t side,bool estimated,uint8_t out[40]);
uint32_t ufi_drive_media_token(void);
bool ufi_drive_media_matches(uint32_t token);
void ufi_drive_media_status(uint8_t out[40]);
int ufi_drive_profile_set(const uint8_t profile[8]);
int ufi_drive_media_set(const uint8_t profile[12]);
void ufi_drive_media_notify(void);
void ufi_drive_position_invalidate(void);
void ufi_drive_media_observed(uint8_t format,uint32_t rate);
