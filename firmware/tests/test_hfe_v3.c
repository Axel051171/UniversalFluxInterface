#include "ufi_hfe_v3.h"
#include <assert.h>
#include <stdio.h>
static uint8_t reverse(uint8_t x){
    uint8_t y=0;for(unsigned i=0;i<8;i++){y=(uint8_t)((y<<1)|(x&1));x>>=1;}return y;
}
static bool read_byte(void* p,uint32_t off,uint8_t* x){*x=reverse(((uint8_t*)p)[off]);return true;}
static bool failed_read(void* p,uint32_t off,uint8_t* x){(void)p;(void)off;(void)x;return false;}
static bool raw_read(void* p,uint32_t off,uint8_t* x){*x=((uint8_t*)p)[off];return true;}
int main(void){
    uint8_t data[]={0xf2,72,0x80,0xf1,0xf2,36,0x40,0xf3,7,1};
    uint32_t out[32];hfe_v3_report_t r;
    assert(hfe_v3_decode(read_byte,data,sizeof(data),out,32,&r)==0);
    assert(r.cells==17 && r.index_cell==8 && r.ticks==6875 && r.flux_count==3);
    assert(out[0]==550 && out[1]==1925 && out[2]==550);
    assert(hfe_v3_decode(read_byte,data,sizeof(data),out,2,&r)==-10);
    assert(hfe_v3_decode(failed_read,data,sizeof(data),out,32,&r)==-13);
    uint8_t weak[]={0xf2,72,0xf4,0xf3,5,0xf4};
    assert(hfe_v3_decode(read_byte,weak,sizeof(weak),0,0,&r)==0);
    assert(r.weak_cells==11 && r.cells==11);
    assert(hfe_v3_decode(read_byte,weak,sizeof(weak),out,32,&r)==-11);
    uint8_t multi[]={0xf2,72,0xf1,0x80,0xf1,0x80};
    assert(hfe_v3_decode(read_byte,multi,sizeof(multi),out,32,&r)==-11);
    uint8_t wrap[]={0x80,0xf2,72,0x40,0x20};
    assert(hfe_v3_decode(read_byte,wrap,sizeof(wrap),out,32,&r)==0);
    assert(out[0]==550 && out[1]==4950 && out[2]==4950 && r.ticks==13200);
    uint8_t fast[]={0xf2,1,0x80,0x40,0x20};
    assert(hfe_v3_decode(read_byte,fast,sizeof(fast),out,32,&r)==-18);
    uint8_t bad[][4]={{0xf2,0},{0xf3,0,1},{0xf3,7,0x80},{0xf5},{0x80},{0xf2,72}};
    unsigned lens[]={2,3,3,1,1,2};
    for(unsigned i=0;i<6;i++)assert(hfe_v3_decode(read_byte,bad[i],lens[i],out,32,&r)==-16);
    uint8_t encoded[32];hfe_v3_encoder_t enc;
    for(unsigned value=0;value<256;value++){
        assert(hfe_v3_encode_begin(&enc,encoded,sizeof(encoded),72));
        /* Leading three edges guarantee a decodable stream, then all 256 literals. */
        for(unsigned i=0;i<3;i++)assert(hfe_v3_encode_cells(&enc,2,true));
        for(int bit=7;bit>=0;bit--)assert(hfe_v3_encode_cells(&enc,1,(value>>bit)&1));
        assert(hfe_v3_encode_end(&enc));
        assert(!hfe_v3_decode(raw_read,encoded,enc.length,out,32,&r));
        assert(r.cells==14 && r.index_count==1 && r.index_cell==0 && !r.weak_cells);
        unsigned edges=3;for(unsigned i=0;i<8;i++)edges+=(value>>i)&1;
        assert(r.flux_count==edges && r.ticks==7700);
    }
    assert(hfe_v3_encode_begin(&enc,encoded,3,72));
    assert(!hfe_v3_encode_cells(&enc,8,true) && enc.failed);
    puts("HFEv3 firmware parser: timing, rotation, weak, malformed and bounds passed");
}
