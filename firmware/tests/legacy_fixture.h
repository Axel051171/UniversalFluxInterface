/* Independent test-only FM/MMFM generator from published format rules.
 * No production encoder or CRC routine used; payload bytes are in on-disk order. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static uint8_t lf_cells[240000];
static uint32_t lf_count,lf_data_begin,lf_id_begin;
static bool lf_hp,lf_previous;
static inline void lf_bit(bool b) {assert(lf_count<sizeof(lf_cells));lf_cells[lf_count++]=b;}
static inline void lf_pair(bool data,bool forced,bool special) {
    bool clock=lf_hp?(!data && (forced || !lf_previous)):(special?forced:true);
    lf_bit(clock);lf_bit(data);lf_previous=clock||data;
}
static inline void lf_byte(uint8_t data,uint8_t clocks,bool special) {
    for(unsigned n=0;n<8;n++) {
        unsigned b=lf_hp?n:7-n;
        lf_pair(((data>>b)&1)!=0,((clocks>>b)&1)!=0,special);
    }
}
static inline void lf_repeat(uint8_t b,unsigned n) {while(n--)lf_byte(b,0,false);}
static inline void lf_sync(void) {
    lf_repeat(0,lf_hp?4:6);if(lf_hp)lf_repeat(255,4);
}
static inline void lf_crc(uint32_t start,bool corrupt) {
    /* Polynomial long division of chronological data bits, including init. */
    uint32_t remainder=0xffff;
    for(uint32_t i=start+1;i<lf_count;i+=2) {
        remainder=(remainder<<1) ^ (lf_cells[i]?0x10000u:0);
        if(remainder&0x10000u)remainder^=0x11021u;
    }
    if(corrupt)remainder^=1;
    for(int i=15;i>=0;i--)lf_pair(((remainder>>i)&1)!=0,false,false);
}
static inline void lf_sector(uint8_t track,uint8_t head,uint8_t number,uint8_t size,
                             const uint8_t* data,bool bad_id,bool bad_data,bool special) {
    lf_sync();uint32_t crc_start=lf_count;
    lf_byte(lf_hp?(special?0xf0:0x70):0xfe,lf_hp?0x0e:0xc7,true);
    if(lf_hp)crc_start=lf_count;
    lf_id_begin=lf_count;
    lf_byte(track,0,false);
    if(lf_hp)lf_byte((uint8_t)(number|(head<<7)),0,false);
    else {lf_byte(head,0,false);lf_byte(number,0,false);lf_byte(size,0,false);}
    lf_crc(crc_start,bad_id);lf_repeat(lf_hp?0:255,17);
    lf_sync();crc_start=lf_count;
    lf_byte(lf_hp?0x50:(special?0xf8:0xfb),lf_hp?0x0e:0xc7,true);
    if(lf_hp)crc_start=lf_count;
    lf_data_begin=lf_count;
    for(unsigned i=0;i<(lf_hp?256u:(128u<<size));i++)lf_byte(data[i],0,false);
    lf_crc(crc_start,bad_data);lf_repeat(lf_hp?0:255,35);
}
static inline void lf_begin(bool hp) {lf_count=0;lf_previous=false;lf_hp=hp;lf_repeat(hp?0:255,16);}
static inline uint32_t lf_flux(uint32_t* out,uint32_t cap,uint32_t rate) {
    uint32_t n=0,last=0;
    for(uint32_t i=0;i<lf_count;i++)if(lf_cells[i]) {
        assert(n<cap);uint32_t t=(uint32_t)(((uint64_t)(i+1)*275000000u)/(2*rate));
        out[n++]=t-last;last=t;
    }
    return n;
}
