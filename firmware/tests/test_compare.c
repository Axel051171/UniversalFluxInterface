#include "ufi_compare.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static track_compare_t c;
static uint8_t wire[COMPARE_WIRE_SIZE+1];
static void compare(const uint32_t* a,unsigned na,const uint32_t* b,unsigned nb,uint32_t period,uint8_t rev){
    unsigned slices=0;
    while(!track_compare_step(&c,a,na,b,nb,period,rev,1))assert(++slices<=na+nb);
}
static unsigned differences(void){unsigned n=0;for(unsigned k=0;k<128;k++)n+=c.windows[k].missing+c.windows[k].extra;return n;}
int main(void){
    const uint32_t a[]={100,200,400,800},speed[]={200,400,800,1600},jitter[]={102,198,400,802};
    track_compare_init(&c,42,3,1000);compare(a,4,speed,4,2000,1);assert(!differences());
    compare(a,4,jitter,4,1000,2);assert(!differences() && c.completed==6);
    const uint32_t changed[]={100,300,400,800};compare(a,4,changed,4,1000,3);
    assert(differences()==2 && c.windows[25].missing==1 && c.windows[38].extra==1);
    assert(c.windows[25].revolutions==8 && c.min_period==1000 && c.max_period==2000);
    compare(a,4,NULL,0,1000,4);assert(differences()==6);
    compare(NULL,0,a,4,1000,5);assert(differences()==10);
    memset(wire,0xa5,sizeof(wire));assert(!track_compare_pack(&c,wire,COMPARE_WIRE_SIZE-1) && wire[0]==0xa5);
    assert(track_compare_pack(&c,wire,COMPARE_WIRE_SIZE)==COMPARE_WIRE_SIZE && wire[COMPARE_WIRE_SIZE]==0xa5);
    assert(wire[0]==1 && wire[1]==128 && wire[2]==62 && wire[4]==42);
    assert(wire[24]==3 && wire[28]==1); /* missing/extra, unverified alignment */
    track_compare_init(&c,43,1,UINT32_MAX);
    const uint32_t ends[]={1,UINT32_MAX};compare(ends,2,ends,2,UINT32_MAX,7);
    assert(!differences() && c.completed==128);
    assert(!track_compare_step(&c,a,4,a,4,0,1,256));
    puts("compare: speed normalisation, tolerance, missing/extra edges, empty tracks, bounds and bounded work passed");
}
