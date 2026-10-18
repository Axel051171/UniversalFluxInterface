#pragma once
#include <stdbool.h>
#include <stdint.h>
bool ufi_analysis_job_active(void);
void ufi_analysis_job_service(void);
void ufi_analysis_job_abort(void);
int ufi_analysis_command(const uint8_t* p,uint16_t n,uint8_t* out,uint16_t* size);
/* track survey (op 46, 4.15, ufi_survey.c): one revolution per track and side, table read while it runs */
bool ufi_survey_active(void);
void ufi_survey_service(void);
void ufi_survey_abort(void);
int ufi_survey_command(const uint8_t* p,uint16_t n,uint8_t* out,uint16_t* size);
