#include "ufi_protection.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>
static quality_t quality;
static protection_state_t evidence;
static sector_details_t details;
static mfm_decoder_t decoder;
static track_compare_t comparison;
static uint8_t payload[1024];
int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size){
    if(size<8 || size>2048)return 0;
    protection_options_t options={(uint16_t)(data[1]&63), (uint16_t)(10+data[4]%241),
                                  (uint8_t)(2+data[2]%7),(uint8_t)(2+data[3]%7)};
    protection_options_t decoded=options,before=decoded;
    if(!protection_options_decode(data,8,&decoded))assert(!memcmp(&decoded,&before,sizeof decoded));
    quality_init(&quality,(uint8_t)(1+data[0]%7),250000,(uint8_t)(1+data[5]%8));
    quality.generation=1;quality_details_attach(&quality,&details);
    protection_begin(&evidence,&quality,&options);quality_begin(&quality,&decoder,0,NULL);
    for(unsigned r=0;r<quality.revs;r++)quality.revolution_ticks[r]=8000000;
    for(unsigned i=0;i<sizeof(payload);i++)payload[i]=data[i%size];
    uint32_t position=0;
    for(size_t i=8;i<size && i<520;i++){
        quality.revolution=(uint8_t)(data[i]%quality.revs);quality.profile=(uint8_t)((data[i]>>7)&1u);
        uint32_t ticks=(uint32_t)(data[i]+1)*16;position+=ticks;quality.extra.position=position;
        protection_interval(&quality,ticks);
        if(!(i%4)){
            decoder.sector_flags=(uint8_t)(data[i]&31);decoder.field_bad=(data[i]&32)!=0;
            mfm_id_t id={data[i],data[i]&1u,data[(i+1)%size],(data[i]>>5)&3u};
            decoder.cb(decoder.ctx,&id,payload,(data[i]&64)!=0);
        }
    }
    quality.extra.completed=(uint8_t)((1u<<quality.revs)-1u);
    if(data[6]&1)quality.status=6;
    track_compare_init(&comparison,1,3,8000000);comparison.completed=(uint8_t)(quality.extra.completed&254);
    comparison.windows[data[6]%128].missing=data[7];comparison.windows[data[6]%128].revolutions=comparison.completed;
    protection_finish(&quality,(data[6]&2)?&comparison:NULL);
    uint32_t dropped=evidence.dropped;uint8_t count=evidence.count;
    protection_finish(&quality,(data[6]&2)?&comparison:NULL);
    if(quality.protection){assert(evidence.count==count && evidence.dropped==dropped);assert(evidence.count<=48);}
    uint8_t out[290];memset(out,0xa5,sizeof out);
    uint16_t capacity=(uint16_t)((data[6]|(uint16_t)data[7]<<8)%289);
    uint16_t n=protection_pack(&quality,(uint8_t)(data[6]%66),(uint8_t)(1+data[7]%10),out,capacity);
    assert(n<=capacity && out[288]==0xa5 && out[289]==0xa5);
    return 0;
}
