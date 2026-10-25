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
        if(c->i<na && c->j<nb && (x>y?x-y:y-x)<=c->tolerance){
            if(c->stats){
                unsigned bin=(unsigned)((uint64_t)x*COMPARE_WINDOWS/c->reference_period);
                if(bin>=COMPARE_WINDOWS)bin=COMPARE_WINDOWS-1;
                compare_stats_t* s=&c->stats[bin];uint32_t d=x>y?x-y:y-x;
                if(s->matched!=UINT32_MAX){s->matched++;s->absolute+=d;s->signed_sum+=(int64_t)y-x;}
                if(d>s->maximum)s->maximum=d;
                s->revs|=(uint8_t)(1u<<rev);
            }
            c->i++;c->j++;
        }
        else if(c->i<na && (c->j==nb || x<y)){difference(c,x,rev,false);c->i++;}
        else {difference(c,y,rev,true);c->j++;}
    }
    if(c->i<na || c->j<nb)return false;
    c->completed|=(uint8_t)(1u<<rev);c->i=c->j=0;return true;
}
static void put(uint8_t* p,uint32_t v){for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(v>>(8*k));}
void track_compare_stats_attach(track_compare_t* c,compare_stats_t storage[COMPARE_WINDOWS]){
    c->stats=storage;if(storage)memset(storage,0,COMPARE_WINDOWS*sizeof(*storage));
}
uint16_t track_compare_stats_pack(const track_compare_t* c,uint8_t first,uint8_t count,uint8_t* out,uint16_t capacity){
    if(!c->generation || !c->stats || first>COMPARE_WINDOWS || !count || count>8)return 0;
    if(count>COMPARE_WINDOWS-first)count=(uint8_t)(COMPARE_WINDOWS-first);
    uint16_t size=(uint16_t)(32u+32u*count);if(capacity<size)return 0;
    memset(out,0,size);out[0]=1;out[1]=COMPARE_WINDOWS;out[2]=first;out[3]=count;
    put(out+4,c->generation);put(out+8,c->tolerance);put(out+12,c->reference_period);
    out[16]=c->completed;out[17]=1; /* index alignment only, no weak-bit verdict */
    for(unsigned k=0;k<count;k++){
        const compare_stats_t* s=&c->stats[first+k];const compare_window_t* w=&c->windows[first+k];
        uint8_t* p=out+32+32*k;put(p,s->matched);put(p+4,w->missing);put(p+8,w->extra);
        put(p+12,s->matched?(uint32_t)(s->absolute/s->matched):0);put(p+16,s->maximum);
        put(p+20,s->matched?(uint32_t)(s->signed_sum/(int64_t)s->matched):0);
        p[24]=s->revs;p[25]=w->revolutions;
    }
    return size;
}
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
