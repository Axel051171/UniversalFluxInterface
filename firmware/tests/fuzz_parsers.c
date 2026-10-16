#include "ufi_hfe_v3.h"
#include "ufi_write_codec.h"
#include "ufi_image.h"
#include "ufi_raw_verify.h"
#include "ufi_job.h"
#include "ufi_resources.h"
#include <stddef.h>
#include <assert.h>
#include <string.h>
typedef struct {const uint8_t* data;size_t size;} input_t;
static bool read_byte(void* p,uint32_t offset,uint8_t* byte){
    const input_t* in=p;if(offset>=in->size)return false;*byte=in->data[offset];return true;
}
int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size){
    if(size>32767)return 0;
    ufi_job_reset();ufi_resources_reset();ufi_resources_touch(0);
    for(size_t i=0;i<size && i<512;i++){
        ufi_job_t before=ufi_job;uint8_t h[16]={0};uint8_t owner=ufi_resources_owner();
        switch(data[i]&7){
        case 0:(void)ufi_job_open(data[i],(data[i]>>3)&3,(uint32_t)i);break;
        case 1:if(!ufi_job_accept(h,data+i,1,(uint32_t)i))assert(!memcmp(&before,&ufi_job,sizeof(before)));break;
        case 2:if(ufi_job_emit(h,data+i,1,(uint32_t)i)){assert(ufi_job.bytes==before.bytes+1 && ufi_job.packets==before.packets+1);}
               else assert(!memcmp(&before,&ufi_job,sizeof(before)));break;
        case 3:ufi_job_finish(data[i]>>3);assert(!ufi_job_live());break;
        case 4:ufi_job_reset();assert(!ufi_job_live());break;
        case 5:if(!ufi_resources_acquire(data[i]>>3))assert(ufi_resources_owner()==owner);break;
        case 6:if(!ufi_resources_release(data[i]>>3))assert(ufi_resources_owner()==owner);break;
        default:(void)ufi_resources_idle_expired((uint32_t)i*65536u,data[i]&8);break;
        }
        assert(ufi_resources_owner()<=UFI_OWNER_GW);
    }
    ufi_job_reset();ufi_resources_reset();
    input_t in={data,size};hfe_v3_report_t report;
    uint32_t out[514];out[0]=out[513]=0x13579bdf;
    (void)hfe_v3_decode(read_byte,&in,(uint32_t)size,NULL,0,&report);
    int ret=hfe_v3_decode(read_byte,&in,(uint32_t)size,out+1,512,&report);
    assert(out[0]==0x13579bdf && out[513]==0x13579bdf);
    if(!ret){
        uint64_t total=0;
        assert(report.flux_count>=3 && report.flux_count<=512 && !report.weak_cells && report.index_count<=1);
        for(uint32_t i=0;i<report.flux_count;i++){assert(out[i+1]>=220);total+=out[i+1];}
        assert(total<=report.ticks);
    }
    uint32_t full[512],parts[512];compact_dec_t a,b;
    compact_init(&a,full,512);compact_init(&b,parts,512);
    (void)compact_feed(&a,data,(uint32_t)size);
    for(size_t i=0;i<size;i++)(void)compact_feed(&b,data+i,1);
    assert(a.bad==b.bad && a.decoded==b.decoded && compact_complete(&a)==compact_complete(&b));
    assert(!memcmp(full,parts,a.decoded*sizeof(uint32_t)));
    if(size>=512){hfe_header_t header;(void)hfe_parse_header(data,&header);}
    if(size && size<=512){
        uint8_t encoded[1024];hfe_v3_encoder_t encoder;
        bool ok=hfe_v3_encode_begin(&encoder,encoded,sizeof(encoded),72);
        for(size_t i=0;i<size && ok;i++)ok=hfe_v3_encode_cells(&encoder,1,data[i]&1);
        if(ok && hfe_v3_encode_end(&encoder)){
            input_t output={encoded,encoder.length};
            assert(!hfe_v3_decode(read_byte,&output,encoder.length,NULL,0,&report));
            assert(report.cells==size && !report.weak_cells && report.index_count==1);
        }
        uint32_t deltas[512],times[512],period=0;
        for(size_t i=0;i<size;i++){deltas[i]=data[i]+1;period+=deltas[i];times[i]=period*2;}
        raw_profile_t x,y;uint8_t result[RAW_REPORT_SIZE];
        bool valid=raw_profile_build(&x,deltas,(uint32_t)size,period,false);
        assert(raw_profile_build(&y,times,(uint32_t)size,period*2,true)==valid);
        if(valid)assert(raw_profile_compare(&x,&y,1,result));
    }
    return 0;
}
