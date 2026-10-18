#include "ufi_write_wave.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
int main(void)
{
    const uint32_t values[] = {220,344,65535,65536,65600,131070,UINT32_MAX};
    uint32_t parts; uint64_t duration;
    assert(wave_plan(values,7,&parts,&duration));
    wave_cursor_t c; wave_init(&c,values,7);
    wave_item_t item; uint64_t sum=0, gap=0; unsigned pulses=0, actual=0;
    while (wave_next(&c,&item)) {
        uint32_t period=(uint32_t)item.arr+1;
        sum+=period; gap+=period; actual++;
        assert(period<=65535 && item.reserved==0);
        if (item.ccr<period) {
            assert(period-item.ccr==110);
            /* Falling edge is CCR ticks after segment start, not the update. */
            assert(gap==values[pulses]+(pulses==0?WAVE_PULSE_TICKS:0u));
            pulses++;gap=0;
        } else assert(item.ccr==period);
    }
    assert(sum==duration && pulses==7 && actual==parts && !gap);
    const uint32_t largest[]={UINT32_MAX};
    assert(wave_plan(largest,1,&parts,&duration));
    wave_init(&c,largest,1); sum=0; actual=0; pulses=0;
    while(wave_next(&c,&item)) {
        sum+=(uint32_t)item.arr+1; actual++;
        if(item.ccr<(uint32_t)item.arr+1) {
            pulses++; assert(sum-WAVE_PULSE_TICKS==UINT32_MAX);
        }
    }
    assert(pulses==1 && sum==duration && actual==parts);
    const uint32_t bad[]={219}; assert(!wave_plan(bad,1,&parts,&duration));
    const uint32_t short_track[]={220,1100,550};
    wave_init(&c,short_track,3);c.lead=55000;c.tail=27500;sum=0;pulses=0;
    const uint64_t edges[]={55220,56320,56870};
    while(wave_next(&c,&item)){
        if(item.ccr<(uint32_t)item.arr+1){assert(sum+item.ccr==edges[pulses]);pulses++;}
        sum+=(uint32_t)item.arr+1;
    }
    assert(pulses==3 && sum==56870+110+27500);
    /* weak-cell markers (4.16): jitter moves one transition and keeps the later ones in place, drop
     * merges two intervals, the last interval is never touched, same seed -> same pattern */
    {
        const uint32_t src[6]={1100|WAVE_WEAK_JITTER,1100,1100|WAVE_WEAK_DROP,230|WAVE_WEAK_JITTER,1100,1100|WAVE_WEAK_JITTER|WAVE_WEAK_DROP};
        uint32_t wk[6],wk2[6],applied=0,applied2=0;
        memcpy(wk,src,sizeof wk);memcpy(wk2,src,sizeof wk2);
        const uint32_t n=wave_weak_apply(wk,6,500,0x12345678u,&applied);
        uint64_t total=0;
        for(uint32_t i=0;i<n;i++){total+=wk[i];assert(wk[i]>=WAVE_MIN_TICKS && !(wk[i]&~WAVE_WEAK_VALUE));}
        assert(total==5730 && n>=5 && n<=6 && applied<=3 && applied>=6-n);
        assert(wk[n-1]==1100);                                         /* last interval: markers cleared, value kept */
        assert(wave_weak_apply(wk2,6,500,0x12345678u,&applied2)==n && applied2==applied && !memcmp(wk,wk2,n*sizeof *wk));
        uint32_t tight[4]={220|WAVE_WEAK_JITTER,220|WAVE_WEAK_JITTER,220|WAVE_WEAK_JITTER,220};
        assert(wave_weak_apply(tight,4,500,9,&applied)==4 && !applied && tight[0]==220 && tight[1]==220 && tight[2]==220);   /* no room: offsets clamp to 0 */
        uint32_t off[3]={1100|WAVE_WEAK_JITTER,1100|WAVE_WEAK_JITTER,1100|WAVE_WEAK_DROP};
        assert(wave_weak_apply(off,3,0,7,&applied)==3 && !applied && off[0]==1100 && off[1]==1100 && off[2]==1100);        /* jitter 0: markers inert, last drop ignored */
        uint32_t plain[3]={1000,2000,3000};
        assert(wave_weak_apply(plain,3,500,1,&applied)==3 && !applied && plain[1]==2000);
        assert(wave_weak_apply(NULL,3,500,1,&applied)==0 && !applied && wave_weak_apply(plain,0,500,1,&applied)==0);
        static uint32_t many[201];
        for(unsigned i=0;i<200;i++)many[i]=1000|WAVE_WEAK_DROP;
        many[200]=1000;total=0;
        const uint32_t kept=wave_weak_apply(many,201,0,42,&applied);
        for(uint32_t i=0;i<kept;i++)total+=many[i];
        assert(applied==201-kept && applied>60 && applied<140 && total==201000);   /* about half of the marked transitions vanish */
        uint32_t seeded[3]={1100|WAVE_WEAK_JITTER,1100,1100},seeded0[3]={1100|WAVE_WEAK_JITTER,1100,1100};
        assert(wave_weak_apply(seeded,3,500,0,&applied)==3 && wave_weak_apply(seeded0,3,500,0x9E3779B9u,&applied2)==3 && seeded[0]==seeded0[0]);   /* seed 0 = the default stream */
    }
    puts("test_write_wave: boundaries, silent segments and exact pulse times, weak-cell markers passed");
    return 0;
}
