#include "ufi_quality.h"
#include <string.h>

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t* p,uint32_t v);

static quality_local_t* local_window(quality_t* q) {
    uint32_t period=q->revolution_ticks[q->revolution];
    if(q->profile || !(q->extra.options.flags&0x84) || !period)return NULL;
    unsigned bin=(unsigned)((uint64_t)q->extra.position*QUALITY_LOCAL_WINDOWS/period);
    if(bin>=QUALITY_LOCAL_WINDOWS)bin=QUALITY_LOCAL_WINDOWS-1;
    return &q->local[bin];
}
static void local_interval(quality_t* q,uint32_t ticks) {
    quality_local_t* w=local_window(q);if(!w)return;
    uint32_t cell=q->rate?MFM_TICK_HZ/((q->format>=5?1u:2u)*q->rate):0;
    uint64_t n=cell?((uint64_t)ticks+cell/2)/cell:0;
    /* Independent of PLL adaptation. Ignore cross-index intervals and long gaps. */
    if(!q->format || !n || n>8 || ticks>q->extra.position || w->count==UINT32_MAX){
        if(w->rejected!=UINT32_MAX)w->rejected++;
        return;
    }
    uint32_t estimate=(uint32_t)(((uint64_t)ticks*256+n/2)/n);
    uint32_t nominal=cell*256,delta=estimate>nominal?estimate-nominal:nominal-estimate;
    if(delta>nominal/8){if(w->rejected!=UINT32_MAX)w->rejected++;return;}
    if(!w->count || estimate<w->min)w->min=estimate;
    if(estimate>w->max)w->max=estimate;
    w->sum+=estimate;w->count++;w->multiples|=(uint16_t)(1u<<n);
}

bool quality_options(const uint8_t* p,uint16_t n,quality_options_t* o)
{
    if (!p || !o || (n!=2u && n!=3u && n!=4u && n!=9u && n!=33u && n!=37u)) return false;
    memset(o,0,sizeof(*o));
    o->limit=48;o->window=4;o->bits=16;o->pattern=0x4489;
    o->margin_permille=250;
    o->glitch=220;o->gap=4400;o->budget_ms=10000;
    o->flags=n>=4u?p[3]:0;o->format=n>=9u?p[4]:0;
    if (n>=9u) o->rate=get32(p+5);
    if (n>=33u) {
        if ((n==33u && (p[9]!=1 || !(o->flags&0xfcu))) || (n==37u && p[9]!=2)) return false;
        o->limit=p[10];o->window=p[11];o->bits=p[12];
        o->glitch=get32(p+13);o->gap=get32(p+17);o->cell=get32(p+21);
        o->pattern=get32(p+25);o->budget_ms=get32(p+29);
    }
    if(n==37u) {
        o->features=(uint16_t)(p[33]|(uint16_t)p[34]<<8);
        o->margin_permille=(uint16_t)(p[35]|(uint16_t)p[36]<<8);
        if(!o->features || (o->features&~QUALITY_FEATURE_MASK) || !o->margin_permille || o->margin_permille>=500)return false;
    }
    return (!o->flags || (o->flags&1u)) &&
           (n<9u || o->flags) && o->format<=7 && p[1]<=1 &&
           o->limit>=1 && o->limit<=48 && o->window>=1 && o->window<=4 &&
           o->bits>=1 && o->bits<=32 && (o->bits==32 || !(o->pattern>>o->bits)) &&
           o->glitch && o->gap>o->glitch && (!o->cell || (o->cell>=100 && o->cell<=100000)) &&
           o->budget_ms>=1 && o->budget_ms<=10000 &&
           (!o->rate || o->rate==125000u || o->rate==150000u || o->rate==250000u || o->rate==300000u || o->rate==500000u || o->rate==1000000u || o->rate==266667u || o->rate==285714u || o->rate==307692u || o->rate==380228u || o->rate==346021u || o->rate==312500u || o->rate==280112u || o->rate==251256u);
}

