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
/* Conservative session-local invalidation epoch, not a hash of settings. */
void ufi_config_changed(void);
uint32_t ufi_config_generation(void);
void ufi_capability_matrix(const uint8_t port[24], bool busy, uint8_t out[UFI_CAP_MATRIX_SIZE]);
