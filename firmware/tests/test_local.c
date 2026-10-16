#include "ufi_quality.h"
#include <assert.h>
#include <stdio.h>
static quality_t q;
static mfm_decoder_t dec;
static uint8_t report[QUALITY_LOCAL_SIZE];
static uint32_t get(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void setup(void){
    quality_init(&q,1,250000,1);q.generation=7;q.extra.options.flags=5;
    quality_begin(&q,&dec,0,NULL);quality_revolution(&q,0);q.revolution_ticks[0]=55000000;
}
static void feed(unsigned bin,unsigned cell,bool diverse){
    for(unsigned i=0;i<100;i++){
        q.extra.position=(bin+1)*1000000+i*3000;
        /* Explicit fixed window center, independent of data-run distribution. */
        q.extra.position=(uint32_t)(((uint64_t)(2*bin+1)*55000000)/64);
        quality_interval(&q,cell*(diverse?2+i%2:2));
    }
}
int main(void){
    setup();feed(0,550,true);feed(1,550,true);feed(2,550,true);feed(3,572,true);
    feed(4,550,false);quality_end_revolution(&q);
    assert(quality_local_pack(&q,report,sizeof(report))==sizeof(report));
    assert((report[32]&3)==1 && (report[32+120]&3)==3);
    assert(!(report[32+160]&1)); /* repeated single run length cannot establish a clock */
    assert(get(report+32+120+8)==572*256 && get(report+24)==550*256);
    q.ambiguous=1;quality_local_pack(&q,report,sizeof(report));assert(!(report[152]&2));
    setup();for(unsigned i=0;i<4;i++)feed(i,572,true);
    quality_end_revolution(&q);quality_local_pack(&q,report,sizeof(report));
    for(unsigned i=0;i<4;i++)assert(!(report[32+40*i]&2)); /* uniform RPM/rate offset */
    setup();q.extra.position=10;quality_interval(&q,1100);assert(!q.local[0].count && q.local[0].rejected==1);
    q.extra.position=10000;quality_interval(&q,UINT32_MAX);assert(q.local[0].rejected==2);
    quality_local_pack(&q,report,sizeof(report));assert(report[32]&128);
    assert(!quality_local_pack(&q,report,sizeof(report)-1));
    setup();q.extra.options.flags=1;feed(0,550,true);assert(!q.local[0].count);
    puts("local analysis: rate change, uniform speed, diversity, ambiguity, index crossing, bounds and opt-in passed");
}
