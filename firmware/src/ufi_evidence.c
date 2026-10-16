#include "ufi_evidence.h"
#include <string.h>
static uint8_t context[EVIDENCE_CONTEXT_SIZE];
static uint8_t media_context[40];
void evidence_set_media(const uint8_t status[40]){memcpy(media_context,status,40);}
/* CPU-only SRAM1; NOLOAD records are published only after every byte was written. */
__attribute__((section(".evidence"),aligned(32))) static uint8_t index_records[EVIDENCE_INDEX_MAX][16],trace_records[EVIDENCE_TRACE_MAX][24];
static volatile uint32_t index_count,index_dropped;
static uint32_t trace_count,trace_dropped,generation;
static volatile uint16_t flags;
static volatile bool active;
static void put(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void evidence_begin(const uint8_t c[EVIDENCE_CONTEXT_SIZE],uint16_t f){
    active=false;memcpy(context,c,sizeof(context));flags=f&EVIDENCE_MASK;
    memset(media_context,0,sizeof(media_context));
    index_count=index_dropped=trace_count=trace_dropped=0;
    if(!++generation)generation=1;
    put(context+4,generation);context[2]=(uint8_t)flags;active=true;
}
uint32_t evidence_id(void){return generation;}
uint16_t evidence_flags(void){return active?flags:0;}
void evidence_end(void){active=false;}
void evidence_index(uint64_t ticks,uint32_t pos,uint32_t reason){
    if(!active || !(flags&EVIDENCE_RAW_INDEX))return;
    uint32_t n=index_count;
    if(n==EVIDENCE_INDEX_MAX){if(index_dropped!=UINT32_MAX)index_dropped++;return;}
    uint8_t* p=index_records[n];put(p,(uint32_t)ticks);put(p+4,(uint32_t)(ticks>>32));put(p+8,pos);put(p+12,reason);
    index_count=n+1;
}
void evidence_trace(uint32_t ms,uint32_t produced,uint32_t pending,uint32_t submitted,uint32_t reason){
    if(!active || !(flags&EVIDENCE_TRANSFER))return;
    if(trace_count==EVIDENCE_TRACE_MAX){if(trace_dropped!=UINT32_MAX)trace_dropped++;return;}
    uint8_t* p=trace_records[trace_count++];put(p,ms);put(p+4,produced);put(p+8,pending);
    put(p+12,submitted);put(p+16,reason);put(p+20,0);
}
uint16_t evidence_pack(uint8_t section,uint8_t* out,uint16_t capacity){
    if(!generation)return 0;
    if(section==0){if(capacity<sizeof(context))return 0;memcpy(out,context,sizeof(context));return sizeof(context);}
    if(section==3){if(capacity<sizeof(media_context))return 0;memcpy(out,media_context,sizeof(media_context));return sizeof(media_context);}
    /* Bounded sections fit the existing 4096-byte USB scratch buffer. */
    uint32_t count=section==1?index_count:trace_count;
    unsigned width=section==1?16:24;
    if(section>2 || capacity<32+count*width)return 0;
    memset(out,0,32);out[0]=1;out[1]=section;out[2]=(uint8_t)flags;out[3]=active;
    put(out+4,generation);put(out+8,count);put(out+12,section==1?index_dropped:trace_dropped);
    put(out+16,width);put(out+20,section==1?EVIDENCE_INDEX_MAX:EVIDENCE_TRACE_MAX);
    memcpy(out+32,section==1?(const void*)index_records:(const void*)trace_records,count*width);
    return (uint16_t)(32+count*width);
}
