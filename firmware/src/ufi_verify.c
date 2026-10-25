/**
 * UFI Flux Engine - write verification with evidence levels (see ufi_verify.h)
 */

#include "ufi_verify.h"
#include <string.h>

#ifndef FLUX_TIMER_FREQ
#define FLUX_TIMER_FREQ 275000000UL
#endif

/* Reflected IEEE polynomial, two immutable nibble lookups per byte. */
static uint32_t crc_byte(uint32_t crc,uint8_t byte)
{
    static const uint32_t table[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
    crc^=byte;
    crc=(crc>>4)^table[crc&15u];
    return (crc>>4)^table[crc&15u];
}

uint32_t ufi_crc32(uint32_t crc, const uint8_t* p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc=crc_byte(crc,p[i]);
    }
    return ~crc;
}

bool ufi_fingerprint(const uint8_t* p,uint32_t n,uint32_t prefix,verify_fingerprint_t* out)
{
    if(!out || (!p && n) || prefix>n)return false;
    uint32_t crc=UINT32_MAX,hash=2166136261u;
    out->prefix_crc=0;out->prefix_hash=hash;
    for(uint32_t i=0;i<n;i++){
        crc=crc_byte(crc,p[i]);hash=(hash^p[i])*16777619u;
        if(i+1==prefix){out->prefix_crc=~crc;out->prefix_hash=hash;}
    }
    out->crc=~crc;out->hash=hash;return true;
}

/* ---- data rate ------------------------------------------------------------------ */

uint32_t verify_guess_rate(const uint16_t* arr, uint32_t n)
{
    /* histogram of periods in 32-tick bins up to 4096 ticks; the lowest bin that holds
     * at least 5 % of the periods is the shortest common interval = one bit time */
    uint16_t bins[128];
    memset(bins, 0, sizeof(bins));
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t p = (uint32_t)arr[i] + 1u;
        if (p < 4096u) {
            if (bins[p / 32u] < 0xFFFFu) bins[p / 32u]++;
            total++;
        }
    }
    if (total < 100u) {
        return 0;
    }
    uint32_t shortest = 0;
    for (uint32_t b = 0; b < 128u; b++) {
        if ((uint32_t)bins[b] * 20u >= total) {
            shortest = b * 32u + 16u;
            break;
        }
    }
    if (!shortest) {
        return 0;
    }
    static const uint32_t rates[] = {250000u, 300000u, 500000u, 1000000u};
    for (int k = 0; k < 4; k++) {
        const uint32_t two_cells = (uint32_t)(FLUX_TIMER_FREQ / rates[k]);   /* one bit time = shortest MFM interval */
        const uint32_t tol = two_cells / 8u;                /* +-12 % */
        if (shortest + tol >= two_cells && shortest <= two_cells + tol) {
            return rates[k];
        }
    }
    return 0;
}

static uint32_t content_hash(const uint8_t* data,uint8_t n,bool amiga,uint8_t format,const mfm_decoder_t* d) {
    uint32_t crc=ufi_crc32(0,data,amiga?528u:format==5?258u:format==6?525u:(128u<<(n&3u)));
    if(format>=3 && d){uint8_t flags=d->sector_flags&6u;crc=ufi_crc32(crc,&flags,1);}
    return crc;
}

/* ---- reference ------------------------------------------------------------------ */

static void ref_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    verify_ref_t* ref = p;
    if (!crc_ok) return;
    if(ref->format>=3 && ref->source_decoder && (ref->source_decoder->field_bad || (ref->source_decoder->sector_flags&4))) {
        ref->conflicting=true;return;
    }
    for (uint8_t i = 0; i < ref->n; i++) {              /* fed twice: keep one copy */
        if (ref->s[i].c == id->c && ref->s[i].h == id->h && ref->s[i].r == id->r) {
            if (ref->s[i].n != id->n || ref->s[i].crc !=
                content_hash(data,id->n,ref->amiga,ref->format,ref->source_decoder)) {
                ref->conflicting = true;
            }
            return;
        }
    }
    if (ref->n >= VERIFY_MAX_SECTORS) {
        ref->overflow = true;
        return;
    }
    ref->s[ref->n].c = id->c;
    ref->s[ref->n].h = id->h;
    ref->s[ref->n].r = id->r;
    ref->s[ref->n].n = id->n;
    ref->s[ref->n].crc = content_hash(data,id->n,ref->amiga,ref->format,ref->source_decoder);
    ref->n++;
}

static void feed_twice(mfm_decoder_t* dec, const uint16_t* arr, uint32_t n)
{
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < n; i++) {
            mfm_dec_interval(dec, (uint32_t)arr[i] + 1u);
        }
    }
}

void verify_ref_build(verify_ref_t* ref, mfm_decoder_t* dec, const uint16_t* arr, uint32_t n)
{
    memset(ref, 0, sizeof(*ref));
    ref->rate_bps = verify_guess_rate(arr, n);
    if (!ref->rate_bps) {
        return;
    }
    mfm_dec_init(dec, ref->rate_bps, ref_sector, ref);
    feed_twice(dec, arr, n);
    if (ref->n == 0) {
        ref->amiga = true;
        mfm_dec_init_amiga(dec, ref->rate_bps, ref_sector, ref);
        feed_twice(dec, arr, n);
        ref->amiga = ref->n > 0;
    }
}

/* ---- match ---------------------------------------------------------------------- */