/* Fixed event storage: all counts continue after the record limit is reached. */
static uint8_t* event(quality_t* q,uint8_t type,uint32_t start,uint32_t end) {
    quality_extra_t* e=&q->extra;
    if (e->count>=e->options.limit) {if(e->dropped!=UINT32_MAX)e->dropped++;return NULL;}
    uint8_t* p=e->events[e->count++];
    p[0]=type;p[1]=q->revolution;put32(p+4,start);put32(p+8,end);return p;
}
static void window(quality_t* q,uint8_t cause) {
    quality_extra_t* e=&q->extra;
    if (!(e->options.flags&0x80)) return;
    uint8_t* p=event(q,5,e->position,e->position);
    if (!p) return;
    p[2]=e->history_count<e->options.window?e->history_count:e->options.window;p[3]=cause;
    for(unsigned i=0;i<p[2];i++) put32(p+12+4*i,e->history[4-p[2]+i]);
    put32(p+28,e->index);
}
static void extra_interval(quality_t* q,uint32_t ticks) {
    quality_extra_t* e=&q->extra;quality_options_t* o=&e->options;
    if ((!(o->flags&0xfc) && !o->features) || q->profile) return;
    for(unsigned i=0;i<3;i++)e->history[i]=e->history[i+1];
    e->history[3]=ticks;if(e->history_count<4)e->history_count++;
    quality_timing_t* t=&e->timing[q->revolution];
    t->count++;t->end=e->position;
    if (o->flags&4) {
        if(t->count==1 || ticks<t->min)t->min=ticks;
        if(ticks>t->max)t->max=ticks;
        t->sum+=ticks;
        uint64_t sq=(uint64_t)ticks*ticks;
        if(UINT64_MAX-t->squares<sq){t->squares=UINT64_MAX;e->saturated=1;}
        else t->squares+=sq;
    }
    bool short_p=ticks<o->glitch,long_p=ticks>=o->gap;
    if(short_p)t->short_count++;
    if(long_p)t->long_count++;
    if ((long_p && (o->flags&8)) || (short_p && (o->flags&16))) {
        uint8_t* p=event(q,long_p?1:2,e->position>=ticks?e->position-ticks:0,e->position);
        if(p){p[3]=e->position<ticks?4:0;put32(p+12,ticks);put32(p+16,e->index);}
    }
    if(short_p || long_p)window(q,long_p?1:2);
    if(o->flags&64) {
        uint32_t cell=o->cell?o->cell:(q->rate?MFM_TICK_HZ/((q->format>=5?1u:2u)*q->rate):550u);
        uint64_t cells=((uint64_t)ticks+cell/2)/cell;
        /* Long silence resets the search: bounded work per captured edge. */
        if(!cells || cells>64) {e->shift=0;e->valid_bits=0;return;}
        uint32_t mask=o->bits==32?UINT32_MAX:((1u<<o->bits)-1);
        for(unsigned i=0;i<cells;i++) {
            e->shift=(e->shift<<1)|(i+1==cells);
            if(e->valid_bits<32)e->valid_bits++;
            if(e->valid_bits>=o->bits && (e->shift&mask)==o->pattern) {
                uint32_t back=(uint32_t)(cells-1-i)*cell;
                uint32_t end=e->position>=back?e->position-back:0;
                uint8_t* p=event(q,4,end,end);
                if(p){p[2]=o->bits;put32(p+12,o->pattern);put32(p+16,cell);put32(p+20,e->index);}
                window(q,4);
            }
        }
    }
}

void quality_init(quality_t* q, uint8_t format, uint32_t rate, uint8_t revs)
{
    memset(q, 0, sizeof(*q));
    q->format = format;
    q->rate = rate;
    q->revs = revs > QUALITY_REVS ? QUALITY_REVS : revs;
    q->profiles = 1;
}

