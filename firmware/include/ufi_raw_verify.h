#pragma once
#include <stdint.h>
#include <stdbool.h>
#define RAW_WINDOWS 64u
#define RAW_REPORT_SIZE (32u+RAW_WINDOWS*4u)
typedef struct {uint16_t bins[RAW_WINDOWS][8];uint32_t period,count;bool valid;} raw_profile_t;
bool raw_profile_build(raw_profile_t* p,const uint32_t* flux,uint32_t count,uint32_t period,bool timestamps);
bool raw_profile_compare(const raw_profile_t* source,const raw_profile_t* observed,
                         uint16_t tolerance,uint8_t report[RAW_REPORT_SIZE]);
int ufi_raw_verify_options(const uint8_t* request,uint16_t size,uint8_t* out);
uint16_t ufi_raw_verify_report(uint8_t* out);
