#include "ufi_quality.h"
#include "check.h"
#include <string.h>
static quality_t q;
static sector_details_t details;
static mfm_decoder_t dec;
static uint8_t data[11*512],wire[289];
static uint32_t flux[100000];
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void init(uint8_t format){
    quality_init(&q,format,250000,1);q.generation=123;
    quality_details_attach(&q,&details);quality_begin(&q,&dec,0,NULL);
}
int main(void){
    init(1);
    mfm_track_fmt_t fmt={250000,100000,2,24,false,0};
    uint32_t n=mfm_encode_track(&fmt,3,1,data,flux,100000);
    CHECK(n,"fixture encoded");
    for(unsigned i=0;i<n;i++){q.extra.position+=flux[i];quality_interval(&q,flux[i]);}
    quality_end_revolution(&q);
    CHECK(details.count==2,"real IBM decoder reports header and data in same record");
    CHECK((details.sectors[0].findings&(SF_TRACK|SF_SIDE))==(SF_TRACK|SF_SIDE),"separate position findings");
    CHECK(!(details.sectors[0].findings&(SF_DATA_BAD|SF_HEADER_BAD)),"clean checksums");
    CHECK(details.sectors[0].expected==512 && details.sectors[0].actual==512,"payload length after header");
    CHECK(!(details.sectors[0].checked&(SF_OFFSET|SF_PROTECTION|SF_NUMBER|SF_VARIANTS)),"no invented checks or IBM geometry");
    memset(wire,0xa5,sizeof wire);
    CHECK(quality_details_pack(&q,0,8,wire,288)==96,"bounded page");
    CHECK(rd(wire+4)==123 && wire[8]==2 && wire[10]==2 && wire[96]==0xa5,"generation and bounds");
    const uint8_t profile[5]={3,1,1,2,9};quality_details_profile(wire,profile);
    CHECK(!(rd(wire+40)&(SF_TRACK|SF_SIDE)) && (rd(wire+40)&(SF_LENGTH|SF_NUMBER))==(SF_LENGTH|SF_NUMBER),"explicit geometry separates expected length and sector range");
    CHECK(wire[30]==1 && !memcmp(wire+56,profile,5),"declared profile on wire");
    CHECK(!quality_details_pack(&q,3,1,wire,288),"invalid cursor");
    CHECK(!quality_details_pack(&q,0,9,wire,288),"invalid page size");
    CHECK(quality_details_pack(&q,2,1,wire,32)==32 && !wire[10],"end cursor");
    memset(wire,0xa5,sizeof wire);
    CHECK(!quality_details_pack(&q,0,1,wire,63) && wire[0]==0xa5,"short buffer unchanged");

    init(1);mfm_id_t id={0,0,1,2};
    dec.sector_flags=MFM_SF_DATA_CHECKSUM;dec.cb(dec.ctx,&id,data,false);
    CHECK(details.sectors[0].findings&SF_DATA_BAD,"bad checksum is not unchecked checksum");
    CHECK(!(details.sectors[0].findings&SF_DATA_UNCHECKED),"bad CRC was checked");
    CHECK(details.sectors[0].findings&SF_REJECTED,"failed CRC rejected by strict policy");
    init(1);dec.sector_flags=MFM_SF_NO_HEADER;id=(mfm_id_t){255,0,255,2};dec.cb(dec.ctx,&id,data,true);
    CHECK((details.sectors[0].findings&SF_NO_HEADER) && !(details.sectors[0].checked&SF_HEADER_BAD),"missing header is not a passed header checksum");
    id=(mfm_id_t){0,0,1,2};
    init(1);dec.state=2;dec.mark=0xfb;dec.id=id;dec.id_ok=true;dec.got=40;dec.want=514;
    mfm_dec_diagnostic_tail(&dec,4);
    CHECK(details.count==1 && details.sectors[0].actual==40 && details.sectors[0].expected==514,"partial field lengths");
    CHECK((details.sectors[0].findings&(SF_TRUNCATED|SF_DATA_UNCHECKED))==(SF_TRUNCATED|SF_DATA_UNCHECKED),"truncation cannot claim checksum failure");
    CHECK(details.sectors[0].reasons==16 && details.sectors[0].unit==1,"capture-end reason and decoder byte unit");
    CHECK(dec.state==2 && dec.got==40,"diagnostics never changes decoder state");
    init(1);dec.state=2;dec.mark=0xfb;dec.id=id;dec.id_ok=true;dec.got=40;dec.want=514;
    mfm_dec_interval(&dec,20000);
    CHECK(details.count==1 && (details.sectors[0].reasons&4),"long gap reports interrupted field");

    init(2);n=mfm_encode_amiga_track(250000,100000,0,0,11,data,flux,100000);
    for(unsigned i=0;i<n;i++)quality_interval(&q,flux[i]);
    CHECK(details.count==11 && !(details.sectors[0].findings&(SF_BLOCK_ID|SF_EXTRA)),"Amiga clean label and block identifier");
    CHECK((details.sectors[0].checked&(SF_BLOCK_ID|SF_EXTRA))==(SF_BLOCK_ID|SF_EXTRA),"Amiga checks advertised");
    CHECK((details.sectors[0].checked&SF_OFFSET) && !(details.sectors[0].findings&SF_OFFSET),"AmigaDOS offset rule on encoded fixture");
    init(2);id=(mfm_id_t){0,0,1,2};dec.raw[0]=dec.raw[1]=0x55000000;data[512]=1;
    dec.cb(dec.ctx,&id,data,true);
    CHECK(details.sectors[0].findings&SF_EXTRA,"Amiga nonzero labels");
    data[512]=0;data[0]=1;dec.cb(dec.ctx,&id,data,true);
    CHECK(details.sectors[0].findings&SF_VARIANTS,"different checksum-accepted observations, not modified-since-formatting");
    CHECK(!(details.sectors[0].checked&SF_PROTECTION),"no protection verdict from variation");
    CHECK(details.variants_count==2 && details.variants[0].observations==1 && details.variants[1].observations==1,"distinct observed contents retained separately");
    q.revolution=1;q.profile=2;dec.cb(dec.ctx,&id,data,true);
    CHECK(details.variants_count==2 && details.variants[1].revs==3 && details.variants[1].profiles==5 && details.variants[1].valid_revs==3,"variant revolution and decoder masks");
    CHECK(quality_variants_pack(&q,0,8,wire,288)==96 && wire[8]==2 && rd(wire+36)==details.variants[0].crc,"variant wire identity");
    CHECK(!quality_variants_pack(&q,3,1,wire,288) && !quality_variants_pack(&q,0,9,wire,288),"variant cursor bounds");
    data[528]=12;dec.cb(dec.ctx,&id,data,true);
    CHECK((details.sectors[0].findings&SF_OFFSET) && details.sectors[0].rule==1 && details.sectors[0].raw_offset==12,"DD sectors-to-gap outside format rule");
    CHECK(quality_details_pack(&q,0,1,wire,288)==64 && wire[62]==12 && wire[63]==1,"format rule and raw value preserved");
    init(2);q.rate=500000;data[528]=22;dec.cb(dec.ctx,&id,data,true);
    CHECK(!(details.sectors[0].findings&SF_OFFSET),"HD sectors-to-gap up to 22 accepted");

    init(1);
    for(unsigned i=0;i<65;i++){id=(mfm_id_t){0,0,(uint8_t)i,2};dec.cb(dec.ctx,&id,data,false);}
    CHECK(details.count==64 && details.dropped==1,"fixed storage with explicit overflow");
    CHECK(details.variants_count==64 && details.variants_dropped==1,"variant storage overflow explicit");
    CHECK(quality_variants_pack(&q,0,8,wire,288)==288 && (wire[11]&1) && rd(wire+12)==1,"variant overflow on wire");
    CHECK(quality_details_pack(&q,0,8,wire,288)==288 && (wire[11]&1) && rd(wire+12)==1,"overflow on wire");
    quality_init(&q,1,250000,1);
    CHECK(!quality_details_pack(&q,0,1,wire,288),"reset invalidates detached data");
    CHECK_SUMMARY("test_sector_details");
}
