"""Decoder tolerance flags (op 34) for IBM MFM, Amiga, FM, HP MMFM, Commodore and Macintosh.

Every format gets a synthetic track with one fault at a time.  Without flags the fault
drops or marks the sector as before; with the matching flag the sector is reported, the
finding bit names what was tolerated and the error counters still count."""
from test_regressions import BASE, ERRORS, run


def main():
    code = BASE + ERRORS + r'''
#include "ufi_mfm.h"
#include "../legacy_fixture.h"
#include "../mac_fixture.h"

/* -- shared capture of what the decoder reports ------------------------------------- */
static struct { mfm_id_t id; bool ok; uint8_t flags; uint8_t data[1024]; } got[16];
static unsigned n_got;
static mfm_decoder_t dec;
static void cb(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok){
    (void)ctx;assert(n_got<16);got[n_got].id=*id;got[n_got].ok=ok;got[n_got].flags=dec.sector_flags;
    memcpy(got[n_got].data,data,1024);n_got++;      /* decoder buffers hold 1026 bytes */
}
/* raw MFM bit writer: 2 us half-bit cells at 250 kbit/s, 550 ticks per cell */
static uint32_t flux[200000],nflux,run_cells;static bool prev_data;
static void raw_bit(unsigned b){run_cells++;if(b){flux[nflux++]=run_cells*550u;run_cells=0;}}
static void raw16(uint16_t w){for(int i=15;i>=0;i--)raw_bit((w>>i)&1u);prev_data=(w&1u)!=0;}
static void mfm_byte(uint8_t b){for(int i=7;i>=0;i--){unsigned d=(b>>i)&1u;raw_bit(!prev_data && !d);raw_bit(d);prev_data=d!=0;}}
static void track_reset(void){nflux=0;run_cells=0;prev_data=false;for(unsigned i=0;i<32;i++)mfm_byte(0x4e);}
static void decode(uint8_t format,uint32_t rate,uint16_t tolerance,const uint32_t* f,uint32_t n){
    n_got=0;mfm_dec_init_format(&dec,format,rate,cb,NULL);dec.tolerance=tolerance;
    for(uint32_t i=0;i<n;i++)mfm_dec_interval(&dec,f[i]);
}

/* -- IBM MFM: ID field FE, data field FB, CRC-16 over the A1 A1 A1 marks ------------- */
static uint8_t payload[1024];
static void ibm_field(uint8_t mark,const uint8_t* body,unsigned len,bool bad_crc){
    for(unsigned i=0;i<12;i++)mfm_byte(0);
    uint8_t sync[4]={0xa1,0xa1,0xa1,mark};uint16_t crc=mfm_crc16(0xffff,sync,4);
    raw16(0x4489);raw16(0x4489);raw16(0x4489);mfm_byte(mark);
    for(unsigned i=0;i<len;i++)mfm_byte(body[i]);
    crc=mfm_crc16(crc,body,len);if(bad_crc)crc^=1;
    mfm_byte((uint8_t)(crc>>8));mfm_byte((uint8_t)crc);
    for(unsigned i=0;i<22;i++)mfm_byte(0x4e);
}
static void ibm_track(bool with_id,bool bad_id,bool bad_data){
    track_reset();
    if(with_id){uint8_t id[4]={12,1,5,2};ibm_field(0xfe,id,4,bad_id);}
    ibm_field(0xfb,payload,512,bad_data);
}

/* -- Amiga: 4489 4489, odd/even longs, XOR checksums over the data bit positions ------ */
#define M55 0x55555555u
static void amiga_long(uint32_t x){           /* x holds data bits in even positions */
    for(int i=31;i>=0;i--){
        if(i&1){unsigned next=(x>>(i-1))&1u;raw_bit(!prev_data && !next);}
        else {unsigned d=(x>>i)&1u;raw_bit(d);prev_data=d!=0;}
    }
}
static void amiga_track(uint8_t format,uint8_t track,uint8_t sector,bool bad_header,bool bad_data){
    track_reset();
    uint32_t info=((uint32_t)format<<24)|((uint32_t)track<<16)|((uint32_t)sector<<8)|1u;
    uint32_t odd[128],even[128],dsum=0;
    for(unsigned i=0;i<128;i++){
        uint32_t v=((uint32_t)payload[4*i]<<24)|((uint32_t)payload[4*i+1]<<16)|((uint32_t)payload[4*i+2]<<8)|payload[4*i+3];
        odd[i]=(v>>1)&M55;even[i]=v&M55;dsum^=odd[i]^even[i];
    }
    uint32_t hsum=((info>>1)&M55)^(info&M55);        /* label longs are zero */
    raw16(0xaaaa);raw16(0xaaaa);raw16(0x4489);raw16(0x4489);
    amiga_long(((info>>1)&M55));amiga_long((info&M55)^(bad_header?4u:0u));
    for(unsigned i=0;i<8;i++)amiga_long(0);
    amiga_long(0);amiga_long(hsum&M55);
    amiga_long(0);amiga_long(dsum&M55);
    for(unsigned i=0;i<128;i++)amiga_long(odd[i]^((bad_data && !i)?4u:0u));
    for(unsigned i=0;i<128;i++)amiga_long(even[i]);
    for(unsigned i=0;i<8;i++)mfm_byte(0);
}

/* -- Commodore 1541 GCR: 10+ one bits sync, header 08, data block 07 ----------------- */
static uint32_t cbm_rate=250000;
static void cbm_bit(unsigned b){run_cells++;if(b){flux[nflux++]=(uint32_t)(((uint64_t)run_cells*MFM_TICK_HZ+cbm_rate/2)/cbm_rate);run_cells=0;}}
static void cbm_byte(uint8_t b){
    static const uint8_t code[16]={10,11,18,19,14,15,22,23,9,25,26,27,13,29,30,21};
    for(int n=1;n>=0;n--)for(int i=4;i>=0;i--)cbm_bit((code[(b>>(n*4))&15]>>i)&1u);
}
static void cbm_sync(void){for(unsigned i=0;i<40;i++)cbm_bit(1);}
static void cbm_gap(void){for(unsigned i=0;i<9;i++)cbm_byte(0x55);}
static void cbm_track(bool with_header,uint8_t track,uint8_t sector,bool bad_header,bool bad_data){
    nflux=0;run_cells=0;cbm_gap();
    if(with_header){
        cbm_sync();cbm_byte(8);cbm_byte((uint8_t)(sector^track^0x12^0x34^(bad_header?1:0)));
        cbm_byte(sector);cbm_byte(track);cbm_byte(0x12);cbm_byte(0x34);cbm_byte(15);cbm_byte(15);cbm_gap();
    }
    cbm_sync();cbm_byte(7);uint8_t sum=0;
    for(unsigned i=0;i<256;i++){cbm_byte(payload[i]);sum^=payload[i];}
    cbm_byte((uint8_t)(sum^(bad_data?1:0)));cbm_byte(0);cbm_byte(0);cbm_gap();
}

int main(void){
    for(unsigned i=0;i<sizeof payload;i++)payload[i]=(uint8_t)(i*37u+(i>>4));
    /* defaults: a fresh decoder is strict, whatever the global setting says */
    mfm_set_tolerance(0x1f);assert(mfm_get_tolerance()==0x1f);mfm_dec_init(&dec,250000,cb,NULL);assert(!dec.tolerance);
    mfm_set_tolerance(0xffff);assert(mfm_get_tolerance()==MFM_TOL_MASK);mfm_set_tolerance(0);

    /* ---- IBM MFM (format 1) ---- */
    ibm_track(true,false,false);decode(1,250000,0,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==12 && got[0].id.r==5 && got[0].id.n==2 && !got[0].flags && !memcmp(got[0].data,payload,512));
    ibm_track(true,true,false);decode(1,250000,0,flux,nflux);
    assert(!n_got && dec.header_errors==1);
    decode(1,250000,MFM_TOL_KEEP_BAD_HEADER,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.r==5 && got[0].flags==MFM_SF_HEADER_CHECKSUM && dec.header_errors==1);
    ibm_track(true,false,true);decode(1,250000,0,flux,nflux);
    assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1);
    decode(1,250000,MFM_TOL_DATA_WITHOUT_CHECKSUM,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1 && !memcmp(got[0].data,payload,512));
    ibm_track(false,false,false);decode(1,250000,0,flux,nflux);
    assert(!n_got);
    decode(1,250000,MFM_TOL_DATA_WITHOUT_HEADER,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==255 && got[0].id.r==255 && got[0].id.n==2 && got[0].flags==MFM_SF_NO_HEADER);
    assert(!memcmp(got[0].data,payload,512));
    /* a bad ID followed by its data: without KEEP the data field is headerless, with KEEP it is sector 5 */
    ibm_track(true,true,false);decode(1,250000,MFM_TOL_DATA_WITHOUT_HEADER,flux,nflux);
    assert(n_got==1 && got[0].id.r==255 && got[0].flags==MFM_SF_NO_HEADER);
    decode(1,250000,MFM_TOL_DATA_WITHOUT_HEADER|MFM_TOL_KEEP_BAD_HEADER,flux,nflux);
    assert(n_got==1 && got[0].id.r==5 && got[0].flags==MFM_SF_HEADER_CHECKSUM);

    /* ---- Amiga (format 2) ---- */
    amiga_track(0xff,3,4,false,false);decode(2,250000,0,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==1 && got[0].id.h==1 && got[0].id.r==5 && !got[0].flags && !memcmp(got[0].data,payload,512));
    amiga_track(0xff,3,4,true,false);decode(2,250000,0,flux,nflux);
    assert(!n_got && dec.header_errors==1);
    decode(2,250000,MFM_TOL_KEEP_BAD_HEADER,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_HEADER_CHECKSUM && dec.header_errors==1);
    amiga_track(0xff,3,4,false,true);decode(2,250000,0,flux,nflux);
    assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1);
    decode(2,250000,MFM_TOL_DATA_WITHOUT_CHECKSUM,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM);
    amiga_track(0xfe,3,4,false,false);decode(2,250000,0,flux,nflux);
    assert(!n_got);
    decode(2,250000,MFM_TOL_ANY_TRACK,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.r==5 && !got[0].flags);
    amiga_track(0xff,3,30,false,false);decode(2,250000,0,flux,nflux);
    assert(!n_got);
    decode(2,250000,MFM_TOL_ANY_TRACK,flux,nflux);
    assert(n_got==1 && got[0].id.r==31);

    /* ---- IBM FM (format 3) and HP MMFM (format 4), legacy fixture ---- */
    uint32_t n;
    lf_begin(false);lf_sector(0,0,1,0,payload,true,false,false);n=lf_flux(flux,200000,250000);
    decode(3,250000,0,flux,n);assert(!n_got && dec.header_errors==1);
    decode(3,250000,MFM_TOL_KEEP_BAD_HEADER,flux,n);
    assert(n_got==1 && got[0].ok && got[0].id.r==1 && got[0].id.n==0 && got[0].flags==MFM_SF_HEADER_CHECKSUM);
    decode(3,250000,MFM_TOL_DATA_WITHOUT_HEADER,flux,n);
    assert(n_got==1 && got[0].ok && got[0].id.r==255 && got[0].flags==MFM_SF_NO_HEADER);
    lf_begin(false);lf_sector(0,0,1,0,payload,false,true,false);n=lf_flux(flux,200000,250000);
    decode(3,250000,0,flux,n);assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM);
    decode(3,250000,MFM_TOL_DATA_WITHOUT_CHECKSUM,flux,n);assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1);
    lf_begin(false);lf_sector(0,0,1,0,payload,false,false,true);n=lf_flux(flux,200000,250000);
    decode(3,250000,0,flux,n);assert(n_got==1 && got[0].ok && got[0].flags==2);          /* deleted mark unchanged */
    lf_begin(true);lf_sector(80,0,3,1,payload,false,false,false);n=lf_flux(flux,200000,500000);
    decode(4,500000,0,flux,n);assert(!n_got);
    decode(4,500000,MFM_TOL_ANY_TRACK,flux,n);assert(n_got==1 && got[0].ok && got[0].id.c==80 && got[0].id.r==3);
    lf_begin(true);lf_sector(3,0,0,1,payload,true,false,true);n=lf_flux(flux,200000,500000);
    decode(4,500000,MFM_TOL_KEEP_BAD_HEADER,flux,n);
    assert(n_got==1 && got[0].ok && got[0].flags==(MFM_SF_HEADER_CHECKSUM|4));            /* HP defect mark kept */

    /* ---- Commodore GCR (format 5) ---- */
    cbm_track(true,18,3,false,false);decode(5,250000,0,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==18 && got[0].id.r==3 && !got[0].flags && got[0].data[256]==0x12);
    cbm_track(true,18,3,true,false);decode(5,250000,0,flux,nflux);
    assert(!n_got && dec.header_errors==1);
    decode(5,250000,MFM_TOL_KEEP_BAD_HEADER,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.r==3 && got[0].flags==MFM_SF_HEADER_CHECKSUM && dec.header_errors==1);
    cbm_track(true,18,3,false,true);decode(5,250000,0,flux,nflux);
    assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM);
    decode(5,250000,MFM_TOL_DATA_WITHOUT_CHECKSUM,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1);
    cbm_track(true,90,25,false,false);decode(5,250000,0,flux,nflux);
    assert(!n_got && !dec.header_errors);
    decode(5,250000,MFM_TOL_ANY_TRACK,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==90 && got[0].id.r==25);
    cbm_track(false,18,3,false,false);decode(5,250000,0,flux,nflux);
    assert(!n_got);
    decode(5,250000,MFM_TOL_DATA_WITHOUT_HEADER,flux,nflux);
    assert(n_got==1 && got[0].ok && got[0].id.c==255 && got[0].id.r==255 && got[0].flags==MFM_SF_NO_HEADER && !memcmp(got[0].data,payload,256));

    /* ---- Macintosh GCR (format 6), mac fixture faults 1 header sum, 2 data sum, 8 epilogue ---- */
    static uint8_t mac_data[524];for(unsigned i=0;i<524;i++)mac_data[i]=(uint8_t)(i*19+(i>>3));
    mac_reset(500000);mac_sector(79,1,7,mac_data,1);decode(6,500000,0,mac_flux,mac_count);
    assert(!n_got && dec.header_errors==1);
    decode(6,500000,MFM_TOL_KEEP_BAD_HEADER,mac_flux,mac_count);
    assert(n_got==1 && got[0].ok && got[0].id.c==79 && got[0].id.r==7 && got[0].flags==MFM_SF_HEADER_CHECKSUM && dec.header_errors==1);
    decode(6,500000,MFM_TOL_DATA_WITHOUT_HEADER,mac_flux,mac_count);
    assert(n_got==1 && got[0].ok && got[0].id.r==255 && got[0].id.n==2 && got[0].flags==MFM_SF_NO_HEADER);
    mac_reset(500000);mac_sector(79,1,7,mac_data,2);decode(6,500000,0,mac_flux,mac_count);
    assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM);
    decode(6,500000,MFM_TOL_DATA_WITHOUT_CHECKSUM,mac_flux,mac_count);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_DATA_CHECKSUM && dec.data_errors==1);
    mac_reset(500000);mac_sector(79,1,7,mac_data,8);decode(6,500000,0,mac_flux,mac_count);
    assert(n_got==1 && !got[0].ok && got[0].flags==MFM_SF_EPILOGUE);
    decode(6,500000,MFM_TOL_IGNORE_EPILOGUES,mac_flux,mac_count);
    assert(n_got==1 && got[0].ok && got[0].flags==MFM_SF_EPILOGUE && dec.data_errors==1);
    mac_reset(500000);mac_sector(85,0,7,mac_data,0);decode(6,500000,0,mac_flux,mac_count);
    assert(!n_got && dec.header_errors==1);
    decode(6,500000,MFM_TOL_ANY_TRACK,mac_flux,mac_count);
    assert(n_got==1 && got[0].ok && got[0].id.c==85 && !got[0].flags);
    puts("decoder tolerance flags: six formats, every flag, findings and counters passed");
    return 0;
}
'''
    run("test_tolerance", code, ["src/ufi_mfm.c"])


if __name__ == "__main__":
    main()
