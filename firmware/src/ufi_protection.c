#include "ufi_protection.h"
#include <string.h>
static unsigned bits(uint8_t mask){unsigned n=0;for(;mask;mask>>=1)n+=mask&1u;return n;}
static uint32_t distance(uint32_t a,uint32_t b,uint32_t period){
    uint32_t d=a>b?a-b:b-a;return d>period/2?period-d:d;
}
bool protection_options_decode(const uint8_t* p,uint16_t n,protection_options_t* out){
    if(n!=8 || !p || !out || p[1]!=1)return false;
    protection_options_t o={(uint16_t)(p[2]|(uint16_t)p[3]<<8),(uint16_t)(p[6]|(uint16_t)p[7]<<8),p[4],p[5]};
    if((o.flags&~PROTECT_MASK) || o.min_revs<2 || o.min_revs>8 || o.min_repeat<2 || o.min_repeat>8 ||
       o.timing_permille<10 || o.timing_permille>250)return false;
    *out=o;return true;
}
void protection_begin(protection_state_t* s,quality_t* q,const protection_options_t* options){
    q->protection=NULL;
    if(!options->flags)return;
    memset(s,0,sizeof(*s));s->options=*options;s->scan_rev=255;q->protection=s;
}
static void add(protection_state_t* s,uint16_t rule,uint16_t category,const mfm_id_t* id,
                uint32_t position,uint32_t extent,uint32_t value,uint32_t reference,uint8_t revs,
                uint8_t level,uint8_t qualifiers,uint8_t unit,uint32_t support){
    if((s->limitations&PROTECT_INCOMPLETE) && level>1)level=1;
    if(s->count==PROTECT_RECORDS){if(s->dropped!=UINT32_MAX)s->dropped++;s->limitations|=PROTECT_OVERFLOW;return;}
    protection_record_t* r=&s->records[s->count++];
    r->rule=rule;r->category=category;r->position=position;r->extent=extent;r->value=value;r->reference=reference;
    r->id=id?*id:(mfm_id_t){255,255,255,255};r->revs=revs;r->level=level;r->qualifiers=qualifiers;r->unit=unit;r->support=support;
    uint8_t state=level==3?PROTECT_KNOWN_SIGNATURE:level==2?PROTECT_CANDIDATE:PROTECT_ANOMALY;
    if(state>s->state)s->state=state;
}
void protection_sector_hashed(quality_t* q,const mfm_id_t* id,const uint8_t* data,bool accepted,
                              const verify_fingerprint_t* fingerprint){
    protection_state_t* s=q->protection;if(!s || q->profile || q->revolution>=8 || !data)return;
    if(!(s->options.flags&(PROTECT_CONTENT|PROTECT_ERRORS|PROTECT_LAYOUT|PROTECT_POSITION)))return;
    uint32_t flags=q->dec->sector_flags;
    if(flags&(MFM_SF_HEADER_CHECKSUM|MFM_SF_NO_HEADER))return;
    unsigned i;for(i=0;i<s->n;i++)if(!memcmp(&s->sectors[i].id,id,4))break;
    if(i==s->n){
        if(i==QUALITY_SECTORS){s->limitations|=PROTECT_OVERFLOW;if(s->observation_dropped!=UINT32_MAX)s->observation_dropped++;return;}
        s->sectors[i].id=*id;s->n++;
    }
    protection_sector_t* a=&s->sectors[i];uint8_t rev=q->revolution,mask=(uint8_t)(1u<<rev);
    uint32_t position=q->extra.position,period=q->revolution_ticks[rev];
    if(a->seen&mask){
        if(period && position<=period && a->positions[rev]<=period &&
           distance(position,a->positions[rev],period)>period/1000u)a->duplicates|=mask;
    }else a->positions[rev]=position;
    if(s->options.flags&PROTECT_POSITION){
        if(s->order_n[rev]<QUALITY_SECTORS)s->order[rev][s->order_n[rev]++]=(uint8_t)i;
        else{s->order_lost|=mask;s->limitations|=PROTECT_OVERFLOW;if(s->observation_dropped!=UINT32_MAX)s->observation_dropped++;}
    }
    unsigned length=q->format==2?512u:q->format==6?525u:q->format==4 || q->format==5 || q->format==7?256u:
                    id->n<=3?128u<<id->n:0u;
    verify_fingerprint_t local;
    if(!fingerprint){(void)ufi_fingerprint(data,length,length,&local);fingerprint=&local;}
    uint32_t crc=fingerprint->prefix_crc,hash=fingerprint->prefix_hash;
    bool good=accepted && !(flags&MFM_SF_DATA_CHECKSUM) && !q->dec->field_bad && length;
    if(good){
        if(!a->valid){a->crc=crc;a->hash=hash;}
        else if(a->crc!=crc || a->hash!=hash)a->changed|=mask;
        a->valid|=mask;
    }
    if(flags&MFM_SF_DATA_CHECKSUM){
        /* Retain variants independently of arrival order: A,B,B is repeated B,
         * even though the first observation A occurred only once. */
        unsigned v;bool have=false;
        for(v=0;v<s->bad_count;v++)if(s->bad_variants[v].sector==i){
            have=true;
            if(s->bad_variants[v].crc==crc && s->bad_variants[v].hash==hash)break;
        }
        if(v==s->bad_count){
            if(have)a->bad_changed|=mask;
            if(v==QUALITY_SECTORS){s->limitations|=PROTECT_OVERFLOW;if(s->observation_dropped!=UINT32_MAX)s->observation_dropped++;}
            else{s->bad_variants[v]=(protection_bad_variant_t){crc,hash,(uint8_t)i,0};s->bad_count++;}
        }
        if(length && v<s->bad_count)s->bad_variants[v].revs|=mask;
        a->bad|=mask;
    }
    if(q->dec->field_bad)a->coding|=mask;
    if(!q->position && (id->c!=(q->format==5?(uint8_t)(q->track+1u):q->track) ||
       (q->format!=5 && q->format!=7 && id->h!=q->side)))a->header_mismatch|=mask;
    a->seen|=mask;
}
void protection_sector(quality_t* q,const mfm_id_t* id,const uint8_t* data,bool accepted){
    protection_sector_hashed(q,id,data,accepted,NULL);
}
/* Modern Copylock sector 6: raw 0x8914, decoded ID6, 16 signature bytes,
 * 496 bytes from a nonzero 23-bit LFSR, zero footer. Reference: Keir Fraser's
 * public-domain disk-utilities/libdisk/format/amiga/copylock.c. No track reconstruction. */
