#include "ufi_evidence.h"
#include "ufi_drive_extra.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
    uint8_t c[160]={1},out[4097];c[30]=42;
    assert(!evidence_pack(0,out,4096));evidence_begin(c,24);uint32_t id=evidence_id();
    memset(c,0,sizeof(c));assert(evidence_pack(0,out,4096)==160 && out[30]==42 && rd(out+4)==id);
    for(unsigned i=0;i<245;i++)evidence_index(0x100000000ull+i,i*50,1u<<(i%5));
    memset(out,0xa5,sizeof(out));assert(evidence_pack(1,out,4096)==3872 && out[3872]==0xa5);
    assert(rd(out+8)==240 && rd(out+12)==5 && rd(out+36)==1 && rd(out+40)==0);
    for(unsigned i=0;i<50;i++)evidence_trace(i*20,i*100,30,i*50,2);
    assert(evidence_pack(2,out,4096)==1184 && rd(out+8)==48 && rd(out+12)==2);
    evidence_end();evidence_index(0,0,0);evidence_trace(0,0,0,0,0);
    assert(evidence_pack(1,out,4096)==3872 && !out[3] && rd(out+12)==5);
    assert(!evidence_pack(1,out,3871) && !evidence_pack(4,out,4096));
    evidence_begin(c,0);assert(evidence_id()!=id);evidence_index(3,2,1);evidence_trace(0,0,0,0,0);
    assert(evidence_pack(1,out,4096)==32 && !rd(out+8));
    drive_extra_t t={60000,2000,60000,200,200,0};assert(drive_extra_valid(&t));
    t.gate_tail_us=201;assert(!drive_extra_valid(&t));t.gate_tail_us=0;t.motor_step_ms=2001;assert(!drive_extra_valid(&t));
    t.motor_step_ms=0;t.reserved=1;assert(!drive_extra_valid(&t));assert(!drive_extra_valid(NULL));
    puts("evidence: immutable context, 64-bit index time, bounds, loss accounting, flags and timing limits passed");
}
