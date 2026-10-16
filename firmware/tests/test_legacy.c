#include "ufi_quality.h"
#include "check.h"
#include "legacy_fixture.h"
#include <string.h>
static mfm_decoder_t dec;
static uint8_t payload[1024],received[1024];
static uint32_t flux[160000],hits,valid;
static mfm_id_t found;
static uint8_t flags;
static void receive(void* ctx,const mfm_id_t* id,const uint8_t* p,bool ok) {
    (void)ctx;hits++;valid+=ok;found=*id;flags=dec.sector_flags;
    memcpy(received,p,128u<<id->n);
}
static void decode(uint8_t format,uint32_t rate,uint8_t profile) {
    hits=valid=0;
    mfm_dec_init_format(&dec,format,rate,receive,NULL);mfm_dec_profile(&dec,profile);
    uint32_t n=lf_flux(flux,160000,rate);
    for(uint32_t i=0;i<n;i++)mfm_dec_interval(&dec,flux[i]);
}
static bool continue_work(void){return true;}
int main(void) {
    for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*37u+(i>>4));
    for(unsigned size=0;size<4;size++)for(unsigned profile=0;profile<3;profile++) {
        lf_begin(false);lf_sector(12,1,5,(uint8_t)size,payload,false,false,false);
        decode(3,250000,(uint8_t)profile);
        CHECK(hits==1 && valid==1 && found.c==12 && found.h==1 && found.r==5 && found.n==size,"FM sector and CHRN");
        CHECK(!memcmp(payload,received,128u<<size) && !dec.code_errors,"FM payload and legal clocks");
    }
    lf_begin(false);lf_sector(0,0,1,0,payload,false,false,true);decode(3,125000,0);
    CHECK(valid==1 && (flags&2),"FM deleted data preserved");
    lf_begin(false);lf_sector(0,0,1,0,payload,true,false,false);decode(3,250000,0);
    CHECK(!hits && dec.header_errors==1,"FM ID CRC failure cannot authorise data");
    lf_begin(false);lf_sector(0,0,1,0,payload,false,true,false);decode(3,250000,0);
    CHECK(hits==1 && !valid && dec.data_errors==1,"FM data CRC reported separately");
    lf_begin(false);lf_sector(0,0,1,0,payload,false,false,false);
    lf_cells[lf_data_begin+10]^=1;decode(3,250000,0);
    CHECK(valid==1 && dec.code_errors && (flags&16),"FM bad clock does not masquerade as bad data CRC");
    verify_ref_t ref;uint32_t n=lf_flux(flux,160000,250000);
    CHECK(verify_ref_build_u32(&ref,&dec,flux,n,0,continue_work) && ref.conflicting,"illegal FM source rejected for content verification");
    for(unsigned profile=0;profile<3;profile++) {
        lf_begin(true);lf_sector(76,1,29,1,payload,false,false,false);decode(4,500000,(uint8_t)profile);
        CHECK(hits==1 && valid==1 && found.c==76 && found.h==1 && found.r==29 && found.n==1,"HP MMFM IDs and LSB order");
        CHECK(!memcmp(received,payload,256) && !dec.code_errors,"HP physical byte order and coding");
    }
    lf_begin(true);lf_sector(3,0,0,1,payload,false,false,true);decode(4,500000,0);
    CHECK(valid==1 && (flags&4),"HP defective track marker preserved");
    lf_begin(true);lf_sector(3,0,0,1,payload,true,false,false);decode(4,500000,0);
    CHECK(!hits && dec.header_errors==1,"HP header CRC rejected");
    lf_begin(true);lf_sector(3,0,0,1,payload,false,true,false);decode(4,500000,0);
    CHECK(hits==1 && !valid && dec.data_errors==1,"HP data CRC rejected");
    lf_begin(true);lf_sector(3,0,0,1,payload,false,false,false);
    lf_cells[lf_data_begin+10]^=1;decode(4,500000,0);
    CHECK(valid==1 && dec.code_errors,"HP coding errors independent of CRC");
    for(unsigned hp=0;hp<2;hp++) {
        lf_begin(hp!=0);lf_sector(3,0,0,1,payload,false,false,false);
        uint32_t rate=hp?500000:250000;n=lf_flux(flux,160000,rate);
        CHECK(verify_ref_build_u32(&ref,&dec,flux,n,0,continue_work) && ref.n==1 && ref.format==(hp?4:3),"FM/MMFM native source detection");
        verify_match_t match;verify_match_begin(&match,&ref,&dec);
        for(uint32_t i=0;i<n;i++)verify_match_interval(&match,flux[i]);
        CHECK(verify_match_result(&match,true)==VERIFY_CONTENT,"FM/MMFM firmware readback content verified");
        payload[7]^=1;lf_begin(hp!=0);lf_sector(3,0,0,1,payload,false,false,false);
        n=lf_flux(flux,160000,rate);verify_match_begin(&match,&ref,&dec);
        for(uint32_t i=0;i<n;i++)verify_match_interval(&match,flux[i]);
        CHECK(!verify_match_result(&match,true),"checksum-valid changed data fails readback");payload[7]^=1;
    }
    /* Complete HP track, sector 0 is a real ID, no renumbering. */
    lf_begin(true);for(unsigned r=0;r<30;r++)lf_sector(3,1,(uint8_t)r,1,payload,false,false,false);
    decode(4,500000,0);CHECK(hits==30 && valid==30 && !dec.code_errors,"30 HP sectors without heap");
    /* A sector crossing the capture index must retain decoder state and ID. */
    lf_begin(true);lf_sector(3,1,7,1,payload,false,false,false);
    n=lf_flux(flux,160000,500000);
    uint32_t duration=(uint32_t)(((uint64_t)lf_count*275000000u)/1000000u);
    uint32_t rotation=lf_data_begin*275u+10000u,total=0,split=0;
    while(split<n && total+flux[split]<rotation)total+=flux[split++];
    static uint32_t rotated[160000];uint32_t cnt=0;
    rotated[cnt++]=total+flux[split]-rotation;
    for(uint32_t i=split+1;i<n;i++)rotated[cnt++]=flux[i];
    uint32_t last=0;for(uint32_t i=0;i<n;i++)last+=flux[i];
    rotated[cnt++]=duration-last+flux[0];
    for(uint32_t i=1;i<split;i++)rotated[cnt++]=flux[i];
    CHECK(verify_ref_build_u32(&ref,&dec,rotated,cnt,duration,continue_work) && ref.n==1 && ref.format==4,"HP sector across index reference");
    /* ECC address mark: distinguish recognition from unsupported content decoding. */
    lf_begin(true);lf_sector(3,0,0,1,payload,false,false,false);
    lf_cells[lf_data_begin-1]=1; /* 0x50 -> 0xd0, last chronological data bit */
    decode(4,500000,0);
    CHECK(!hits && dec.marks==2 && dec.last_mark==0xd0,"HP ECC mark recognised without claiming payload support");
    /* Unsupported FM size cannot index beyond the fixed decoder buffer. */
    lf_begin(false);lf_sync();uint32_t start=lf_count;lf_byte(0xfe,0xc7,true);
    lf_byte(0,0,false);lf_byte(0,0,false);lf_byte(1,0,false);lf_byte(7,0,false);
    lf_crc(start,false);lf_repeat(255,17);lf_sync();lf_byte(0xfb,0xc7,true);lf_repeat(0,10);
    decode(3,250000,0);CHECK(!hits && !dec.id_ok,"FM oversized sectors bounded");
    CHECK_SUMMARY("test_legacy");
}