static void sector(void* ctx, const mfm_id_t* id, const uint8_t* data, bool ok)
{
    quality_t* q = ctx;
    if (!q->profile && (q->extra.options.flags&0x20)) {
        uint8_t* p=event(q,3,q->extra.field_start,q->extra.position);
        if(p){p[2]=q->extra.field_rev;p[3]=ok?1:0;memcpy(p+12,id,4);put32(p+16,q->extra.index);}
    }
    if (!q->profile && !ok)window(q,3);
    if (q->match) verify_match_accept(q->match, id, data, ok);
    uint8_t slot;
    for (slot=0; slot<q->n; slot++) {
        if (!memcmp(&q->sectors[slot].id,id,sizeof(*id))) break;
    }
    if (slot==q->n) {
        if (slot==QUALITY_SECTORS) {q->dropped++;return;}
        q->sectors[slot].id=*id;
        q->n++;
    }
    quality_sector_t* s=&q->sectors[slot];
    s->flags|=q->dec->sector_flags;
    if(q->dec->field_bad)s->flags|=16;
    const uint8_t revbit=(uint8_t)(1u<<q->revolution);
    if (!ok) {s->bad|=revbit;return;}
    const uint32_t crc=ufi_crc32(0,data,q->format==2 ? 529u : q->format==5 ? 258u : q->format==6 ? 525u : (128u<<id->n));
    if (s->seen && s->crc!=crc) {
        s->flags|=1u;if(!q->profile)window(q,6);
        quality_local_t* w=local_window(q);if(w && w->conflicts!=UINT32_MAX)w->conflicts++;
    }
    if (!s->seen) s->crc=crc;
    s->seen|=revbit;
    if (!q->profile) s->nominal|=revbit;
    q->attempts[q->revolution][q->profile].seen|=1ull<<slot;
}

void quality_begin(quality_t* q, mfm_decoder_t* dec, uint8_t profile, verify_match_t* match)
{
    q->dec=dec;q->profile=profile<3u?profile:0;q->match=match;q->revolution=0;
    if (q->profiles<q->profile+1u) q->profiles=q->profile+1u;
    mfm_dec_init_format(dec,q->format?q->format:1,q->rate?q->rate:250000u,sector,q);
    dec->tolerance=mfm_get_tolerance();          /* op 34 flags apply to the analysis decoders only */
    if(q->extra.options.features&QUALITY_FEATURE_CODING)dec->check_encoding=true;
    if(q->extra.options.features&QUALITY_FEATURE_MARGIN)dec->margin_permille=q->extra.options.margin_permille;
    mfm_dec_profile(dec,q->profile);
}

void quality_revolution(quality_t* q, uint8_t rev)
{
    q->revolution=rev<QUALITY_REVS?rev:QUALITY_REVS-1u;
    q->dec->header_errors=q->dec->data_errors=0;
    q->dec->code_errors=q->dec->marks=q->dec->margin_errors=0;
    q->extra.position=0;q->extra.index=0;
    q->extra.history_count=0;q->extra.valid_bits=0;q->extra.shift=0;
}

void quality_interval(quality_t* q, uint32_t ticks)
{
    local_interval(q,ticks);
    extra_interval(q,ticks);
    if (!q->profile) {
        uint64_t bin=(uint64_t)ticks*8000000u/MFM_TICK_HZ;
        if (bin>=QUALITY_BINS) bin=QUALITY_BINS-1u;
        if (q->histogram[bin]!=UINT32_MAX) q->histogram[bin]++;
    }
    if (q->format) {
        uint8_t state=q->dec->state;
        uint32_t codes=q->dec->code_errors,marks=q->dec->marks,margins=q->dec->margin_errors;
        uint32_t headers=q->dec->header_errors,data_errors=q->dec->data_errors;
        mfm_dec_interval(q->dec,ticks);
        quality_local_t* w=local_window(q);
        if(w && (codes!=q->dec->code_errors || headers!=q->dec->header_errors || data_errors!=q->dec->data_errors))
            if(w->errors!=UINT32_MAX)w->errors++;
        uint32_t* diagnostics=q->extra.diagnostics[q->revolution][q->profile];
        diagnostics[0]=q->dec->code_errors;diagnostics[1]=q->dec->marks;diagnostics[2]=q->dec->margin_errors;
        if(!q->profile && q->extra.options.features) {
            uint32_t after[]={q->dec->code_errors,q->dec->marks,q->dec->margin_errors};
            uint32_t before[]={codes,marks,margins};
            for(unsigned k=0;k<3;k++) if((q->extra.options.features&(1u<<k)) && after[k]!=before[k]) {
                uint8_t* p=event(q,(uint8_t)(6+k),q->extra.position,q->extra.position);
                if(p){p[2]=q->format;p[3]=q->dec->last_mark;put32(p+12,after[k]-before[k]);put32(p+16,q->extra.index);}
                if(k!=1)window(q,(uint8_t)(7+k));
            }
        }
        if(!state && q->dec->state) {
            /* Boundary is the captured edge completing field sync, not a fabricated exact cell. */
            q->extra.field_start=q->extra.position;q->extra.field_rev=q->revolution;
        }
    }
}

