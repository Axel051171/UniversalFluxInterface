#pragma once
#include <stdint.h>
#include <stdbool.h>
bool ufi_precision_enabled(void);
int ufi_precision_options(const uint8_t* request,uint16_t size,uint8_t* out);
int ufi_precision_source(const uint32_t* deltas,uint32_t count,uint32_t period,uint32_t offset);
void ufi_precision_reset(void);
int ufi_precision_verify(void);
int ufi_precision_verify_begin(void);
int ufi_precision_verify_step(void);
void ufi_precision_verify_cancel(void);
uint16_t ufi_precision_report(uint8_t first,uint8_t count,uint8_t* out);
