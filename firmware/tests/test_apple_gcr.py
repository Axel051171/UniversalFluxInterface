"""Apple II 5.25-inch 6-and-2 GCR decoder (format 7) against a synthetic track."""
from test_regressions import BASE, ERRORS, run


def main():
    code = BASE + ERRORS + r'''
#include "ufi_mfm.h"
#include <assert.h>
#include <string.h>
#include "../apple_fixture.h"
static const apple_gcr_options_t apple_defaults={{0xd5,0xaa,0x96},{0xd5,0xaa,0xad},{0xde,0xaa},{0xde,0xaa},0};
static struct { mfm_id_t id; uint8_t data[256]; bool ok; } found[64];
static unsigned nfound;
static void cb(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok){
    (void)ctx;assert(nfound<64);found[nfound].id=*id;memcpy(found[nfound].data,data,256);found[nfound++].ok=ok;
}
static void feed(mfm_decoder_t* d){for(unsigned i=0;i<apl_count;i++)mfm_dec_interval(d,apl_flux[i]);}
int main(void){
    static mfm_decoder_t d;
    uint8_t data[16][256];
    for(unsigned s=0;s<16;s++)for(unsigned i=0;i<256;i++)data[s][i]=(uint8_t)(s*37u+i*11u+(i>>3));
    /* clean track 17, volume 254, physical order */
    apl_reset();for(unsigned s=0;s<16;s++)apl_sector(254,17,s,data[s],0);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==16);
    for(unsigned s=0;s<16;s++){
        assert(found[s].ok && found[s].id.c==17 && found[s].id.h==0 && found[s].id.r==s && found[s].id.n==1);
        assert(!memcmp(found[s].data,data[s],256));
    }
    assert(d.disk_id==254 && d.ids_seen==16 && !d.header_errors && !d.data_errors && !d.code_errors);
    /* faults: bad address checksum drops the header, bad data checksum or epilogue is reported as !ok */
    apl_reset();apl_sector(254,3,0,data[0],1);apl_sector(254,3,1,data[1],2);apl_sector(254,3,2,data[2],4);apl_sector(254,3,3,data[3],0);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==3 && d.header_errors==1 && d.data_errors==2);
    assert(!found[0].ok && found[0].id.r==1 && !found[1].ok && found[1].id.r==2 && found[2].ok && found[2].id.r==3);
    /* speed tolerance: a 6 % slow drive still decodes */
    apl_reset();for(unsigned s=0;s<16;s++)apl_sector(1,0,s,data[s],0);
    for(unsigned i=0;i<apl_count;i++)apl_flux[i]=apl_flux[i]*106u/100u;
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==16 && found[15].ok);
    /* options (1.18): defaults, validation, copy-protection marks, tolerance flags, sector flags */
    apple_gcr_options_t o=mfm_apple_get_options();
    assert(o.addr_prologue[2]==0x96 && o.data_prologue[2]==0xad && o.addr_epilogue[0]==0xde && !o.flags);
    assert(mfm_apple_options_valid(&o));
    o.flags=0x20;assert(!mfm_apple_options_valid(&o));o.flags=0;
    o.addr_prologue[0]=0x55;assert(!mfm_apple_options_valid(&o));o.addr_prologue[0]=0xd5;
    memcpy(o.data_prologue,o.addr_prologue,3);assert(!mfm_apple_options_valid(&o));o.data_prologue[2]=0xad;
    /* the faulty track from above: with the flags every sector is reported, with diagnostics */
    apl_reset();apl_sector(254,3,0,data[0],1);apl_sector(254,3,1,data[1],2);apl_sector(254,3,2,data[2],4);apl_sector(254,3,3,data[3],0);
    o.flags=APPLE_GCR_KEEP_BAD_ADDR_CHECKSUM|APPLE_GCR_IGNORE_EPILOGUES|APPLE_GCR_DATA_WITHOUT_CHECKSUM;
    mfm_apple_set_options(&o);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==4 && found[0].ok && found[0].id.r==0 && found[1].ok && found[2].ok && found[3].ok);
    assert((d.sector_flags&APPLE_GCR_SF_ADDR_CHECKSUM)==0);   /* last sector was clean */
    assert(!memcmp(found[1].data,data[1],256) && !memcmp(found[2].data,data[2],256));
    /* a protected disk with its own marks decodes only with matching options */
    apl_marks(0xd4,0xaa,0x96,0xd5,0xaa,0xb5,0xde,0xab,0xde,0xaa);
    apl_reset();for(unsigned s=0;s<16;s++)apl_sector(1,5,s,data[s],0);
    mfm_apple_set_options(NULL);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==0 && d.ids_seen==0);
    apple_gcr_options_t p={{0xd4,0xaa,0x96},{0xd5,0xaa,0xb5},{0xde,0xab},{0xde,0xaa},0};
    assert(mfm_apple_options_valid(&p));mfm_apple_set_options(&p);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==16 && found[7].ok && found[7].id.c==5 && !memcmp(found[7].data,data[7],256));
    /* track 45 / sector 20 only with ANY_TRACK; a data field without header only with the flag */
    apl_marks(0xd5,0xaa,0x96,0xd5,0xaa,0xad,0xde,0xaa,0xde,0xaa);
    apl_reset();apl_sector(1,45,20,data[0],0);
    mfm_apple_set_options(NULL);nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==0 && d.header_errors==0 && d.ids_seen==0);
    p=apple_defaults;p.flags=APPLE_GCR_ANY_TRACK;mfm_apple_set_options(&p);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==1 && found[0].ok && found[0].id.c==45 && found[0].id.r==20);
    apl_reset();apl_sector(1,3,0,data[0],1);         /* bad header: data field normally skipped */
    mfm_apple_set_options(NULL);nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==0);
    p=apple_defaults;p.flags=APPLE_GCR_DATA_WITHOUT_HEADER;mfm_apple_set_options(&p);
    nfound=0;mfm_dec_init_format(&d,7,250000u,cb,NULL);feed(&d);
    assert(nfound==1 && found[0].ok && found[0].id.c==255 && found[0].id.r==255 && !memcmp(found[0].data,data[0],256));
    mfm_apple_set_options(NULL);
    puts("apple gcr: 16 sectors in physical order, 4-and-4 header, 6-and-2 checksum, faults, speed tolerance, options and protection marks passed");
    return 0;
}
'''
    run('regression_apple_gcr', code, ('src/ufi_mfm.c',))


if __name__ == '__main__':
    main()
