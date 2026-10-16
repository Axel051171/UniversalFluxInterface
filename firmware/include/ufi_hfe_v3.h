#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef bool (*hfe_v3_read_t)(void* context,uint32_t offset,uint8_t* byte);
typedef struct {
    uint32_t cells,ticks,flux_count,weak_cells,index_count,index_cell;
} hfe_v3_report_t;
/* Read raw on-disk (bit-reversed) bytes. NULL output inspects metadata only.
 * Errors match UFI: malformed -16, storage -13, unsupported -11, capacity -10,
 * timing -18. Length is bounded by the HFE track-table representation. */
int hfe_v3_decode(hfe_v3_read_t read,void* context,uint32_t length,
                  uint32_t* out,uint32_t capacity,hfe_v3_report_t* report);
typedef struct {uint8_t* out;uint32_t capacity,length;uint8_t value,bits;bool failed;} hfe_v3_encoder_t;
bool hfe_v3_encode_begin(hfe_v3_encoder_t* e,uint8_t* out,uint32_t capacity,uint8_t period);
bool hfe_v3_encode_cells(hfe_v3_encoder_t* e,uint32_t cells,bool transition);
bool hfe_v3_encode_end(hfe_v3_encoder_t* e);
