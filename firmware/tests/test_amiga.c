/* Independent bit-level Amiga vectors, synthetic data, no production encoder.
 * Layout: AmigaOS trackdisk documentation. Tests also exercise the real encoder
 * separately for rotation length and first/last sector coverage. */
#include "ufi_mfm.h"
#include "ufi_verify.h"
#include "check.h"
#include <string.h>

#define MAX_FLUX 120000u
static uint32_t flux[MAX_FLUX];
static uint16_t source[MAX_FLUX], changed[MAX_FLUX];
static uint8_t trackdata[22 * 512];
static mfm_decoder_t decoder;
static uint32_t pos, last, count, previous;
static void raw_bit(uint32_t bit) {
    ++pos;
    if (bit) {
        flux[count++] = (pos-last)*550u;
        last=pos;
    }
    previous=bit;
}
static void raw_word(uint32_t value) {
    for (int bit=31; bit>=0; --bit) raw_bit((value >> bit)&1u);
}
static void block(const uint8_t *bytes, uint32_t length) {
    for (int parity=0; parity<2; ++parity) {
        for (uint32_t byte=0; byte<length; ++byte) {
            for (int bit=7-parity; bit>=0; bit-=2) {
                uint32_t value=(bytes[byte]>>bit)&1u;
                uint32_t clock=!(previous || value);
                raw_bit(clock);
                raw_bit(value);
            }
        }
    }
}
static uint32_t checksum(const uint8_t *bytes, uint32_t n) {
    uint32_t value=0;
    for (uint32_t i=0; i<n; ++i) value ^= (uint32_t)bytes[i] << (24u-8u*(i%4u));
    return ((value>>1)^value)&0x55555555u;
}
static void sum_block(uint32_t value) {
    uint8_t bytes[4]={(uint8_t)(value>>24), (uint8_t)(value>>16), (uint8_t)(value>>8), (uint8_t)value};
    block(bytes, 4);
}
static uint32_t independent(uint16_t *out, bool label_change, bool bad_header) {
    uint8_t header[20]={255,0,0,11};
    for (int i=0; i<16; ++i) header[4+i]=(uint8_t)i;
    if (label_change) header[19]^=0x80;
    uint8_t data[512];
    for (int i=0; i<512; ++i) data[i]=(uint8_t)(i*17);
    pos=last=count=previous=0;
    raw_word(0xaaaaaaaau);
    raw_word(0x44894489u);
    block(header,4);
    block(header+4,16);
    sum_block(checksum(header,20) ^ (bad_header ? 1u : 0u));
    sum_block(checksum(data,512));
    block(data,512);
    while (pos<100000u) raw_word(0xaaaaaaaau);
    for (uint32_t i=0; i<count; ++i) out[i]=(uint16_t)(flux[i]-1u);
    return count;
}
static uint32_t seen;
static uint32_t ids;
static void sector(void *ctx, const mfm_id_t *id, const uint8_t *data, bool ok) {
    (void)ctx;
    if (!ok) return;
    ++seen;
    ids |= 1u << (id->r-1u);
    if (ctx) {
        CHECK(data[511]==(uint8_t)(511*17), "independent payload decoded");
        CHECK(data[512]==0 && data[527]==15, "independent nonzero label decoded");
    }
}
static void feed(verify_match_t *match, const uint16_t *arr, uint32_t n) {
    for (uint32_t i=0; i<n; ++i) verify_match_interval(match,(uint32_t)arr[i]+1u);
}
int main(void) {
    uint32_t n=independent(source,false,false);
    seen=ids=0;
    mfm_dec_init_amiga(&decoder,250000u,sector,&seen);
    for (uint32_t i=0; i<n; ++i) mfm_dec_interval(&decoder,(uint32_t)source[i]+1u);
    CHECK(seen==1 && ids==1, "independent sector accepted");
    verify_ref_t ref;
    verify_ref_build(&ref,&decoder,source,n);
    CHECK(ref.amiga && ref.n==1, "independent Amiga reference");
    verify_match_t match;
    verify_match_begin(&match,&ref,&decoder);
    feed(&match,source,n);
    CHECK(verify_match_result(&match,true)==VERIFY_CONTENT, "labels match");
    uint32_t n2=independent(changed,true,false);
    feed(&match,changed,n2);
    CHECK(verify_match_result(&match,true)==0, "valid changed label cannot be hidden by good revolution");
    /* A valid duplicate with different labels makes a source ambiguous too. */
    memcpy(source+n,changed,n2*sizeof(*source));
    verify_ref_build(&ref,&decoder,source,n+n2);
    CHECK(ref.conflicting, "conflicting source labels recorded");
    n=independent(source,false,true);
    verify_ref_build(&ref,&decoder,source,n);
    CHECK(ref.n==0, "bad header checksum refused");

    for (uint8_t spt=11; spt<=22; spt+=11) {
        uint32_t rate=spt==11 ? 250000u : 500000u;
        for (uint32_t rpm=297; rpm<=303; rpm+=3) {
            uint32_t cells=(uint32_t)((uint64_t)rate*120u/rpm);
            n=mfm_encode_amiga_track(rate,cells,3,1,spt,trackdata,flux,MAX_FLUX);
            CHECK(n>0, "DD/HD track fits measured revolution");
            uint64_t duration=0;
            seen=ids=0;
            mfm_dec_init_amiga(&decoder,rate,sector,NULL);
            for (uint32_t i=0; i<n; ++i) {
                duration+=flux[i];
                mfm_dec_interval(&decoder,flux[i]);
            }
            CHECK(duration<=(uint64_t)cells*MFM_TICK_HZ/(2u*rate), "encoded track never exceeds revolution");
            CHECK(seen==spt && ids==((1u<<spt)-1u), "first and last sectors survive gap placement");
        }
        uint32_t minimum=(uint32_t)spt*1088u*8u;
        CHECK(mfm_encode_amiga_track(rate,minimum,0,0,spt,trackdata,flux,MAX_FLUX)==0,
              "four startup cells must fit too");
    }
    CHECK(mfm_encode_amiga_track(0,100000,0,0,11,trackdata,flux,MAX_FLUX)==0, "zero rate refused");
    CHECK_SUMMARY("test_amiga");
}
