#include "ufi_quality.h"
#include "check.h"
#include <string.h>
static quality_t q;
static mfm_decoder_t dec;
static uint8_t data[22*512], wire[QUALITY_WIRE_SIZE+1];
static uint32_t flux[100000];
static void feed(uint32_t n,uint8_t rev) {
    quality_revolution(&q,rev);
    for (uint32_t i=0;i<n;i++) quality_interval(&q,flux[i]);
    quality_end_revolution(&q);
}
static bool cancelled(void) {return false;}
int main(void) {
    quality_options_t options;
    uint8_t request[9]={0,0,3,3,2,0x90,0xd0,3,0};
    CHECK(quality_options(request,2,&options) && !options.flags,"legacy ANALYZE payload");
    CHECK(quality_options(request,4,&options) && options.flags==3 && !options.rate,"adaptive flags");
    CHECK(quality_options(request,9,&options) && options.rate==250000 && options.format==2,"format and rate hints");
    request[3]=2;CHECK(!quality_options(request,4,&options),"adaptive requires detail");
    request[3]=0xff;CHECK(quality_options(request,4,&options),"all analysis flags accepted");
    request[3]=1;CHECK(!quality_options(request,8,&options),"truncated hints refused");
    request[4]=7;CHECK(quality_options(request,9,&options) && options.format==7,"Apple II GCR hint accepted (1.18)");
    request[4]=8;CHECK(!quality_options(request,9,&options),"unknown codec hint refused");
    uint32_t n=mfm_encode_amiga_track(250000,100000,0,0,11,data,flux,100000);
    quality_init(&q,2,250000,2);
    for (uint8_t p=0;p<3;p++) {
        quality_begin(&q,&dec,p,NULL);feed(n,0);feed(n,1);
    }
    CHECK(q.n==11 && q.profiles==3,"all Amiga sectors and profiles");
    CHECK(q.sectors[0].seen==3 && q.sectors[0].nominal==3 && !q.sectors[0].flags,"revolution evidence");
    uint32_t samples=0;for(unsigned i=0;i<128;i++) samples+=q.histogram[i];
    CHECK(samples==2*n,"histogram counts raw samples once, not once per profile");
    data[0]=1;data[4]=1;
    n=mfm_encode_amiga_track(250000,100000,0,0,11,data,flux,100000);
    quality_begin(&q,&dec,0,NULL);feed(n,1);
    CHECK(q.sectors[0].flags&1,"checksum-valid differing data remains a conflict");
    memset(wire,0xa5,sizeof(wire));
    CHECK(!quality_pack(&q,wire,QUALITY_WIRE_SIZE-1) && wire[0]==0xa5,"short output untouched");
    CHECK(quality_pack(&q,wire,QUALITY_WIRE_SIZE)==QUALITY_WIRE_SIZE && wire[QUALITY_WIRE_SIZE]==0xa5,"wire bounds");
    CHECK(wire[0]==1 && wire[1]==2 && wire[2]==2 && wire[3]==11 && wire[5]==3,"wire header");
    CHECK(wire[64+8]==3 && wire[64+11]==1,"wire sector masks and conflict");
    verify_ref_t ref;
    CHECK(verify_ref_build_u32(&ref,&dec,flux,n,55000000,NULL) && ref.amiga && ref.n==11,"u32 source reference");
    CHECK(!verify_ref_build_u32(&ref,&dec,flux,n,1,NULL),"invalid source duration refused");
    CHECK(!verify_ref_build_u32(&ref,&dec,flux,n,0,cancelled),"source decoding cancellable");
    const mfm_track_fmt_t fmt={.rate_bps=250000,.track_cells=100000,.spt=10,.gap3=32,.head_id_swap=true};
    n=mfm_encode_track(&fmt,4,0,data,flux,100000);
    CHECK(verify_ref_build_u32(&ref,&dec,flux,n,0,NULL) && !ref.amiga && ref.n==10,"IBM/1581 source reference");
    quality_init(&q,1,250000,2);quality_begin(&q,&dec,0,NULL);feed(n,0);feed(n,1);
    CHECK(q.n==10 && q.sectors[0].id.c==4 && q.sectors[0].id.h==1,"IBM IDs preserve 1581 head swap");
    CHECK(q.sectors[0].seen==3 && !q.sectors[0].flags,"IBM repeated sector evidence");
    quality_init(&q,0,0,1);quality_begin(&q,&dec,0,NULL);
    quality_revolution(&q,0);quality_interval(&q,UINT32_MAX);quality_end_revolution(&q);
    CHECK(q.n==0 && q.histogram[127]==1,"unknown formats retain long gaps, no invented sectors");
    uint8_t extended[33]={0,0,1,0xff,0,0,0,0,0,1,48,4,16};
    extended[13]=220;extended[17]=0x30;extended[18]=0x11;
    extended[25]=0x89;extended[26]=0x44;extended[29]=0x10;extended[30]=0x27;
    CHECK(quality_options(extended,33,&options),"extended parameter block defaults");
    for(unsigned f=0;f<256;f++) {
        extended[3]=(uint8_t)f;
        CHECK(quality_options(extended,4,&options)==(!f || (f&1)),"flag dependency matrix");
    }
    extended[3]=0xff;extended[10]=49;
    CHECK(!quality_options(extended,33,&options),"bounded event limit");
    extended[10]=48;extended[12]=33;
    CHECK(!quality_options(extended,33,&options),"bounded pattern width");
    extended[12]=16;extended[11]=5;
    CHECK(!quality_options(extended,33,&options),"bounded raw window");
    extended[11]=4;extended[30]=0xff;
    CHECK(!quality_options(extended,33,&options),"bounded time budget");
    CHECK(!quality_options(extended,32,&options),"truncated parameter block");
    request[3]=0xff;request[4]=0;
    CHECK(quality_options(request,4,&options),"defaults for raw evidence tests");
    quality_init(&q,0,0,1);q.extra.options=options;q.extra.options.flags=5;
    quality_begin(&q,&dec,0,NULL);quality_revolution(&q,0);
    quality_interval(&q,UINT32_MAX);quality_interval(&q,UINT32_MAX);
    CHECK(q.extra.saturated && q.extra.timing[0].squares==UINT64_MAX,"quadratic statistics saturate without wrapping");
    CHECK(q.extra.timing[0].sum==2ull*UINT32_MAX,"64-bit interval sum");
    quality_init(&q,0,0,1);q.extra.options=options;q.extra.options.flags=0x41;
    q.extra.options.pattern=0x80000001u;q.extra.options.bits=32;q.extra.options.cell=100;
    quality_begin(&q,&dec,0,NULL);quality_revolution(&q,0);
    q.extra.position=100;quality_interval(&q,100);
    q.extra.position=3200;quality_interval(&q,3100);
    CHECK(q.extra.count==1 && q.extra.events[0][0]==4,"32-bit raw sync pattern without sector decoder");
    quality_revolution(&q,0);q.extra.position=100;quality_interval(&q,100);
    quality_revolution(&q,0);q.extra.position=3100;quality_interval(&q,3100);
    CHECK(q.extra.count==1,"sync search cannot fabricate a pattern across index reset");
    quality_init(&q,0,0,1);q.extra.options=options;q.extra.options.flags=9;
    quality_begin(&q,&dec,0,NULL);quality_revolution(&q,0);
    quality_tail(&q,1000,9000);
    CHECK(q.extra.count==1 && q.extra.events[0][3]==8 && !q.extra.timing[0].count,"silent tail is an interval bound, not a fabricated edge");
    q.extra.position=100;quality_interval(&q,9000);
    CHECK(q.extra.events[1][3]==4 && q.extra.events[1][4]==0,"index-spanning gap is marked and start clipped");
    CHECK_SUMMARY("test_quality");
}
