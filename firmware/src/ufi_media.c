#include "ufi_media.h"
#include <string.h>
static uint32_t serial;
static uint32_t get32(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void media_check(const uint8_t s[40],uint8_t step,uint8_t format,uint32_t rate,
                 uint32_t period,uint32_t hz,uint8_t out[MEDIA_CHECK_SIZE]){
    uint32_t flags=0,reasons=0;
    const uint8_t* d=s+12;const uint8_t* m=s+20;
    uint32_t rpm=d[4]|(uint32_t)d[5]<<8;
    bool profile=(s[2]&8)!=0;
    if(!profile)reasons|=16;
    else {
        if(step && m[5]!=step)reasons|=1;
        if((step && m[1] && (unsigned)(m[1]-1)*step>d[1]) || m[2]>d[2])reasons|=2;
        if(format && m[0]!=format)reasons|=4;
        /* Zoned GCR rates describe one track only, never the whole disk. */
        if(format && rate && m[0]!=5 && !(m[0]==6 && m[7]) && get32(m+8)!=rate)reasons|=8;
        if(m[0]==6 && m[7]==1){
            static const uint32_t rates[]={380228,346021,312500,280112,251256};
            if(!step || !(s[2]&1) || s[8]/step>=80)reasons|=512;
            else {
                unsigned zone=(s[8]/step)/16;
                if((format==6 && rate && rate!=rates[zone]) || (m[3] && m[3]!=12-zone)){
                    flags|=MEDIA_CHECK_ZONE_MISMATCH;reasons|=2048;
                }
            }
            if(rpm && rpm!=300)reasons|=1024;
        }
    }
    if(!period || !hz)reasons|=32;
    if(!format || !rate)reasons|=64;
    if(!rpm)reasons|=128;
    if(!step)reasons|=256;
    if(reasons&15)flags|=MEDIA_CHECK_PROFILE_CONFLICT;
    if(reasons&496)flags|=MEDIA_CHECK_INSUFFICIENT_EVIDENCE;
    if(reasons&512)flags|=MEDIA_CHECK_INSUFFICIENT_EVIDENCE;
    if(reasons&1024)flags|=MEDIA_CHECK_PROFILE_CONFLICT;
    /* Compare period against expected RPM with a fixed 20 per mille tolerance.
     * Integer cross-products avoid rounded RPM and fit in uint64_t. */
    if(period && hz && rpm){
        uint64_t actual=(uint64_t)period*rpm,expected=(uint64_t)hz*60;
        uint64_t delta=actual>expected?actual-expected:expected-actual;
        if(delta*1000>expected*20)flags|=MEDIA_CHECK_RPM_MISMATCH;
    }
    memset(out,0,MEDIA_CHECK_SIZE);out[0]=1;out[1]=step;out[2]=format;out[3]=20;
    put32(out+4,flags);put32(out+8,reasons);put32(out+12,get32(s+4));
    put32(out+16,rpm);put32(out+20,period);put32(out+24,hz);put32(out+28,rate);
}
static uint32_t next(void){if(!++serial)serial=1;return serial;}
void media_invalidate(media_state_t* s,uint8_t reason){
    s->generation=next();memset(s->media,0,sizeof(s->media));s->reason=reason;
}
void media_init(media_state_t* s,uint8_t max_track,uint8_t heads,bool change){
    memset(s,0,sizeof(*s));s->drive[0]=1;s->drive[1]=max_track;s->drive[2]=heads;s->drive[3]=change;
    media_invalidate(s,0);
}
bool media_observe(media_state_t* s,bool asserted){
    asserted=asserted && s->drive[3];bool changed=asserted && !s->change_asserted;
    s->change_asserted=asserted;if(changed)media_invalidate(s,1);return changed;
}
bool media_drive_valid(const uint8_t p[8]){
    unsigned rpm=p[4]|(unsigned)p[5]<<8;
    return p[0]==1 && p[1]>=6 && p[1]<=83 && p[2]>=1 && p[2]<=2 && p[3]<=1 &&
           (!rpm || rpm==300 || rpm==360) && !p[6] && !p[7];
}
bool media_profile_valid(const uint8_t p[12]){
    uint32_t rate=p[8]|(uint32_t)p[9]<<8|(uint32_t)p[10]<<16|(uint32_t)p[11]<<24;
    if(!p[0]){for(unsigned i=1;i<12;i++)if(p[i])return false;return true;}
    bool valid_rate=p[0]<=2?(rate==250000 || rate==300000 || rate==500000 || rate==1000000):
                    p[0]==3?(rate==125000 || rate==150000 || rate==250000 || rate==300000 || rate==500000):
                    p[0]==4?rate==500000:p[0]==5?(rate==250000 || rate==266667 || rate==285714 || rate==307692):
                    p[7]==1?rate==0:(rate==500000 || rate==380228 || rate==346021 || rate==312500 || rate==280112 || rate==251256);
    return p[0]<=6 && p[1]<=(p[0]==6?80:84) && p[2]<=2 && p[3]<=64 && p[4]<=3 &&
           (p[5]==1 || p[5]==2) && p[6]==1 && (p[0]==6?p[7]<=1:!p[7]) && valid_rate;
}
void media_pack(const media_state_t* s,uint8_t drive,uint8_t track,uint8_t side,bool estimated,uint8_t out[40]){
    memset(out,0,40);out[0]=1;out[1]=drive;
    out[2]=(s->position_valid?1:0)|(s->drive[3]?2:0)|(s->change_asserted?4:0)|(s->media[0]?8:0)|(estimated?16:0);
    for(unsigned i=0;i<4;i++)out[4+i]=(uint8_t)(s->generation>>(8*i));
    out[8]=track;out[9]=side;memcpy(out+12,s->drive,8);memcpy(out+20,s->media,12);out[32]=s->reason;
}
