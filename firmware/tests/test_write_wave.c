#include "ufi_write_wave.h"
#include <assert.h>
#include <stdio.h>
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
    puts("test_write_wave: boundaries, silent segments and exact pulse times passed");
    return 0;
}
