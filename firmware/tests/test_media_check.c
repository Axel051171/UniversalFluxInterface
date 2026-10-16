#include "ufi_media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void){
    /* Standard operating combinations, including 360 KB in a 1.2 MB drive. */
    const unsigned cases[][5]={{40,39,1,300,250000},{40,79,2,360,300000},
        {80,79,1,300,250000},{80,79,1,300,500000},{80,79,1,360,500000}};
    uint8_t s[40],out[32];media_state_t state;
    for(unsigned i=0;i<5;i++){
        media_init(&state,(uint8_t)cases[i][1],2,true);
        state.drive[4]=(uint8_t)cases[i][3];state.drive[5]=(uint8_t)(cases[i][3]>>8);
        uint8_t m[12]={1,0,2,9,2,0,1,0};m[1]=(uint8_t)cases[i][0];m[5]=(uint8_t)cases[i][2];put(m+8,cases[i][4]);
        assert(media_profile_valid(m));memcpy(state.media,m,12);media_pack(&state,1,0,0,false,s);
        uint32_t period=(uint32_t)(275000000ull*60/cases[i][3]);
        media_check(s,m[5],1,cases[i][4],period,275000000,out);assert(!rd(out+4));
        media_check(s,m[5],1,cases[i][4],period+period/10,275000000,out);assert(rd(out+4)==1);
    }
    media_check(s,2,2,250000,55000000,275000000,out);
    assert((rd(out+4)&3)==3 && (rd(out+8)&15)==15);
    media_check(s,0,0,0,0,275000000,out);assert(rd(out+4)==4 && (rd(out+8)&256));
    s[2]=0;media_check(s,1,0,0,0,275000000,out);assert(rd(out+4)==4);
    /* Synthetic/missing index is represented by period zero, never RPM mismatch. */
    s[2]=8;s[20]=5;put(s+28,250000);
    media_check(s,1,5,307692,0,275000000,out);assert(rd(out+4)==4 && !(rd(out+8)&8));
    s[16]=0;s[17]=0;media_check(s,1,5,307692,UINT32_MAX,275000000,out);assert(rd(out+4)==4);
    uint8_t mac[12]={6,80,2,0,2,1,1,1};assert(media_profile_valid(mac));
    memcpy(s+20,mac,12);s[2]=9;s[8]=32;s[16]=0x2c;s[17]=1;
    media_check(s,1,6,312500,55000000,275000000,out);assert(!rd(out+4));
    media_check(s,1,6,380228,55000000,275000000,out);assert(rd(out+4)==32 && (rd(out+8)&2048));
    s[2]=8;media_check(s,1,6,312500,55000000,275000000,out);assert(rd(out+4)==4 && (rd(out+8)&512));
    mac[7]=2;assert(!media_profile_valid(mac));
    puts("media check: operating matrix, conflicts, missing evidence, zoned rates and RPM passed");
}
