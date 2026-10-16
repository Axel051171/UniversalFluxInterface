/* Independent bit-level Commodore vectors: no production encoder used. */
#include "ufi_quality.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t flux[20000],count,elapsed,rate;
static uint8_t data[256];
static uint8_t disk_id=0x34;
static bool bad_code;
static void bit(unsigned b) {elapsed++;if(b){flux[count++]=(uint32_t)(((uint64_t)elapsed*MFM_TICK_HZ+rate/2)/rate);elapsed=0;}}
static void byte(uint8_t b) {
    static const uint8_t code[16]={10,11,18,19,14,15,22,23,9,25,26,27,13,29,30,21};
    for(int n=1;n>=0;n--)for(int i=4;i>=0;i--)bit((code[(b>>(n*4))&15]>>i)&1);
}
static void sync(void){for(unsigned i=0;i<40;i++)bit(1);}
static void gap(void){for(unsigned i=0;i<9;i++)byte(0x55);}
static void track(bool bad_header,bool bad_data) {
    count=elapsed=0;gap();sync();byte(8);byte(3^18^0x12^disk_id^(bad_header?1:0));
    byte(3);byte(18);byte(0x12);byte(disk_id);byte(15);byte(15);gap();sync();byte(7);
    uint8_t sum=0;for(unsigned i=0;i<256;i++){
        if(bad_code && !i){for(unsigned b=0;b<10;b++)bit(0);}else byte(data[i]);
        sum^=data[i];
    }
    byte(sum^(bad_data?1:0));byte(0);byte(0);gap();
}
static unsigned seen,good;
static void sector(void* ctx,const mfm_id_t* id,const uint8_t* p,bool valid) {
    (void)ctx;seen++;good+=valid;assert(id->c==18 && !id->h && id->r==3 && id->n==1);
    assert(!memcmp(p,data,256) && p[256]==0x12 && p[257]==disk_id);
}
static mfm_decoder_t decoder;
static void decode(void) {
    seen=good=0;mfm_dec_init_format(&decoder,5,rate,sector,NULL);
    for(unsigned i=0;i<count;i++)mfm_dec_interval(&decoder,flux[i]);
}
int main(void) {
    for(unsigned i=0;i<256;i++)data[i]=(uint8_t)i;
    const uint32_t rates[]={250000,266667,285714,307692};
    for(unsigned r=0;r<4;r++) {
        rate=rates[r];track(false,false);decode();assert(seen==1 && good==1 && !decoder.code_errors);
        verify_ref_t source;verify_match_t match;
        assert(verify_ref_build_u32(&source,&decoder,flux,count,0,NULL));
        assert(source.format==5 && source.n==1 && !source.conflicting);
        assert(verify_ref_expected(&source,1ull<<3,18,0,1));
        verify_match_begin(&match,&source,&decoder);
        for(unsigned i=0;i<count;i++)verify_match_interval(&match,flux[i]);
        assert(verify_match_result(&match,false)==VERIFY_CONTENT);
        disk_id^=1;track(false,false);verify_match_begin(&match,&source,&decoder);
        for(unsigned i=0;i<count;i++)verify_match_interval(&match,flux[i]);
        assert(!verify_match_result(&match,true));disk_id^=1;
        data[0]^=1;track(false,false);verify_match_begin(&match,&source,&decoder);
        for(unsigned i=0;i<count;i++)verify_match_interval(&match,flux[i]);
        assert(!verify_match_result(&match,true));data[0]^=1;
        track(true,false);decode();assert(!seen && decoder.header_errors);
        track(false,true);decode();assert(seen==1 && !good && decoder.data_errors);
        verify_match_begin(&match,&source,&decoder);
        for(unsigned i=0;i<count;i++)verify_match_interval(&match,flux[i]);
        assert(!verify_match_result(&match,true));
        bad_code=true;track(false,false);decode();assert(!seen && decoder.code_errors);bad_code=false;
        track(false,false);static uint32_t rotated[20000];uint32_t duration=0,cut=count/2;
        for(unsigned i=0;i<count;i++){rotated[i]=flux[(i+cut)%count];duration+=rotated[i];}
        assert(verify_ref_build_u32(&source,&decoder,rotated,count,duration,NULL));
        assert(source.format==5 && source.n==1 && !source.conflicting);
    }
    static quality_t q;quality_init(&q,5,rate,2);quality_begin(&q,&decoder,0,NULL);
    track(false,false);quality_revolution(&q,0);
    for(unsigned i=0;i<count/2;i++){quality_interval(&q,flux[i]);}
    quality_end_revolution(&q);
    quality_revolution(&q,1);
    for(unsigned i=count/2;i<count;i++){quality_interval(&q,flux[i]);}
    quality_end_revolution(&q);
    assert(q.n==1 && q.sectors[0].seen==2 && !q.sectors[0].flags); /* data crosses index */
    disk_id^=1;track(false,false);
    for(unsigned i=0;i<count;i++)quality_interval(&q,flux[i]);
    assert(q.sectors[0].flags&1); /* valid payload but different disk identity */
    verify_ref_t ref={0};ref.n=2;ref.s[0]=(verify_sector_t){18,0,3,1,0};ref.s[1]=(verify_sector_t){18,0,4,1,0};
    assert(verify_ref_expected(&ref,24,18,0,1));assert(!verify_ref_expected(&ref,56,18,0,1));
    assert(!verify_ref_expected(&ref,8,18,0,1));assert(!verify_ref_expected(&ref,24,18,1,1));
    ref.conflicting=true;assert(!verify_ref_expected(&ref,24,18,0,1));
    puts("test_gcr: four zones, header/data checksums, disk ID and exact reference coverage passed");
}