void quality_end_revolution(quality_t* q)
{
    quality_attempt_t* a=&q->attempts[q->revolution][q->profile];
    a->header_errors=q->dec->header_errors;
    a->data_errors=q->dec->data_errors;
    uint32_t* diag=q->extra.diagnostics[q->revolution][q->profile];
    diag[0]=q->dec->code_errors;diag[1]=q->dec->marks;diag[2]=q->dec->margin_errors;
    q->extra.profile_complete[q->profile]|=(uint8_t)(1u<<q->revolution);
    if(!q->profile)q->extra.completed|=(uint8_t)(1u<<q->revolution);
}

static void put32(uint8_t* p,uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}

static bool local_sufficient(const quality_local_t* w) {
    return w->count>=64 && (w->multiples&(w->multiples-1u)) &&
           (uint64_t)w->rejected*10<=w->count &&
           (uint64_t)(w->max-w->min)*20<=w->sum/w->count;
}
uint16_t quality_local_pack(const quality_t* q,uint8_t* out,uint16_t capacity) {
    if(!q || capacity<QUALITY_LOCAL_SIZE)return 0;
    memset(out,0,QUALITY_LOCAL_SIZE);out[0]=1;out[1]=QUALITY_LOCAL_WINDOWS;out[2]=40;
    out[3]=q->extra.completed;put32(out+4,q->generation);put32(out+8,q->rate);
    out[12]=q->format;out[13]=q->extra.options.flags&0x84;
    out[15]=q->ambiguous;
    put32(out+16,64);put32(out+20,30); /* minimum observations, change threshold permille */
    uint32_t means[QUALITY_LOCAL_WINDOWS];unsigned count=0;
    for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++)if(local_sufficient(&q->local[k])) {
        uint32_t m=(uint32_t)(q->local[k].sum/q->local[k].count);unsigned j=count++;
        while(j && means[j-1]>m){means[j]=means[j-1];j--;}means[j]=m;
    }
    uint32_t baseline=count?means[count/2]:0;put32(out+24,baseline);
    for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++) {
        const quality_local_t* w=&q->local[k];uint8_t* p=out+32+40*k;
        uint32_t mean=w->count?(uint32_t)(w->sum/w->count):0,flags=64;
        if(local_sufficient(w))flags|=1;
        if((flags&1) && count>=3 && !q->ambiguous &&
           (uint64_t)(mean>baseline?mean-baseline:baseline-mean)*1000>=(uint64_t)baseline*30)flags|=2;
        if(w->errors)flags|=4;
        if(w->conflicts)flags|=8;
        if(q->status || q->extra.completed!=(uint8_t)((1u<<q->revs)-1u))flags|=128;
        put32(p,flags);put32(p+4,w->count);put32(p+8,mean);put32(p+12,w->min);
        put32(p+16,w->max);put32(p+20,w->rejected);put32(p+24,w->errors);put32(p+28,w->conflicts);
        /* +32/+36 missing/extra edges are filled by the runtime comparison join. */
    }
    return QUALITY_LOCAL_SIZE;
}

/* Interpret observations, never declare physical weak bits or cross-track sync. */
uint32_t quality_recognition(const quality_t* q,uint64_t expected,uint8_t c,uint8_t h,uint8_t n,
                             bool check_position,uint32_t* valid,uint32_t* mismatches) {
    uint32_t flags=RECOG_GEOMETRY_UNCONFIRMED;*valid=*mismatches=0;
    if(!q)return flags|RECOG_INCOMPLETE;
    bool complete=!q->status && !q->dropped && q->extra.completed==(uint8_t)((1u<<q->revs)-1u);
    bool clean=complete && !q->ambiguous;
    if(!complete)flags|=RECOG_INCOMPLETE;
    if(q->ambiguous)flags|=RECOG_AMBIGUOUS;
    uint64_t seen=0;
    for(unsigned r=0;r<q->revs;r++)
        if(q->attempts[r][0].header_errors || q->attempts[r][0].data_errors || q->extra.diagnostics[r][0][0])clean=false;
    for(unsigned i=0;i<q->n;i++){
        const quality_sector_t* s=&q->sectors[i];
        if(s->bad || (s->flags&21))clean=false;
        if(!s->seen || (s->flags&20))continue;
        (*valid)++;
        if(check_position && (s->id.c!=c || s->id.h!=h))(*mismatches)++;
        if(s->id.c==c && s->id.h==h && s->id.n==n && s->id.r<64 && !(s->flags&1))seen|=1ull<<s->id.r;
        else if(expected)clean=false;
    }
    if(*valid && q->format)flags|=RECOG_FORMAT_OBSERVED;
    if(check_position && *valid)flags|=RECOG_POSITION_CHECKED;
    if(*mismatches)flags|=RECOG_HEADER_POSITION_MISMATCH;
    if(expected && seen==expected && clean)flags|=RECOG_SECTOR_SET_CONFIRMED;
    return flags;
}