bool verify_ref_build_u32(verify_ref_t* ref, mfm_decoder_t* dec, const uint32_t* deltas,
                          uint32_t n, uint32_t index_ticks, bool (*poll)(void))
{
    memset(ref,0,sizeof(*ref));
    uint32_t bins[128]={0}, total=0;
    uint64_t duration=0;
    for (uint32_t i=0;i<n;i++) {
        if (!(i&1023u) && poll && !poll()) return false;
        if (!deltas[i]) return false;
        duration+=deltas[i];
        if (deltas[i]<4096u) {bins[deltas[i]/32u]++;total++;}
    }
    if (index_ticks && duration>index_ticks) return false;
    if (total<100u) return true;
    uint32_t shortest=0;
    for (unsigned b=0;b<128;b++) if ((uint64_t)bins[b]*20u>=total) {shortest=b*32u+16u;break;}
    static const uint32_t rates[]={250000u,300000u,500000u,1000000u};
    for (unsigned k=0;k<4;k++) {
        uint32_t period=MFM_TICK_HZ/rates[k],tol=period/8u;
        if (shortest+tol>=period && shortest<=period+tol) {ref->rate_bps=rates[k];break;}
    }
    uint32_t guessed=ref->rate_bps;
    static const uint32_t legacy_rates[]={125000,150000,250000,300000,500000,500000,
                                          250000,266667,285714,307692,500000,380228,346021,312500,280112,251256};
    for (unsigned candidate=0;candidate<18;candidate++) {
        uint8_t format=candidate<2?(uint8_t)(candidate+1):candidate<7?3:candidate==7?4:candidate<12?5:6;
        uint32_t rate=candidate<2?guessed:legacy_rates[candidate-2];
        if(!rate)continue;
        ref->format=format;ref->rate_bps=rate;ref->amiga=format==2;ref->source_decoder=dec;
        mfm_dec_init_format(dec,format,rate,ref_sector,ref);
        for (unsigned pass=0;pass<(index_ticks?2u:1u);pass++) {
            for (uint32_t i=0;i<n;i++) {
                if (!(i&1023u) && poll && !poll()) return false;
                const uint64_t ticks=deltas[i]+((pass && !i)?index_ticks-duration:0);
                if (ticks>UINT32_MAX) return false;
                mfm_dec_interval(dec,(uint32_t)ticks);
            }
        }
        if(format==6 && ref->n && candidate>12 && candidate!=13u+ref->s[0].c/16u){
            ref->n=0;ref->conflicting=false;continue;
        }
        if (ref->n || ref->conflicting) {ref->source_decoder=NULL;return true;}
    }
    ref->format=0;ref->source_decoder=NULL;
    ref->amiga=false;
    return true;
}

void verify_match_accept(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    verify_match_t* m = p;
    for (uint8_t i = 0; i < m->ref->n; i++) {
        const verify_sector_t* s = &m->ref->s[i];
        if (s->c != id->c || s->h != id->h || s->r != id->r) {
            continue;
        }
        if (!crc_ok || (m->ref->format>=3 && m->dec && (m->dec->field_bad || (m->dec->sector_flags&4)))) {
            if (m->bad_crc < 255u) m->bad_crc++;
            if (m->state[i] == VERIFY_SECTOR_MISSING) m->state[i] = VERIFY_SECTOR_BAD_CRC;
        } else if (s->n == id->n && content_hash(data,id->n,m->ref->amiga,m->ref->format,m->dec) == s->crc) {
            m->found |= 1ull << i;
            m->state[i] = VERIFY_SECTOR_OK;
        } else {
            m->mismatch = true;
            if (m->state[i] != VERIFY_SECTOR_OK) m->state[i] = VERIFY_SECTOR_DIFFERENT;
        }
        return;
    }
}

void verify_match_begin(verify_match_t* m, const verify_ref_t* ref, mfm_decoder_t* dec)
{
    memset(m, 0, sizeof(*m));
    m->ref = ref;
    m->dec = dec;
    if (ref->n) {
        if(ref->format>=3){
            mfm_dec_init_format(dec,ref->format,ref->rate_bps,verify_match_accept,m);
        } else if (ref->amiga) {
            mfm_dec_init_amiga(dec, ref->rate_bps, verify_match_accept, m);
        } else {
            mfm_dec_init(dec, ref->rate_bps, verify_match_accept, m);
        }
    }
}

void verify_match_interval(verify_match_t* m, uint32_t ticks)
{
    if (m->ref->n) {
        mfm_dec_interval(m->dec, ticks);
    }
}

uint8_t verify_match_result(const verify_match_t* m, bool count_ok)
{
    const verify_ref_t* ref = m->ref;
    if (ref->conflicting || m->mismatch) {
        return 0;
    }
    if (ref->n == 0 || ref->overflow) {
        return count_ok ? VERIFY_READBACK_PLAUSIBLE : 0;   /* no content reference */
    }
    const uint64_t all = (ref->n >= 64u) ? ~0ull : ((1ull << ref->n) - 1ull);
    return (m->found == all) ? VERIFY_CONTENT : 0;        /* a lost or changed sector fails */
}

bool verify_ref_expected(const verify_ref_t* ref,uint64_t sectors,uint8_t c,uint8_t h,uint8_t n) {
    if(!sectors || ref->overflow || ref->conflicting || !ref->n)return false;
    uint64_t seen=0;
    for(unsigned i=0;i<ref->n;i++) {
        const verify_sector_t* s=&ref->s[i];
        if(s->c!=c || s->h!=h || s->n!=n || s->r>=64)return false;
        seen|=1ull<<s->r;
    }
    return seen==sectors;
}
