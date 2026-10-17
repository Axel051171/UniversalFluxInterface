/**
 * UFI Flux Engine - MFM sector codecs (see ufi_mfm.h)
 *
 * Decoder: digital PLL turns flux intervals into MFM cells.
 *  IBM: three A1 sync words (raw 0x4489, missing clock) start a field: FE = ID (C H R N
 *  + CRC), FB/F8 = data (128 << N bytes + CRC).  CRC-16/CCITT (0x1021, init 0xFFFF) over
 *  A1 A1 A1 + mark + field; the CRC bytes included the remainder is 0.
 *  Amiga: 0x4489 0x4489, then 270 raw longwords: info (format 0xFF, track, sector,
 *  sectors to gap), label, header and data checksum, 512 data bytes, each value as its
 *  odd bits then its even bits; checksums = XOR of the raw longs masked with 0x55555555.
 * Encoder: cells to flux intervals on absolute time so fractional cell periods do not
 * drift.  IBM: System/34 track; gap 3 shrinks (and the IAM is dropped) for dense formats
 * (DMF, 1581, Atari 10 sectors) so the track fits 98 % of a revolution.
 */

#include "ufi_mfm.h"
#include <string.h>

#define CRC_A1A1A1      0xCDB4u     /* CRC after the three A1 sync bytes */
#define SYNC3           0x448944894489ull
#define SYNC_A1         0x4489u
#define SYNC_C2         0x5224u
#define SYNC_AMIGA      0x44894489u
#define ID_DATA_CELLS   2000u       /* data field must follow its ID within this */
#define M55             0x55555555u

enum { S_SEARCH, S_MARK, S_FIELD };

