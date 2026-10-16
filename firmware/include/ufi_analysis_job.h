#pragma once
#include <stdbool.h>
#include <stdint.h>
bool ufi_analysis_job_active(void);
void ufi_analysis_job_service(void);
void ufi_analysis_job_abort(void);
int ufi_analysis_command(const uint8_t* p,uint16_t n,uint8_t* out,uint16_t* size);
