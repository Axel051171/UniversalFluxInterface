#pragma once
#include <stdint.h>
#include <stdbool.h>
#define EVIDENCE_RAW_INDEX 8u
#define EVIDENCE_TRANSFER 16u
#define EVIDENCE_MASK 24u
#define EVIDENCE_INDEX_MAX 240u
#define EVIDENCE_TRACE_MAX 48u
#define EVIDENCE_CONTEXT_SIZE 160u
/* Single IRQ producer for index records; reports are read only after capture stops. */
void evidence_begin(const uint8_t context[EVIDENCE_CONTEXT_SIZE],uint16_t flags);
void evidence_index(uint64_t ticks,uint32_t dma_position,uint32_t flags);
void evidence_trace(uint32_t ms,uint32_t produced,uint32_t pending,uint32_t submitted,uint32_t flags);
void evidence_end(void);
void evidence_set_media(const uint8_t status[40]);
uint32_t evidence_id(void);
uint16_t evidence_pack(uint8_t section,uint8_t* out,uint16_t capacity);
uint16_t evidence_flags(void);
void ufi_evidence_arm(uint16_t flags);
void ufi_evidence_disarm(void);
void ufi_evidence_begin(uint8_t track,uint8_t side,uint8_t revs,uint32_t period);
void ufi_evidence_service(void);
void ufi_evidence_usb(uint32_t bytes,bool blocked);
void ufi_evidence_finish(void);
void ufi_evidence_config(uint16_t flags);
uint16_t ufi_evidence_configured(void);