uint16_t mfm_crc16(uint16_t crc, const uint8_t* p, uint32_t n)
{
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ============================================================================
 * decoder
 * ============================================================================ */

void mfm_dec_init(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx)
{
    memset(d, 0, sizeof(*d));
    d->nominal = (uint32_t)(((uint64_t)MFM_TICK_HZ * 256u) / (2u * rate_bps));
    d->period = d->nominal;
    d->pmin = d->nominal - d->nominal / 8u;
    d->pmax = d->nominal + d->nominal / 8u;
    d->cb = cb;
    d->ctx = ctx;
    d->encoding = 1;
    d->period_divisor = 16;
    d->phase_divisor = 2;
}

void mfm_dec_profile(mfm_decoder_t* d, uint8_t profile)
{
    d->period_divisor = profile == 1 ? 8 : profile == 2 ? 32 : 16;
    d->phase_divisor = profile == 2 ? 4 : 2;
    d->period = (uint32_t)((uint64_t)d->nominal * (profile == 1 ? 960 : profile == 2 ? 1040 : 1000) / 1000);
}

void mfm_dec_init_amiga(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx)
{
    mfm_dec_init(d, rate_bps, cb, ctx);
    d->amiga = true;d->encoding=2;
}

void mfm_dec_init_format(mfm_decoder_t* d,uint8_t format,uint32_t rate,mfm_sector_cb cb,void* ctx)
{
    mfm_dec_init(d,rate,cb,ctx);
    d->encoding=format;d->amiga=format==2;
    d->check_encoding=format>=3;
    if(format==5 || format==6 || format==7){d->nominal=(uint32_t)(((uint64_t)MFM_TICK_HZ*256u)/rate);d->period=d->nominal;d->pmin=d->nominal-d->nominal/8;d->pmax=d->nominal+d->nominal/8;}
}

static uint8_t raw_to_byte(uint32_t raw)
{
    uint8_t b = 0;
    for (int i = 7; i >= 0; i--) {
        b = (uint8_t)((b << 1) | ((raw >> (2 * i)) & 1u));
    }
    return b;
}

static void field_byte(mfm_decoder_t* d, uint8_t b)
{
    d->crc = mfm_crc16(d->crc, &b, 1);
    if (d->state == S_MARK) {
        d->mark = b;d->last_mark=b;d->marks++;
        d->sector_flags=b==0xf8?2:0;
        d->got = 0;
        if (b == 0xFE) {
            d->want = 6;
        } else if ((b == 0xFB || b == 0xF8) && d->id_ok && d->id_age < ID_DATA_CELLS &&
                   d->id.n <= 3) {
            d->want = (uint16_t)((128u << d->id.n) + 2u);
        } else {
            d->state = S_SEARCH;
            return;
        }
        d->state = S_FIELD;
        return;
    }
    d->buf[d->got++] = b;
    if (d->got < d->want) {
        return;
    }
    if (d->mark == 0xFE) {
        d->id_ok = (d->crc == 0);
        if (!d->id_ok) d->header_errors++;
        if(d->field_bad)d->id_ok=false;
        if (d->id_ok) {
            d->id = (mfm_id_t){d->buf[0], d->buf[1], d->buf[2], d->buf[3]};
            d->id_age = 0;
            d->ids_seen++;
            if (d->id.h < 8u) {
                d->head_ids |= (uint8_t)(1u << d->id.h);
            }
        }
    } else {
        if (d->crc != 0) d->data_errors++;
        if (d->cb) {
            d->cb(d->ctx, &d->id, d->buf, d->crc == 0);
        }
        d->id_ok = false;
    }
    d->state = S_SEARCH;
}

static uint32_t odd_even(uint32_t odd, uint32_t even)
{
    return ((odd & M55) << 1) | (even & M55);
}

static void amiga_sector(mfm_decoder_t* d)
{
    const uint32_t* r = d->raw;
    const uint32_t info = odd_even(r[0], r[1]);
    const uint8_t track = (uint8_t)(info >> 16), sector = (uint8_t)(info >> 8);
    if ((info >> 24) != 0xFFu || sector >= AMIGA_SECTORS_HD) {   /* DD 11 or HD 22 */
        return;
    }
    uint32_t hsum = 0, dsum = 0;
    for (int i = 0; i < 10; i++) {
        hsum ^= r[i];
    }
    for (int i = 14; i < (int)AMIGA_RAW_LONGS; i++) {
        dsum ^= r[i];
    }
    const bool hdr_ok = (hsum & M55) == odd_even(r[10], r[11]);
    const bool data_ok = (dsum & M55) == odd_even(r[12], r[13]);
    if (!hdr_ok) {
        d->header_errors++;
        return;
    }
    if (!data_ok) d->data_errors++;
    d->ids_seen++;
    for (int i = 0; i < 128; i++) {
        const uint32_t v = odd_even(r[14 + i], r[14 + 128 + i]);
        d->buf[4 * i] = (uint8_t)(v >> 24);
        d->buf[4 * i + 1] = (uint8_t)(v >> 16);
        d->buf[4 * i + 2] = (uint8_t)(v >> 8);
        d->buf[4 * i + 3] = (uint8_t)v;
    }
    for (int i = 0; i < 4; i++) {
        const uint32_t v = odd_even(r[2 + i], r[6 + i]);
        d->buf[512 + 4 * i] = (uint8_t)(v >> 24);
        d->buf[513 + 4 * i] = (uint8_t)(v >> 16);
        d->buf[514 + 4 * i] = (uint8_t)(v >> 8);
        d->buf[515 + 4 * i] = (uint8_t)v;
    }
    const mfm_id_t id = {(uint8_t)(track >> 1), (uint8_t)(track & 1u), (uint8_t)(sector + 1u), 2};
    d->buf[528] = (uint8_t)info; /* sectors until gap, for the quality report */
    if (d->cb) {
        d->cb(d->ctx, &id, d->buf, data_ok);
    }
}

/* FM and HP MMFM: independent implementation from HP service manual 2-11..2-15.
 * CRC runs over chronological data bits; HP bytes are LSB-first but CRC bits
 * are emitted x^15 first. HP payload remains in physical byte order (not HPI). */
static uint8_t reverse_byte(uint8_t b) {
    b=(uint8_t)((b>>4)|(b<<4));
    b=(uint8_t)(((b&0xcc)>>2)|((b&0x33)<<2));
    return (uint8_t)(((b&0xaa)>>1)|((b&0x55)<<1));
}
static void code_byte(mfm_decoder_t* d,uint16_t raw,bool hp) {
    for(int i=7;i>=0;i--) {
        bool clock=((raw>>(2*i+1))&1)!=0,data=((raw>>(2*i))&1)!=0;
        bool expected=hp?(!data && !d->previous_transition):true;
        if(clock!=expected){d->code_errors++;d->field_bad=true;}
        d->previous_transition=clock||data;
    }
}
static void legacy_cell(mfm_decoder_t* d) {
    const bool hp=d->encoding==4;
    if(d->id_age<UINT32_MAX)d->id_age++;
    if(d->state==S_SEARCH) {
        uint16_t raw=(uint16_t)d->sr;uint8_t mark=0;
        if(hp && (uint16_t)(d->sr>>16)==0x5555) {
            if(raw==0x2a54)mark=0x70;
            else if(raw==0x2a55)mark=0xf0;
            else if(raw==0x2a44)mark=0x50;
            else if(raw==0x2a45)mark=0xd0;
        } else if(!hp && (uint16_t)(d->sr>>16)==0xaaaa) {
            if(raw==0xf57e)mark=0xfe;
            else if(raw==0xf56f)mark=0xfb;
            else if(raw==0xf56a)mark=0xf8;
            else if(raw==0xf77a)mark=0xfc;
        }
        if(!mark)return;
        d->marks++;d->last_mark=mark;d->mark=mark;
        bool header=hp?(mark==0x70 || mark==0xf0):mark==0xfe;
        if(header)d->id_ok=false;
        /* ECC-marked HP data is recognised but its layout is not guessed. */
        if(mark==0xd0 || mark==0xfc){d->id_ok=false;return;}
        if(!header && (!d->id_ok || d->id_age>=ID_DATA_CELLS))return;
        d->state=S_FIELD;d->cells=0;d->got=0;d->field_bad=false;
        d->previous_transition=(raw&3)!=0;
        d->crc=hp?0xffff:mfm_crc16(0xffff,&mark,1);
        d->want=header?(hp?4:6):(uint16_t)((128u<<d->id.n)+2u);
        d->sector_flags=hp?d->id_flags:(mark==0xf8?2:0);
        if(header)d->id_flags=mark==0xf0?4:0;
        return;
    }
    if(++d->cells!=16)return;
    d->cells=0;
    uint16_t raw=(uint16_t)d->sr;uint8_t serial=raw_to_byte(raw);
    code_byte(d,raw,hp);
    d->crc=mfm_crc16(d->crc,&serial,1);
    d->buf[d->got++]=hp?reverse_byte(serial):serial;
    if(d->got<d->want)return;
    bool header=hp?(d->mark==0x70 || d->mark==0xf0):d->mark==0xfe;
    if(header) {
        if(d->crc)d->header_errors++;
        d->id_ok=!d->crc && !d->field_bad;
        if(hp) {
            if(d->buf[0]>76 || (d->buf[1]&0x7f)>=30)d->id_ok=false;
            d->id=(mfm_id_t){d->buf[0],(uint8_t)(d->buf[1]>>7),(uint8_t)(d->buf[1]&0x7f),1};
        } else {
            d->id=(mfm_id_t){d->buf[0],d->buf[1],d->buf[2],d->buf[3]};
            if(d->id.n>3)d->id_ok=false;
        }
        if(d->id_ok){d->ids_seen++;d->id_age=0;}
    } else {
        if(d->crc)d->data_errors++;
        if(d->field_bad)d->sector_flags|=16;
        if(d->cb)d->cb(d->ctx,&d->id,d->buf,d->crc==0);
        d->id_ok=false;
    }
    d->state=S_SEARCH;
}

/* Validate ordinary MFM clock/data pairs only after sync alignment. */
static void check_mfm_word(mfm_decoder_t* d,uint16_t raw) {
    if(!d->check_encoding)return;
    for(int i=7;i>=0;i--) {
        bool data=((raw>>(2*i))&1)!=0,clock=((raw>>(2*i+1))&1)!=0;
        if(clock!=(!d->previous_transition && !data)){d->code_errors++;d->field_bad=true;}
        d->previous_transition=data;
    }
}

static void amiga_cell(mfm_decoder_t* d)
{
    if (d->state == S_SEARCH) {
        if ((uint32_t)d->sr == SYNC_AMIGA) {
            d->state = S_FIELD;
            d->field_bad=false;d->previous_transition=true;d->sector_flags=0;
            d->marks++;d->last_mark=0xa1;
            d->cells = 0;
            d->got = 0;
        }
        return;
    }
    if(d->cells==15 || d->cells==31)check_mfm_word(d,(uint16_t)d->sr);
    if (++d->cells == 32u) {
        d->cells = 0;
        d->raw[d->got++] = (uint32_t)d->sr;
        if (d->got == 2u && (odd_even(d->raw[0], d->raw[1]) >> 24) != 0xFFu) {
            d->state = S_SEARCH;            /* not a sector header */
        } else if (d->got == AMIGA_RAW_LONGS) {
            amiga_sector(d);
            d->state = S_SEARCH;
        }
    }
}

/* Commodore 1541/1571 GCR: sync, 8-byte header, 260-byte data block.
 * Keep the on-disk track number and disk ID; physical head is not in the header. */
static void gcr_cell(mfm_decoder_t* d,uint32_t bit) {
    static const int8_t decode[32]={-1,-1,-1,-1,-1,-1,-1,-1,-1,8,0,1,-1,12,4,5,
                                   -1,-1,2,3,-1,15,6,7,-1,9,10,11,-1,13,14,-1};
    if(d->id_age<UINT32_MAX)d->id_age++;
    if(bit){if(d->gcr_ones<255)d->gcr_ones++;}
    else {
        if(d->gcr_ones>=10){d->state=S_FIELD;d->cells=0;d->got=0;d->gcr_half=0;d->want=1;d->field_bad=false;d->sector_flags=0;}
        d->gcr_ones=0;
    }
    if(d->gcr_ones>=10){d->state=S_SEARCH;return;}
    if(d->state!=S_FIELD || ++d->cells<5)return;
    d->cells=0;int8_t nibble=decode[d->sr&31];
    if(nibble<0){d->code_errors++;d->field_bad=true;d->id_ok=false;d->state=S_SEARCH;return;}
    if(!d->gcr_half){d->gcr_value=(uint8_t)(nibble<<4);d->gcr_half=1;return;}
    d->gcr_half=0;uint8_t b=(uint8_t)(d->gcr_value|nibble);d->buf[d->got++]=b;
    if(d->got==1) {
        d->mark=d->last_mark=b;d->marks++;
        if(b==8){d->id_ok=false;d->want=8;}
        else if(b==7 && d->id_ok && d->id_age<1000)d->want=260;
        else {d->state=S_SEARCH;return;}
    }
    if(d->got<d->want)return;
    if(d->mark==8) {
        uint8_t sum=0;for(unsigned i=1;i<6;i++)sum^=d->buf[i];
        if(sum)d->header_errors++;
        uint8_t track=d->buf[3],sector=d->buf[2];
        d->id_ok=!sum && track>=1 && track<=84 && sector<21;
        if(d->id_ok){d->id=(mfm_id_t){track,0,sector,1};d->disk_id=(uint16_t)(d->buf[4]|(uint16_t)d->buf[5]<<8);d->id_age=0;d->ids_seen++;}
    } else {
        uint8_t sum=0;for(unsigned i=1;i<258;i++)sum^=d->buf[i];
        if(sum)d->data_errors++;
        memmove(d->buf,d->buf+1,256);d->buf[256]=(uint8_t)d->disk_id;d->buf[257]=(uint8_t)(d->disk_id>>8);
        if(d->cb)d->cb(d->ctx,&d->id,d->buf,sum==0);
        d->id_ok=false;
    }
    d->state=S_SEARCH;
}

/* Macintosh 3.5-inch 6-and-2 GCR, independently implemented from the documented
 * on-disk layout. Sources and checksum provenance: docs/Floppy_Emu_Review.md.
 * Decode in place: each four-symbol group yields at most three bytes, so writes
 * remain behind unread input. Output retains 12 tags + 512 data + format byte. */
static bool mac_payload(mfm_decoder_t* d)
{
    uint16_t a=0,b=0,c=0;unsigned in=1,out=0;
    while(out<524){
        uint8_t hi=d->buf[in++],x=d->buf[in++],y=d->buf[in++];
        bool last=out==522;uint8_t z=last?0:d->buf[in++];
        a=(uint16_t)((a&255u)<<1);if(a&256)a++;
        uint8_t v=(uint8_t)((x|((hi<<2)&192))^a);
        c+=v;if(a&256){c++;a&=255;}d->buf[out++]=v;
        v=(uint8_t)((y|((hi<<4)&192))^c);
        b+=v;if(c>255){b++;c&=255;}d->buf[out++]=v;
        if(!last){
            v=(uint8_t)((z|((hi<<6)&192))^b);
            a+=v;if(b>255){a++;b&=255;}d->buf[out++]=v;
        }
    }
    uint8_t high=(uint8_t)(((a>>6)&3)|((b>>4)&12)|((c>>2)&48));
    bool ok=d->buf[in]==high && d->buf[in+1]==(c&63) &&
            d->buf[in+2]==(b&63) && d->buf[in+3]==(a&63);
    d->buf[524]=(uint8_t)d->disk_id;
    return ok;
}

static int mac_symbol(uint8_t byte)
{
    static const uint8_t symbols[64]={
        0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
        0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
        0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
        0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
    for(unsigned i=0;i<64;i++)if(symbols[i]==byte)return (int)i;
    return -1;
}

static void mac_cell(mfm_decoder_t* d)
{
    if(d->id_age<UINT32_MAX)d->id_age++;
    if(d->state==S_SEARCH){
        uint32_t sync=(uint32_t)d->sr&0xffffffu;
        if(sync==0xd5aa96u || (sync==0xd5aaadu && d->id_ok && d->id_age<2000)){
            d->mark=d->last_mark=(uint8_t)sync;d->marks++;
            d->want=d->mark==0x96?7:706;d->got=d->cells=0;
            d->field_bad=false;d->sector_flags=0;d->state=S_FIELD;
            if(d->mark==0x96)d->id_ok=false;
        }
        return;
    }
    if(++d->cells<8)return;
    d->cells=0;uint8_t byte=(uint8_t)d->sr;
    if(d->got<d->want-2){
        int symbol=mac_symbol(byte);
        if(symbol<0){d->code_errors++;d->field_bad=true;d->id_ok=false;d->state=S_SEARCH;return;}
        byte=(uint8_t)symbol;
    }
    d->buf[d->got++]=byte;
    if(d->got<d->want)return;
    bool ending=d->buf[d->want-2]==0xde && d->buf[d->want-1]==0xaa;
    if(d->mark==0x96){
        uint8_t sum=0;for(unsigned i=0;i<5;i++)sum^=d->buf[i];
        uint8_t cyl=(uint8_t)(d->buf[0]|((d->buf[2]&1)<<6));
        d->id_ok=ending && !sum && cyl<80 && !(d->buf[2]&0x1e);
        if(d->id_ok){
            d->id=(mfm_id_t){cyl,(uint8_t)(d->buf[2]>>5),d->buf[1],2};
            d->disk_id=d->buf[3];d->id_age=0;d->ids_seen++;
        }else d->header_errors++;
    }else{
        bool sector_match=d->buf[0]==d->id.r;
        bool checksum=mac_payload(d);
        bool ok=ending && sector_match && checksum;
        if(!ok)d->data_errors++;
        if(d->cb)d->cb(d->ctx,&d->id,d->buf,ok);
        d->id_ok=false;
    }
    d->state=S_SEARCH;
}

/* Apple II 5.25-inch 16-sector 6-and-2 GCR (DOS 3.3 / ProDOS), format 7 (1.18).
 * Independently implemented from the documented layout (address field D5 AA 96 with
 * 4-and-4 volume/track/sector/checksum and DE AA, data field D5 AA AD with 342 nibbles
 * plus checksum nibble and DE AA; docs/REA_Floppy_Emu_Analyse_2026-10-17.md lists the
 * reference tables).  Sectors are reported in physical order: id = {track, 0, sector, 1},
 * 256 payload bytes, disk_id = volume; DOS 3.3 / ProDOS ordering is a host concern. */
static void apple_payload(mfm_decoder_t* d, uint8_t out[256])
{
    for(unsigned i=0;i<256;i++){
        const uint8_t pair=(uint8_t)((d->buf[i%86u]>>(2u*(i/86u)))&3u);
        out[i]=(uint8_t)((d->buf[86u+i]<<2)|((pair&1u)<<1)|(pair>>1));
    }
}

static const apple_gcr_options_t apple_defaults={{0xd5,0xaa,0x96},{0xd5,0xaa,0xad},{0xde,0xaa},{0xde,0xaa},0};
static apple_gcr_options_t apple_opt={{0xd5,0xaa,0x96},{0xd5,0xaa,0xad},{0xde,0xaa},{0xde,0xaa},0};

bool mfm_apple_options_valid(const apple_gcr_options_t* o)
{
    if(!o || (o->flags&~APPLE_GCR_FLAG_MASK))return false;
    const uint8_t* b=(const uint8_t*)o;
    for(unsigned i=0;i<10;i++)if(!(b[i]&0x80u))return false;    /* GCR bytes always carry the top bit */
    return memcmp(o->addr_prologue,o->data_prologue,3)!=0;
}
void mfm_apple_set_options(const apple_gcr_options_t* o){apple_opt=o?*o:apple_defaults;}
apple_gcr_options_t mfm_apple_get_options(void){return apple_opt;}
static uint32_t apple_mark(const uint8_t m[3]){return ((uint32_t)m[0]<<16)|((uint32_t)m[1]<<8)|m[2];}

static void apple_cell(mfm_decoder_t* d)
{
    const apple_gcr_options_t* o=&apple_opt;
    if(d->id_age<UINT32_MAX)d->id_age++;
    if(d->state==S_SEARCH){
        const uint32_t sync=(uint32_t)d->sr&0xffffffu;
        const bool addr=sync==apple_mark(o->addr_prologue);
        const bool data=!addr && sync==apple_mark(o->data_prologue) &&
            ((d->id_ok && d->id_age<4000) || (o->flags&APPLE_GCR_DATA_WITHOUT_HEADER));
        if(addr || data){
            d->mark=d->last_mark=addr?0x96:0xad;d->marks++;
            d->want=addr?10:345;d->got=d->cells=0;
            d->field_bad=false;d->sector_flags=0;d->state=S_FIELD;
            if(addr)d->id_ok=false;
        }
        return;
    }
    if(++d->cells<8)return;
    d->cells=0;const uint8_t byte=(uint8_t)d->sr;
    if(!(byte&0x80u)){d->code_errors++;d->field_bad=true;d->id_ok=false;d->state=S_SEARCH;return;}
    if(d->mark==0xad && d->got<d->want-2){
        int symbol=mac_symbol(byte);
        if(symbol<0){d->code_errors++;d->field_bad=true;d->id_ok=false;d->state=S_SEARCH;return;}
        /* undo the running XOR of the 6-and-2 stream while storing */
        d->buf[d->got]=(uint8_t)(symbol^(d->got?d->buf[d->got-1]:0));
        d->got++;
    }else d->buf[d->got++]=byte;
    if(d->got<d->want)return;
    const uint8_t* ep=d->mark==0x96?o->addr_epilogue:o->data_epilogue;
    const bool ending=d->buf[d->want-2]==ep[0] && d->buf[d->want-1]==ep[1];
    if(!ending)d->sector_flags|=APPLE_GCR_SF_EPILOGUE;
    const bool end_ok=ending || (o->flags&APPLE_GCR_IGNORE_EPILOGUES);
    if(d->mark==0x96){
        uint8_t v[4];
        for(unsigned i=0;i<4;i++)v[i]=(uint8_t)(((d->buf[2*i]<<1)|1u)&d->buf[2*i+1]);
        const bool sum_ok=v[3]==(uint8_t)(v[0]^v[1]^v[2]);
        if(!sum_ok)d->sector_flags|=APPLE_GCR_SF_ADDR_CHECKSUM;
        const bool limits=(o->flags&APPLE_GCR_ANY_TRACK)?v[2]<64:(v[1]<40 && v[2]<16);
        d->id_ok=end_ok && limits && (sum_ok || (o->flags&APPLE_GCR_KEEP_BAD_ADDR_CHECKSUM));
        if(d->id_ok){
            d->id=(mfm_id_t){v[1],0,v[2],1};
            d->disk_id=v[0];d->id_age=0;d->ids_seen++;d->id_flags=d->sector_flags;
        }
        if(!sum_ok || !ending)d->header_errors++;
    }else{
        /* buf[342] holds the decoded checksum nibble: it must equal buf[341] after the XOR chain */
        const bool checksum=d->buf[342]==0;
        if(!checksum)d->sector_flags|=APPLE_GCR_SF_DATA_CHECKSUM;
        d->sector_flags|=d->id_flags;               /* header findings travel with the sector */
        uint8_t out[256];apple_payload(d,out);memcpy(d->buf,out,256);
        const bool ok=end_ok && (checksum || (o->flags&APPLE_GCR_DATA_WITHOUT_CHECKSUM));
        if(!checksum || !ending)d->data_errors++;
        if(!d->id_ok)d->id=(mfm_id_t){255,0,255,1};  /* data field without header (flag 0x10) */
        if(d->cb)d->cb(d->ctx,&d->id,d->buf,ok);
        d->id_ok=false;d->id_flags=0;
    }
    d->state=S_SEARCH;
}

static void cell(mfm_decoder_t* d, uint32_t bit)
{
    d->sr = (d->sr << 1) | bit;
    if(d->encoding==7){apple_cell(d);return;}
    if(d->encoding==6){mac_cell(d);return;}
    if(d->encoding==5){gcr_cell(d,bit);return;}
    if(d->encoding>=3){legacy_cell(d);return;}
    if (d->amiga) {
        amiga_cell(d);
        return;
    }
    d->id_age++;
    if (d->state == S_SEARCH) {
        if ((d->sr & 0xFFFFFFFFFFFFull) == SYNC3) {
            d->state = S_MARK;
            d->field_bad=false;d->previous_transition=true;
            d->cells = 0;
            d->crc = CRC_A1A1A1;
        }
        return;
    }
    if (++d->cells == 16u) {
        d->cells = 0;
        check_mfm_word(d,(uint16_t)d->sr);
        field_byte(d, raw_to_byte((uint32_t)d->sr & 0xFFFFu));
    }
}

void mfm_dec_interval(mfm_decoder_t* d, uint32_t ticks)
{
    const int64_t t = (int64_t)ticks * 256 + d->phase;
    int64_t n = (t + d->period / 2) / d->period;
    if (n < 1) {
        n = 1;
    }
    if(d->margin_permille && n>=1 && n<=8 && d->state!=S_SEARCH) {
        int64_t residual=t-n*d->period;if(residual<0)residual=-residual;
        if((uint64_t)residual*1000u>(uint64_t)d->period*d->margin_permille)d->margin_errors++;
    }
    if (n > 8) {                        /* no flux / damage: resynchronise */
        if(d->state!=S_SEARCH && d->check_encoding)d->code_errors++;
        d->state = S_SEARCH;
        if(d->encoding>=3)d->id_ok=false;
        d->phase = 0;
        d->period = d->nominal;
        for (int i = 0; i < 8; i++) {
            cell(d, 0);
        }
        return;
    }
    /* frequency: 1/16 of the per-cell error; phase: keep half of the residual */
    const int64_t err = t - n * (int64_t)d->period;
    int64_t p = (int64_t)d->period + err / (n * d->period_divisor);
    if (p < d->pmin) p = d->pmin;
    if (p > d->pmax) p = d->pmax;
    d->period = (uint32_t)p;
    d->phase = (int32_t)(err / d->phase_divisor);
    for (int64_t i = 1; i < n; i++) {
        cell(d, 0);
    }
    cell(d, 1);
}

/* ============================================================================
 * encoder
 * ============================================================================ */

typedef struct {
    uint32_t* out;
    uint32_t max, count;
    uint64_t pos;               /* cell index from the index pulse */
    uint64_t last_t;            /* tick time of the previous transition */
    uint32_t rate;
    uint8_t prev;               /* previous data bit */
} enc_t;

static void put_cell(enc_t* e, uint32_t bit)
{
    if (bit) {
        const uint64_t t = (e->pos * MFM_TICK_HZ) / (2u * e->rate);
        if (e->count < e->max) {
            e->out[e->count] = (uint32_t)(t - e->last_t);
        }
        e->count++;
        e->last_t = t;
    }
    e->pos++;
}

/* n data bits, MSB first, with MFM clock bits */
static void put_bits(enc_t* e, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        const uint8_t bit = (v >> i) & 1u;
        put_cell(e, (!e->prev && !bit) ? 1u : 0u);     /* clock */
        put_cell(e, bit);
        e->prev = bit;
    }
}

static void put_byte(enc_t* e, uint8_t b)
{
    put_bits(e, b, 8);
}

static void put_raw(enc_t* e, uint16_t raw)
{
    for (int i = 15; i >= 0; i--) {
        put_cell(e, (raw >> i) & 1u);
    }
    e->prev = raw & 1u;
}

static void put_run(enc_t* e, uint8_t b, uint32_t n)
{
    while (n--) {
        put_byte(e, b);
    }
}

static void put_crc_field(enc_t* e, uint8_t mark, const uint8_t* p, uint32_t n)
{
    put_run(e, 0x00, 12);
    put_raw(e, SYNC_A1);
    put_raw(e, SYNC_A1);
    put_raw(e, SYNC_A1);
    put_byte(e, mark);
    uint16_t crc = mfm_crc16(CRC_A1A1A1, &mark, 1);
    crc = mfm_crc16(crc, p, n);
    for (uint32_t i = 0; i < n; i++) {
        put_byte(e, p[i]);
    }
    put_byte(e, (uint8_t)(crc >> 8));
    put_byte(e, (uint8_t)crc);
}

#define IBM_SECTOR_BYTES(sz) (22u + 22u + (sz) + 18u)  /* ID part, gap 2, data part (no gap 3) */
#define IBM_HEAD_IAM        (80u + 12u + 4u + 50u)
#define IBM_HEAD_NO_IAM     16u

uint32_t mfm_encode_track(const mfm_track_fmt_t* f, uint8_t cyl, uint8_t head,
                          const uint8_t* sectors, uint32_t* deltas, uint32_t max)
{
    /* layout: standard (IAM, nominal gap 3) if it fits, else shorter gap 3, else no IAM */
    const uint32_t avail = f->track_cells / 16u;
    const uint8_t ncode = f->size_code ? f->size_code : 2u;
    const uint32_t sbytes = 128u << ncode;
    uint32_t gap3 = 0;
    bool iam = false;
    for (int with_iam = 1; with_iam >= 0; with_iam--) {
        const uint32_t hdr = with_iam ? IBM_HEAD_IAM : IBM_HEAD_NO_IAM;
        if (avail <= hdr || !f->spt) {
            continue;
        }
        const uint32_t per = (avail - hdr) / f->spt;
        if (per >= IBM_SECTOR_BYTES(sbytes) + 8u) {
            gap3 = per - IBM_SECTOR_BYTES(sbytes);
            if (gap3 > f->gap3) {
                gap3 = f->gap3;
            }
            iam = with_iam;
            break;
        }
    }
    if (gap3 == 0) {
        return 0;                       /* does not fit (e.g. Atari 11 sectors): read only */
    }
    /* first transition two bit cells after the index: the first interval is never 0 (the
     * write path refuses periods below 1.6 us) */
    enc_t e = {.out = deltas, .max = max, .rate = f->rate_bps, .pos = 4};

    if (iam) {
        put_run(&e, 0x4E, 80);
        put_run(&e, 0x00, 12);
        put_raw(&e, SYNC_C2);
        put_raw(&e, SYNC_C2);
        put_raw(&e, SYNC_C2);
        put_byte(&e, 0xFC);
        put_run(&e, 0x4E, 50);
    } else {
        put_run(&e, 0x4E, IBM_HEAD_NO_IAM);
    }
    const uint8_t id_head = f->head_id_swap ? (uint8_t)(head ^ 1u) : head;
    for (uint8_t s = 0; s < f->spt; s++) {
        const uint8_t id[4] = {cyl, id_head, (uint8_t)(s + 1u), ncode};
        put_crc_field(&e, 0xFE, id, 4);
        put_run(&e, 0x4E, 22);
        put_crc_field(&e, 0xFB, sectors + (uint32_t)s * sbytes, sbytes);
        put_run(&e, 0x4E, gap3);
    }
    while (e.pos + 16u <= f->track_cells) {     /* gap 4b up to the track length */
        put_byte(&e, 0x4E);
    }
    return e.count <= max ? e.count : 0;
}

/* Amiga: odd bits (31, 29 .. 1) or even bits (30 .. 0) of v as 16 data bits */
static uint16_t odd16(uint32_t v)
{
    uint16_t w = 0;
    for (int i = 15; i >= 0; i--) {
        w = (uint16_t)((w << 1) | ((v >> (2 * i + 1)) & 1u));
    }
    return w;
}

static uint16_t even16(uint32_t v)
{
    uint16_t w = 0;
    for (int i = 15; i >= 0; i--) {
        w = (uint16_t)((w << 1) | ((v >> (2 * i)) & 1u));
    }
    return w;
}

/* data bits of a 16-bit word spread to the even positions (= raw long & 0x55555555) */
static uint32_t spread16(uint16_t w)
{
    uint32_t v = 0;
    for (int i = 0; i < 16; i++) {
        v |= ((uint32_t)(w >> i) & 1u) << (2 * i);
    }
    return v;
}

static void put_word(enc_t* e, uint16_t w, uint32_t* sum)
{
    put_bits(e, w, 16);
    if (sum) {
        *sum ^= spread16(w);
    }
}

static void put_long_odd_even(enc_t* e, uint32_t v, uint32_t* sum)
{
    put_word(e, odd16(v), sum);
    put_word(e, even16(v), sum);
}

uint32_t mfm_encode_amiga_track(uint32_t rate_bps, uint32_t track_cells, uint8_t cyl,
                                uint8_t head, uint8_t spt, const uint8_t* sectors,
                                uint32_t* deltas, uint32_t max)
{
    if (!rate_bps || head > 1u || cyl > 127u ||
        (spt != AMIGA_SECTORS && spt != AMIGA_SECTORS_HD) ||
        track_cells < spt * 1088u * 8u + 4u) {
        return 0;
    }
    enc_t e = {.out = deltas, .max = max, .rate = rate_bps, .pos = 4};
    const uint8_t track = (uint8_t)(cyl * 2u + head);
    /* Put the spare cells before the sectors. Keep two trailing null bytes
     * when space permits, so the write seam lies in the gap at both ends. */
    const uint32_t sector_cells = (uint32_t)spt * 1088u * 8u;
    while ((uint64_t)e.pos + 16u + sector_cells + 32u <= track_cells) {
        put_byte(&e, 0x00);
    }
    for (uint8_t s = 0; s < spt; s++) {
        const uint8_t* d = sectors + (uint32_t)s * 512u;
        put_run(&e, 0x00, 2);
        put_raw(&e, SYNC_A1);
        put_raw(&e, SYNC_A1);
        uint32_t hsum = 0, dsum = 0;
        const uint32_t info = 0xFF000000u | ((uint32_t)track << 16) | ((uint32_t)s << 8) |
                              (uint32_t)(spt - s);
        put_long_odd_even(&e, info, &hsum);
        for (int i = 0; i < 4; i++) {
            put_word(&e, 0, &hsum);                   /* label odd halves */
        }
        for (int i = 0; i < 4; i++) {
            put_word(&e, 0, &hsum);                   /* label even halves */
        }
        uint32_t longs[128];
        for (int i = 0; i < 128; i++) {
            longs[i] = ((uint32_t)d[4 * i] << 24) | ((uint32_t)d[4 * i + 1] << 16) |
                       ((uint32_t)d[4 * i + 2] << 8) | d[4 * i + 3];
            dsum ^= spread16(odd16(longs[i])) ^ spread16(even16(longs[i]));
        }
        put_long_odd_even(&e, hsum, NULL);
        put_long_odd_even(&e, dsum, NULL);
        for (int i = 0; i < 128; i++) {
            put_word(&e, odd16(longs[i]), NULL);
        }
        for (int i = 0; i < 128; i++) {
            put_word(&e, even16(longs[i]), NULL);
        }
    }
    while (e.pos + 16u <= track_cells) {               /* track gap */
        put_byte(&e, 0x00);
    }
    return e.count <= max ? e.count : 0;
}