uint16_t quality_observation_pack(const quality_t* q,uint8_t* out,uint16_t capacity)
{
    if(!q || capacity<48)return 0;
    memset(out,0,48);out[0]=1;out[1]=q->format;out[2]=255;
    uint32_t flags=4,conflicts=0,bad=0,headers=0,data=0,coding=0;
    uint64_t seen=0,expected=0;
    if(q->format==6 && q->track<80){out[2]=q->track/16;out[3]=(uint8_t)(12-out[2]);expected=(1ull<<out[3])-1;}
    for(unsigned i=0;i<q->n;i++){
        const quality_sector_t* s=&q->sectors[i];
        if(s->flags&1)conflicts++;
        if(s->bad || (s->flags&20))bad++;
        if(q->format==6 && s->seen){
            if(s->id.c!=q->track || s->id.h!=q->side || s->id.r>=out[3])flags|=1;
            else if(!(s->flags&21))seen|=1ull<<s->id.r;
        }
    }
    for(unsigned r=0;r<q->revs;r++){
        headers+=q->attempts[r][0].header_errors;data+=q->attempts[r][0].data_errors;
        coding+=q->extra.diagnostics[r][0][0];
    }
    bool valid=false;for(unsigned i=0;i<q->n;i++)if(q->sectors[i].seen)valid=true;
    if(q->format==6 && seen!=expected)flags|=2;
    if(conflicts)flags|=8;
    if(bad || headers || data || coding)flags|=16;
    if(!valid)flags|=32;
    if(q->ambiguous)flags|=64;
    put32(out+4,flags);put32(out+8,(uint32_t)seen);put32(out+12,(uint32_t)(seen>>32));
    put32(out+16,(uint32_t)expected);put32(out+20,(uint32_t)(expected>>32));
    put32(out+24,conflicts);put32(out+28,bad);put32(out+32,headers);
    put32(out+36,data);put32(out+40,coding);put32(out+44,q->format==6?12:0);
    return 48;
}