static uint32_t previous_lfsr(uint32_t x){return (x>>1)|((((x>>1)^x)&1u)<<22);}
static bool copylock_data(const uint8_t* p){
    static const uint8_t signature[]={0x52,0x6f,0x62,0x20,0x4e,0x6f,0x72,0x74,0x68,0x65,0x6e,0x20,0x43,0x6f,0x6d,0x70};
    if(memcmp(p,signature,sizeof(signature)))return false;
    uint32_t x=(uint32_t)p[256]<<15|(uint32_t)p[264]<<7|p[272]>>1;
    for(unsigned i=16;i<256;i++)x=previous_lfsr(x);
    if(!x)return false;
    for(unsigned i=16;i<512;i++){
        if(p[i]!=(uint8_t)(x>>15))return false;
        x=((x<<1)&0x7fffffu)|(((x>>22)^x)&1u);
    }
    return true;
}
static void signature_bit(protection_state_t* s,quality_t* q,unsigned bit){
    s->shift=(s->shift<<1)|bit;
    if(!s->scan_state){
        if((uint16_t)s->shift==0x8914u){s->scan_state=1;s->scan_bits=0;s->scan_bytes=0;s->scan_previous=0;s->scan_start=q->extra.position;}
        return;
    }
    if(++s->scan_bits<16)return;
    s->scan_bits=0;uint16_t word=(uint16_t)s->shift;uint8_t byte=0;
    for(int k=7;k>=0;k--){
        unsigned data=(word>>(2*k))&1u,clock=(word>>(2*k+1))&1u;
        if(clock!=!(s->scan_previous || data)){s->scan_state=0;return;}
        s->scan_previous=(uint8_t)data;byte=(uint8_t)((byte<<1)|data);
    }
    if(s->scan_state==1){s->scan_state=byte==6?2:0;return;}
    if(s->scan_bytes<512){s->signature_data[s->scan_bytes++]=byte;return;}
    if(!byte && copylock_data(s->signature_data)){
        s->signature_revs|=(uint8_t)(1u<<q->revolution);
        uint32_t period=q->revolution_ticks[q->revolution];
        s->signature_position=period?(uint32_t)((uint64_t)s->scan_start*q->revolution_ticks[0]/period):s->scan_start;
    }
    s->scan_state=0;
}
void protection_interval(quality_t* q,uint32_t ticks){
    protection_state_t* s=q->protection;
    if(!s || !(s->options.flags&PROTECT_SIGNATURE) || q->profile || q->revolution>=8)return;
    if(s->scan_rev!=q->revolution){
        s->scan_rev=q->revolution;s->scan_period=(MFM_TICK_HZ/500000u)*256u;
        s->scan_phase=0;s->shift=0;s->scan_state=s->scan_bits=0;
    }
    s->scanned|=(uint8_t)(1u<<q->revolution);
    if(ticks>q->extra.position){s->scan_state=0;s->shift=0;s->scan_phase=0;return;}
    int64_t t=(int64_t)ticks*256+s->scan_phase;
    int64_t n=(t+s->scan_period/2)/s->scan_period;
    if(n<1 || n>8){s->scan_state=0;s->shift=0;s->scan_phase=0;return;}
    int64_t error=t-n*s->scan_period;
    int64_t p=s->scan_period+error/(n*16);
    const int64_t nominal=(MFM_TICK_HZ/500000u)*256u;
    if(p<nominal*7/8)p=nominal*7/8;
    if(p>nominal*9/8)p=nominal*9/8;
    s->scan_period=(uint32_t)p;s->scan_phase=(int32_t)(error/2);
    for(int64_t k=1;k<n;k++)signature_bit(s,q,0);
    signature_bit(s,q,1);
}
static uint8_t complete_revs(const quality_t* q){
    return q->revs && q->revs<=8?(uint8_t)(q->extra.completed&((1u<<q->revs)-1u)):0;
}
static void sectors_finish(protection_state_t* s,const quality_t* q,uint8_t completed){
    unsigned minimum=s->options.min_revs;uint16_t f=s->options.flags;
    bool enough=bits(completed)>=minimum;
    uint16_t categories=PROTECT_CONTENT|PROTECT_ERRORS|PROTECT_LAYOUT|PROTECT_POSITION;
    if(!(f&categories))return;
    if(!s->n){s->limitations|=PROTECT_NO_SECTORS;s->insufficient|=f&categories;return;}
    if(!enough)s->insufficient|=f&categories;else s->checked|=f&categories;
    if(q->ambiguous)s->insufficient|=f&categories;
    uint8_t first_n=s->sectors[0].id.n;bool mixed=false;
    for(unsigned k=0;k<s->n;k++){
        const protection_sector_t* a=&s->sectors[k];uint8_t seen=(uint8_t)(a->seen&completed),valid=(uint8_t)(a->valid&completed);
        if(bits(seen)<minimum)s->insufficient|=f&categories;
        if(a->duplicates)s->insufficient|=f&(PROTECT_CONTENT|PROTECT_POSITION);
        uint32_t position=0;for(unsigned r=0;r<q->revs;r++)if(seen&(1u<<r)){
            position=q->revolution_ticks[r]?(uint32_t)((uint64_t)a->positions[r]*q->revolution_ticks[0]/q->revolution_ticks[r]):0;break;
        }
        uint8_t level=enough && !q->ambiguous?2:1;
        if((f&PROTECT_CONTENT) && !a->duplicates && a->changed && bits(valid)>=minimum)
            add(s,PR_CONTENT,PROTECT_CONTENT,&a->id,position,0,bits(valid),minimum,valid,level,0,0,bits(valid));
        if((f&PROTECT_CONTENT) && !a->duplicates && a->bad_changed && bits(a->bad&completed)>=minimum)
            add(s,PR_CONTENT_BAD_CRC,PROTECT_CONTENT,&a->id,position,0,bits(a->bad&completed),minimum,(uint8_t)(a->bad&completed),level,4,0,bits(a->bad&completed));
        if(f&PROTECT_ERRORS){
            uint8_t stable=0;
            for(unsigned v=0;v<s->bad_count;v++)if(s->bad_variants[v].sector==k){
                uint8_t support=(uint8_t)(s->bad_variants[v].revs&completed);
                if(bits(support)>bits(stable))stable=support;
            }
            if(bits(stable)>=s->options.min_repeat)add(s,PR_STABLE_BAD_CRC,PROTECT_ERRORS,&a->id,position,0,bits(stable),s->options.min_repeat,stable,level,0,0,bits(stable));
            if(a->coding&completed)add(s,PR_CODING,PROTECT_ERRORS,&a->id,position,0,bits(a->coding&completed),0,(uint8_t)(a->coding&completed),1,0,0,bits(a->coding&completed));
        }
        if(f&PROTECT_LAYOUT){
            uint8_t duplicate=(uint8_t)(a->duplicates&completed);
            if(duplicate)add(s,PR_DUPLICATE,PROTECT_LAYOUT,&a->id,position,0,bits(duplicate),minimum,duplicate,bits(duplicate)>=minimum?level:1,0,0,bits(duplicate));
            if(a->header_mismatch&completed)add(s,PR_HEADER_POSITION,PROTECT_LAYOUT,&a->id,position,0,
                (uint32_t)a->id.c<<8|a->id.h,((uint32_t)(q->format==5?q->track+1u:q->track)<<8)|
                (q->format==5 || q->format==7?0:q->side),(uint8_t)(a->header_mismatch&completed),1,0,3,bits(a->header_mismatch&completed));
            if(a->id.n!=first_n)mixed=true;
        }
        if((f&PROTECT_POSITION) && !a->duplicates && enough && bits(seen)>=minimum){
            uint32_t origin=0,period=0,maximum=0;bool have=false;
            for(unsigned r=0;r<q->revs;r++)if((seen&(1u<<r)) && q->revolution_ticks[r]){
                uint32_t t=a->positions[r];if(t>q->revolution_ticks[r])continue;
                if(!period)period=q->revolution_ticks[0]?q->revolution_ticks[0]:q->revolution_ticks[r];
                t=(uint32_t)((uint64_t)t*period/q->revolution_ticks[r]);
                if(!have){origin=t;have=true;continue;}
                uint32_t d=distance(t,origin,period);if(d>maximum)maximum=d;
            }
            if(period && maximum>period/100u)add(s,PR_POSITION,PROTECT_POSITION,&a->id,position,0,maximum,period/100u,seen,1,1,1,bits(seen));
        }
    }
    if(mixed)add(s,PR_MIXED_LENGTH,PROTECT_LAYOUT,NULL,0,0,s->n,first_n,completed,1,2,3,s->n);
    if(!(f&PROTECT_POSITION) || !enough)return;
    /* Compare circular order only for identical complete, unique ID sets. */
    int baseline=-1;uint8_t changed=0;uint64_t baseline_set=0;
    for(unsigned r=0;r<q->revs;r++)if(completed&(1u<<r)){
        unsigned count=s->order_n[r];uint64_t set=0;bool unique=true;
        for(unsigned k=0;k<count;k++){uint64_t bit=1ull<<s->order[r][k];if(set&bit)unique=false;set|=bit;}
        if(count<3 || !unique || (s->order_lost&(1u<<r)))continue;
        if(baseline<0){baseline=(int)r;baseline_set=set;continue;}
        if(set!=baseline_set || count!=s->order_n[baseline]){s->insufficient|=PROTECT_POSITION;continue;}
        unsigned shift=0;while(shift<count && s->order[r][shift]!=s->order[baseline][0])shift++;
        for(unsigned k=0;k<count;k++)if(s->order[r][(shift+k)%count]!=s->order[baseline][k]){changed|=(uint8_t)(1u<<r);break;}
    }
    if(changed)add(s,PR_ORDER,PROTECT_POSITION,NULL,0,0,bits(changed),0,changed,1,2,0,bits(changed));
}
static void timing_finish(protection_state_t* s,const quality_t* q,uint8_t completed){
    uint32_t means[QUALITY_LOCAL_WINDOWS];unsigned n=0;
    for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++){
        const quality_local_t* w=&q->local[k];
        if(w->count<64 || !(w->multiples&(w->multiples-1u)) || (uint64_t)w->rejected*10>w->count ||
           (uint64_t)(w->max-w->min)*20>w->sum/w->count)continue;
        uint32_t mean=(uint32_t)(w->sum/w->count);unsigned i=n++;
        while(i && means[i-1]>mean){means[i]=means[i-1];i--;}means[i]=mean;
    }
    uint32_t baseline=n?means[n/2]:0;
    if(n<3 || bits(completed)<s->options.min_revs || q->ambiguous){s->insufficient|=PROTECT_TIMING;s->limitations|=PROTECT_NO_TIMING;}
    else s->checked|=PROTECT_TIMING;
    if(baseline && n>=3 && !q->ambiguous)for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++){
        const quality_local_t* w=&q->local[k];if(!w->count)continue;
        uint32_t mean=(uint32_t)(w->sum/w->count),delta=mean>baseline?mean-baseline:baseline-mean;
        bool stable=w->count>=64 && (w->multiples&(w->multiples-1u)) && (uint64_t)w->rejected*10<=w->count &&
                    (uint64_t)(w->max-w->min)*20<=mean;
        if(stable && (uint64_t)delta*1000>=(uint64_t)baseline*s->options.timing_permille)
            add(s,PR_RATE_CHANGE,PROTECT_TIMING,NULL,(uint32_t)((uint64_t)q->revolution_ticks[0]*k/32),q->revolution_ticks[0]/32,
                (uint32_t)((uint64_t)delta*1000/baseline),s->options.timing_permille,completed,1,1,2,w->count);
    }
    uint8_t gaps=0;uint32_t maximum=0,threshold=0;
    for(unsigned r=0;r<q->revs;r++)if((completed&(1u<<r)) && q->revolution_ticks[r] && q->max_gap[r]>=q->revolution_ticks[r]/8){
        gaps|=(uint8_t)(1u<<r);if(q->max_gap[r]>maximum){maximum=q->max_gap[r];threshold=q->revolution_ticks[r]/8;}
    }
    if(gaps)add(s,PR_LONG_GAP,PROTECT_TIMING,NULL,0,0,maximum,threshold,gaps,1,2,1,bits(gaps));
}
void protection_finish(quality_t* q,const track_compare_t* comparison){
    protection_state_t* s=q->protection;if(!s)return;
    s->count=0;s->dropped=s->observation_dropped;s->checked=s->insufficient=0;s->state=PROTECT_NO_INDICATOR;
    if(!q->revs || q->revs>8){s->insufficient=s->options.flags;s->limitations|=PROTECT_INCOMPLETE;s->state=PROTECT_INSUFFICIENT;return;}
    uint8_t completed=complete_revs(q);uint16_t f=s->options.flags;
    if(q->status || bits(completed)!=q->revs || q->dropped || q->extra.saturated){s->limitations|=PROTECT_INCOMPLETE;s->insufficient=f;}
    if(bits(completed)<s->options.min_revs)s->limitations|=PROTECT_FEW_REVS;
    if(q->ambiguous)s->limitations|=PROTECT_AMBIGUOUS;
    sectors_finish(s,q,completed);
    if((f&PROTECT_LAYOUT) && q->details)for(unsigned i=0;i<q->details->count;i++){
        const sector_detail_t* d=&q->details->sectors[i];
        if((q->format==1 || q->format==3) && d->id.n>3 && (d->checked&SF_HEADER_BAD) &&
           !(d->findings&(SF_HEADER_BAD|SF_NO_HEADER)) && (d->findings&SF_LENGTH))
            add(s,PR_HEADER_LENGTH,PROTECT_LAYOUT,&d->id,d->offset,0,d->id.n,3,d->revs,1,8,3,bits(d->revs));
    }
    if(f&PROTECT_TIMING)timing_finish(s,q,completed);
    if(f&PROTECT_CONTENT){
        if(!comparison || comparison->generation!=q->generation || bits(comparison->completed)<s->options.min_revs-1u){
            s->insufficient|=PROTECT_CONTENT;s->limitations|=PROTECT_NO_COMPARE;
        }else{
            s->limitations|=PROTECT_ALIGNMENT;
            for(unsigned k=0;k<COMPARE_WINDOWS;k++){
                const compare_window_t* w=&comparison->windows[k];if(!w->missing && !w->extra)continue;
                uint64_t total=(uint64_t)w->missing+w->extra;
                add(s,PR_FLUX_VARIATION,PROTECT_CONTENT,NULL,(uint32_t)((uint64_t)comparison->reference_period*k/128),comparison->reference_period/128,
                    total>UINT32_MAX?UINT32_MAX:(uint32_t)total,comparison->tolerance,w->revolutions,1,1,0,bits(w->revolutions));
            }
        }
    }
    if(f&PROTECT_SIGNATURE){
        if(bits(s->scanned&completed)<s->options.min_revs)s->insufficient|=PROTECT_SIGNATURE;
        else s->checked|=PROTECT_SIGNATURE;
        uint8_t mask=(uint8_t)(s->signature_revs&completed);
        if(mask)add(s,PR_COPYLOCK_SECTOR6,PROTECT_SIGNATURE,NULL,s->signature_position,0,bits(mask),s->options.min_revs,mask,
                    bits(mask)>=s->options.min_revs?3:1,0,0,bits(mask));
    }
    if(s->limitations&PROTECT_OVERFLOW)s->insufficient|=f;
    s->checked&=(uint16_t)~s->insufficient;
    if(s->state==PROTECT_NO_INDICATOR && s->insufficient)s->state=PROTECT_INSUFFICIENT;
}
static void wr(uint8_t* p,uint32_t v){for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(v>>(8*k));}
uint16_t protection_pack(const quality_t* q,uint8_t first,uint8_t count,uint8_t* out,uint16_t capacity){
    if(!q || !count || count>8)return 0;
    const protection_state_t* s=q->protection;uint8_t total=s?s->count:0;
    if(first>total)return 0;
    if(count>total-first)count=(uint8_t)(total-first);
    uint16_t n=(uint16_t)(32+32*count);if(capacity<n)return 0;
    memset(out,0,n);out[0]=1;out[1]=s?s->state:PROTECT_DISABLED;out[2]=first;out[3]=count;wr(out+4,q->generation);
    out[23]=q->extra.completed;out[24]=q->format;out[25]=q->track;out[26]=q->side;out[27]=q->position;out[30]=q->status;
    if(s){
        out[8]=(uint8_t)s->options.flags;out[9]=(uint8_t)(s->options.flags>>8);out[10]=(uint8_t)s->checked;out[11]=(uint8_t)(s->checked>>8);
        out[12]=(uint8_t)s->insufficient;out[13]=(uint8_t)(s->insufficient>>8);out[14]=(uint8_t)s->limitations;out[15]=(uint8_t)(s->limitations>>8);
        wr(out+16,s->dropped);out[20]=s->count;out[21]=s->options.min_revs;out[22]=s->options.min_repeat;
        out[23]=q->extra.completed;out[24]=q->format;out[25]=q->track;out[26]=q->side;out[27]=q->position;
        out[28]=(uint8_t)s->options.timing_permille;out[29]=(uint8_t)(s->options.timing_permille>>8);out[30]=q->status;
    }
    for(unsigned k=0;k<count;k++){
        const protection_record_t* r=&s->records[first+k];uint8_t* p=out+32+32*k;
        p[0]=(uint8_t)r->rule;p[1]=(uint8_t)(r->rule>>8);p[2]=(uint8_t)r->category;p[3]=(uint8_t)(r->category>>8);
        wr(p+4,r->position);wr(p+8,r->extent);wr(p+12,r->value);wr(p+16,r->reference);memcpy(p+20,&r->id,4);
        p[24]=r->revs;p[25]=r->level;p[26]=r->qualifiers;p[27]=r->unit;wr(p+28,r->support);
    }
    return n;
}
