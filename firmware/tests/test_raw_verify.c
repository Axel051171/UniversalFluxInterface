#include "ufi_raw_verify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    uint32_t source[4096],jitter[4096],wrong[4096];
    for(unsigned i=0;i<4096;i++){source[i]=1000;jitter[i]=1000+(i&1?2:-2);wrong[i]=i&1?1500:500;}
    raw_profile_t a,b;uint8_t report[RAW_REPORT_SIZE];
    assert(raw_profile_build(&a,source,4096,4096000,false));
    raw_builder_t builder;
    assert(raw_profile_begin(&b,&builder,source,4096,4096000,false));
    assert(!raw_profile_step(&b,&builder,0) && !builder.edge && !b.valid);
    unsigned steps=0;int step;
    do{step=raw_profile_step(&b,&builder,256);steps++;}while(!step);
    assert(step==1 && steps==16 && !memcmp(&a,&b,sizeof a));
    assert(raw_profile_build(&b,jitter,4096,4096000,false));
    assert(raw_profile_compare(&a,&b,30,report) && report[2]==1 && report[3]==1);
    assert(raw_profile_build(&b,wrong,4096,4096000,false));
    assert(!raw_profile_compare(&a,&b,30,report) && report[2]==2); /* identical count and duration */
    for(unsigned i=0;i<4096;i++)jitter[i]=(i+1)*2000;
    assert(raw_profile_build(&b,jitter,4096,8192000,true));
    assert(raw_profile_compare(&a,&b,30,report)); /* uniform speed normalisation */
    jitter[2]=0;assert(!raw_profile_build(&b,jitter,4096,8192000,true));
    assert(!raw_profile_compare(&a,&b,30,report) && report[2]==4);
    assert(!raw_profile_build(&b,source,4096,0,false));
    uint32_t boundary[]={80,217,94},double_times[]={160,594,782};
    assert(raw_profile_build(&a,boundary,3,391,false));
    assert(raw_profile_build(&b,double_times,3,782,true));
    assert(raw_profile_compare(&a,&b,1,report)); /* fuzz regression: scaled window boundary */
    puts("raw timing profile: wrong timing with same count rejected, jitter/speed, bounds and unavailable passed");
}
