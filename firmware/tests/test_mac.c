#include "ufi_quality.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mac_fixture.h"
static uint8_t payload[524];
static unsigned good,bad;
static mfm_decoder_t dec;
static void accept(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok){
    (void)ctx;assert(id->c==79 && id->h==1 && id->r==7 && id->n==2);
    if(ok){assert(!memcmp(data,payload,524) && data[524]==0x22);good++;}else bad++;
}
int main(void){
    for(unsigned i=0;i<524;i++)payload[i]=(uint8_t)(i*19+(i>>3));
    static const uint32_t rates[]={500000,380228,346021,312500,280112,251256};
    for(unsigned r=0;r<6;r++){
        mac_reset(rates[r]);mac_sector(79,1,7,payload,0);
        good=bad=0;mfm_dec_init_format(&dec,6,rates[r],accept,NULL);
        for(unsigned i=0;i<mac_count;i++)mfm_dec_interval(&dec,mac_flux[i]);
        assert(good==1 && !bad && !dec.code_errors);
    }
    for(unsigned fault=1;fault<=8;fault<<=1){
        mac_reset(500000);mac_sector(79,1,7,payload,fault);
        good=bad=0;mfm_dec_init_format(&dec,6,500000,accept,NULL);
        for(unsigned i=0;i<mac_count;i++)mfm_dec_interval(&dec,mac_flux[i]);
        assert(!good);if(fault!=1)assert(bad==1);
    }
    mac_reset(500000);mac_sector(79,1,7,payload,0);
    verify_ref_t ref;assert(verify_ref_build_u32(&ref,&dec,mac_flux,mac_count,0,NULL));
    assert(ref.format==6 && ref.n==1);
    verify_match_t match;verify_match_begin(&match,&ref,&dec);
    for(unsigned i=0;i<mac_count;i++)verify_match_interval(&match,mac_flux[i]);
    assert(verify_match_result(&match,true)==VERIFY_CONTENT);
    payload[0]^=1;mac_reset(500000);mac_sector(79,1,7,payload,0);
    verify_match_begin(&match,&ref,&dec);
    for(unsigned i=0;i<mac_count;i++)verify_match_interval(&match,mac_flux[i]);
    assert(!verify_match_result(&match,true)); /* tags are part of content */
    mac_reset(251256);mac_sector(79,1,7,payload,0);
    assert(verify_ref_build_u32(&ref,&dec,mac_flux,mac_count,0,NULL));
    assert(ref.format==6 && ref.rate_bps==251256 && ref.n==1);
    static quality_t quality;uint8_t summary[48];
    quality_init(&quality,6,251256,2);quality.track=79;quality.side=1;quality_begin(&quality,&dec,0,NULL);
    quality.extra.options.flags=0x85;
    for(unsigned rev=0;rev<2;rev++){
        quality_revolution(&quality,(uint8_t)rev);
        quality.revolution_ticks[rev]=55000000;
        for(unsigned i=0;i<mac_count;i++){
            quality.extra.position+=mac_flux[i];quality_interval(&quality,mac_flux[i]);
        }
        quality_end_revolution(&quality);
        payload[0]^=1;mac_reset(251256);mac_sector(79,1,7,payload,0);
    }
    assert(quality_observation_pack(&quality,summary,48)==48 && (summary[4]&8) && !(summary[4]&16));
    assert(summary[24]==1 && !summary[28]); /* changed valid tags, no decode errors */
    static uint8_t local[QUALITY_LOCAL_SIZE];quality_local_pack(&quality,local,sizeof(local));
    unsigned conflicts=0;for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++){
        if(local[32+40*k]&8)conflicts++;
        assert(!(local[32+40*k]&4));
    }
    assert(conflicts==1); /* localized valid content change, not a decoder failure */
    mac_reset(251256);mac_sector(79,1,7,payload,2);
    quality_init(&quality,6,251256,1);quality.extra.options.flags=5;
    quality_begin(&quality,&dec,0,NULL);quality_revolution(&quality,0);
    quality.revolution_ticks[0]=55000000;
    for(unsigned i=0;i<mac_count;i++){
        quality.extra.position+=mac_flux[i];quality_interval(&quality,mac_flux[i]);
    }
    quality_end_revolution(&quality);quality_local_pack(&quality,local,sizeof(local));
    unsigned errors=0;for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++){
        if(local[32+40*k]&4)errors++;
        assert(!(local[32+40*k]&8));
    }
    assert(errors==1); /* checksum failure is not valid content variation */
    puts("Mac GCR: six rates, payload/tags, header/data checksums, sector association, epilogue and reference/readback passed");
}
