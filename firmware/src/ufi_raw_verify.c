#include "ufi_raw_verify.h"
#include <string.h>
static void put(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
bool raw_profile_build(raw_profile_t* p,const uint32_t* flux,uint32_t count,uint32_t period,bool timestamps){
    memset(p,0,sizeof(*p));p->period=period;p->count=count;
    if(!period || !flux || count<3)return false;
    uint64_t position=0;
    for(uint32_t i=0;i<count;i++){
        uint64_t next=timestamps?flux[i]:position+flux[i];
        if(next<=position || next>period)return false;
        uint32_t scaled=(uint32_t)((next-position)*1048576u/period);
        unsigned bin=0,threshold=6;
        while(scaled>threshold && bin<7){threshold*=2;bin++;}
        unsigned window=(unsigned)((next*RAW_WINDOWS-1)/period);
        if(p->bins[window][bin]==UINT16_MAX)return false;
        p->bins[window][bin]++;position=next;
    }
    p->valid=true;return true;
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
