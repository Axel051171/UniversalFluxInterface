#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint16_t density_us,motor_step_ms,write_settle_us,gate_lead_us,gate_tail_us,reserved;
} drive_extra_t;
bool drive_extra_valid(const drive_extra_t* t);
drive_extra_t ufi_drive_extra_get(void);
bool ufi_drive_extra_set(const drive_extra_t* t);