uint16_t quality_pack(const quality_t* q,uint8_t* out,uint16_t capacity)
{
    bool diagnostic=q->extra.options.features!=0;
    bool extended=(q->extra.options.flags&0xfc)!=0 || diagnostic;
    uint16_t size=QUALITY_WIRE_SIZE+(extended?QUALITY_EXTRA_SIZE:0)+(diagnostic?QUALITY_DIAG_SIZE:0);
    if (capacity<size) return 0;
    memset(out,0,size);
    out[0]=1;out[1]=q->format;out[2]=q->revs;out[3]=q->n;
    out[4]=q->status;out[5]=q->profiles;out[6]=QUALITY_BINS;out[7]=125;
    put32(out+8,q->rate);put32(out+12,q->dropped);
    out[16]=q->ambiguous;
    out[17]=q->track;out[18]=q->side;
    for (unsigned i=0;i<8;i++) {out[20+2*i]=(uint8_t)q->candidates[i];out[21+2*i]=(uint8_t)(q->candidates[i]>>8);}
    put32(out+36,q->generation);put32(out+40,MFM_TICK_HZ);put32(out+44,q->elapsed_ms);
    const uint16_t tol=q->dec?q->dec->tolerance:0,apple=mfm_apple_get_options().flags;   /* flags in effect for this decode */
    out[60]=(uint8_t)tol;out[61]=(uint8_t)(tol>>8);out[62]=(uint8_t)apple;out[63]=(uint8_t)(apple>>8);
    for(unsigned i=8;i<14;i++){out[48+2*(i-8)]=(uint8_t)q->candidates[i];out[49+2*(i-8)]=(uint8_t)(q->candidates[i]>>8);}
    /* 64 fixed sector slots, then 8 x 3 attempts, then interval histogram. */
    for (unsigned i=0;i<QUALITY_SECTORS;i++) {
        const quality_sector_t* s=&q->sectors[i];uint8_t* p=out+64+12*i;
        memcpy(p,&s->id,4);put32(p+4,s->crc);
        p[8]=s->seen;p[9]=s->nominal;p[10]=s->bad;p[11]=s->flags;
    }
    for (unsigned r=0;r<QUALITY_REVS;r++) for (unsigned k=0;k<3;k++) {
        const quality_attempt_t* a=&q->attempts[r][k];uint8_t* p=out+832+16*(3*r+k);
        put32(p,(uint32_t)a->seen);put32(p+4,(uint32_t)(a->seen>>32));
        put32(p+8,a->header_errors);put32(p+12,a->data_errors);
    }
    for (unsigned i=0;i<QUALITY_BINS;i++) put32(out+1216+4*i,q->histogram[i]);
    for (unsigned i=0;i<QUALITY_REVS;i++) {
        put32(out+1728+12*i,q->revolution_ticks[i]);
        put32(out+1732+12*i,q->flux_count[i]);put32(out+1736+12*i,q->max_gap[i]);
    }
    if(extended) {
        const quality_extra_t* e=&q->extra;uint8_t* x=out+QUALITY_WIRE_SIZE;
        x[0]='Q';x[1]='X';x[2]=1;x[3]=e->options.flags;
        put32(x+4,QUALITY_EXTRA_SIZE);x[8]=e->count;x[9]=e->options.limit;
        x[10]=e->completed;x[11]=e->saturated;
        put32(x+12,e->dropped);put32(x+16,e->options.glitch);put32(x+20,e->options.gap);
        put32(x+24,e->options.cell?e->options.cell:(q->rate?MFM_TICK_HZ/((q->format>=5?1u:2u)*q->rate):550));
        put32(x+28,e->options.pattern);put32(x+32,e->options.budget_ms);
        x[36]=e->options.bits;x[37]=e->options.window;x[38]=q->status;
        for(unsigned i=0;i<QUALITY_REVS;i++) {
            const quality_timing_t* t=&e->timing[i];uint8_t* p=x+64+40*i;
            put32(p,t->count);put32(p+4,t->min);put32(p+8,t->max);
            put32(p+12,(uint32_t)t->sum);put32(p+16,(uint32_t)(t->sum>>32));
            put32(p+20,(uint32_t)t->squares);put32(p+24,(uint32_t)(t->squares>>32));
            put32(p+28,t->short_count);put32(p+32,t->long_count);put32(p+36,t->end);
        }
        memcpy(x+384,e->events,sizeof(e->events));
    }
    if(diagnostic) {
        uint8_t* d=out+QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE;
        d[0]='Q';d[1]='D';d[2]=1;d[3]=q->format;
        put32(d+4,QUALITY_DIAG_SIZE);d[8]=(uint8_t)q->extra.options.features;
        d[9]=q->format?(uint8_t)q->extra.options.features:0;
        d[10]=(uint8_t)q->extra.options.margin_permille;d[11]=(uint8_t)(q->extra.options.margin_permille>>8);
        d[12]=q->extra.completed;d[13]=q->status;
        d[14]=q->extra.profile_complete[1];d[15]=q->extra.profile_complete[2];
        for(unsigned r=0;r<8;r++)for(unsigned k=0;k<3;k++)for(unsigned c=0;c<3;c++)
            put32(d+16+12*(3*r+k)+4*c,q->extra.diagnostics[r][k][c]);
    }
    return size;
}

void quality_tail(quality_t* q,uint32_t previous,uint32_t end) {
    quality_extra_t* e=&q->extra;
    if(q->profile || !(e->options.flags&8) || end-previous<e->options.gap)return;
    uint8_t* p=event(q,1,previous,end);
    if(p){p[3]=8;put32(p+12,end-previous);put32(p+16,e->index);}
}
