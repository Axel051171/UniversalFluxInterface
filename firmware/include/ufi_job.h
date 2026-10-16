#pragma once
#include <stdbool.h>
#include <stdint.h>
#define UFI_JOB_HEADER 16u
enum { JOB_IDLE, JOB_OPEN, JOB_ACTIVE, JOB_READY, JOB_DONE, JOB_FAILED };
typedef struct {
    uint64_t id;
    uint32_t packets, bytes, crc, last_ms;
    uint8_t kind, state, status;
} ufi_job_t;
extern ufi_job_t ufi_job;
uint32_t ufi_job_crc(uint32_t crc, const uint8_t *p, uint32_t n);
bool ufi_job_open(uint64_t id, uint8_t kind, uint32_t now);
bool ufi_job_accept(const uint8_t *header, const uint8_t *data, uint32_t n, uint32_t now);
bool ufi_job_emit(uint8_t *header, const uint8_t *data, uint32_t n, uint32_t now);
bool ufi_job_live(void);
void ufi_job_finish(uint8_t status);
void ufi_job_reset(void);
