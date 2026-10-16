#include "ufi_compare.h"
#include <string.h>
void track_compare_init(track_compare_t* c,uint32_t generation,uint32_t tolerance,uint32_t period){
    memset(c,0,sizeof(*c));c->generation=generation;c->tolerance=tolerance;
    c->reference_period=c->min_period=c->max_period=period;
}
static void difference(track_compare_t* c,uint32_t position,uint8_t rev,bool extra){
    unsigned bin=(unsigned)((uint64_t)position*COMPARE_WINDOWS/c->reference_period);
    if(bin>=COMPARE_WINDOWS)bin=COMPARE_WINDOWS-1;
    compare_window_t* w=&c->windows[bin];uint32_t* n=extra?&w->extra:&w->missing;
    if(*n!=UINT32_MAX)(*n)++;
    w->revolutions|=(uint8_t)(1u<<rev);
}
bool track_compare_step(track_compare_t* c,const uint32_t* a,uint32_t na,
                        const uint32_t* b,uint32_t nb,uint32_t period,uint8_t rev,unsigned budget){
    if(!period || !c->reference_period || rev<1 || rev>7)return false;
    if(period<c->min_period)c->min_period=period;
    if(period>c->max_period)c->max_period=period;
    while(budget-- && (c->i<na || c->j<nb)){
        uint32_t x=c->i<na?a[c->i]:0;
        uint32_t y=c->j<nb?(uint32_t)((uint64_t)b[c->j]*c->reference_period/period):0;
        if(c->i<na && c->j<nb && (x>y?x-y:y-x)<=c->tolerance){c->i++;c->j++;}
        else if(c->i<na && (c->j==nb || x<y)){difference(c,x,rev,false);c->i++;}
        else {difference(c,y,rev,true);c->j++;}
    }
    if(c->i<na || c->j<nb)return false;
    c->completed|=(uint8_t)(1u<<rev);c->i=c->j=0;return true;
}
static void put(uint8_t* p,uint32_t v){for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(v>>(8*k));}
uint16_t track_compare_pack(const track_compare_t* c,uint8_t* out,uint16_t capacity){
    if(capacity<COMPARE_WIRE_SIZE || !c->generation)return 0;
    memset(out,0,COMPARE_WIRE_SIZE);out[0]=1;out[1]=COMPARE_WINDOWS;out[2]=c->completed;
    put(out+4,c->generation);put(out+8,c->tolerance);put(out+12,c->reference_period);
    put(out+16,c->min_period);put(out+20,c->max_period);
    for(unsigned k=0;k<COMPARE_WINDOWS;k++){
        uint8_t* p=out+32+12*k;put(p,c->windows[k].missing);put(p+4,c->windows[k].extra);p[8]=c->windows[k].revolutions;
        if(c->windows[k].missing)out[24]|=1;
        if(c->windows[k].extra)out[24]|=2;
    }
    out[28]=1; /* alignment unverified; differences are not a weak-bit verdict */
    return COMPARE_WIRE_SIZE;
}
