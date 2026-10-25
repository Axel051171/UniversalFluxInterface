#include "ufi_raw_verify.h"
#include <string.h>
static void put(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
bool raw_profile_begin(raw_profile_t* p,raw_builder_t* b,const uint32_t* flux,uint32_t count,uint32_t period,bool timestamps){
    if(!p || !b)return false;
    memset(p,0,sizeof(*p));p->period=period;p->count=count;
    memset(b,0,sizeof *b);b->flux=flux;b->timestamps=timestamps;
    b->failed=!period || !flux || count<3;return !b->failed;
}
int raw_profile_step(raw_profile_t* p,raw_builder_t* b,uint32_t budget){
    if(!p || !b || b->failed)return -1;
    if(p->valid)return 1;
    while(b->edge<p->count && budget--){
        uint64_t next=b->timestamps?b->flux[b->edge]:(uint64_t)b->previous+b->flux[b->edge];
        if(next<=b->previous || next>p->period){b->failed=true;return -1;}
        uint32_t scaled=(uint32_t)((next-b->previous)*1048576u/p->period);
        unsigned bin=0,threshold=6;
        while(scaled>threshold && bin<7){threshold*=2;bin++;}
        unsigned window=(unsigned)((next*RAW_WINDOWS-1)/p->period);
        if(p->bins[window][bin]==UINT16_MAX){b->failed=true;return -1;}
        p->bins[window][bin]++;b->previous=(uint32_t)next;b->edge++;
    }
    p->valid=b->edge==p->count;return p->valid?1:0;
}
bool raw_profile_build(raw_profile_t* p,const uint32_t* flux,uint32_t count,uint32_t period,bool timestamps){
    raw_builder_t builder;
    if(!raw_profile_begin(p,&builder,flux,count,period,timestamps))return false;
    int ret;do{ret=raw_profile_step(p,&builder,256);}while(!ret);
    return ret>0;
}
bool raw_profile_compare(const raw_profile_t* a,const raw_profile_t* b,uint16_t tolerance,uint8_t report[RAW_REPORT_SIZE]){
    memset(report,0,RAW_REPORT_SIZE);report[0]=1;report[1]=RAW_WINDOWS;
    put(report+4,a->period);put(report+8,b->period);put(report+12,a->count);put(report+16,b->count);
    report[24]=(uint8_t)tolerance;report[25]=(uint8_t)(tolerance>>8);
    /* Explicitly a lossy timing/density fingerprint, never a content verdict. */
    report[3]=1;
    if(!a->valid || !b->valid || !tolerance || tolerance>1000){report[2]=4;return false;}
    uint64_t distance=0;
    for(unsigned w=0;w<RAW_WINDOWS;w++){
        uint32_t d=0;for(unsigned k=0;k<8;k++){
            uint16_t x=a->bins[w][k],y=b->bins[w][k];d+=x>y?x-y:y-x;
        }
        put(report+32+4*w,d);distance+=d;
    }
    put(report+20,(uint32_t)distance);
    bool match=distance*1000u<=(uint64_t)tolerance*(a->count+b->count);
    report[2]=match?1:2;return match;
}
