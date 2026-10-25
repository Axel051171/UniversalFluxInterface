/* On-device work over the shared, completed U11/fallback capture. */
#include "ufi_firmware.h"
#include "ufi_quality.h"
#include "ufi_cancel.h"
#include "ufi_compare.h"
#include "ufi_protection.h"
#include "ufi_metrics.h"
#include "ufi_profile.h"
#include "ufi_capabilities.h"
#include <string.h>

extern capture_context_t g_capture;
static quality_t result;
__attribute__((section(".sram1"),aligned(4))) static sector_details_t sector_details;
/* CPU-only scratch; every pass initializes the decoder before reading it. */
__attribute__((section(".sram1"),aligned(4))) static mfm_decoder_t decoder;
static uint32_t started;
static bool available;
static uint32_t generation;
static uint32_t budget_ms=10000;
__attribute__((section(".evidence"),aligned(32))) static track_compare_t comparison;
__attribute__((section(".backup"),aligned(8))) static compare_stats_t comparison_stats[COMPARE_WINDOWS];
static bool comparison_available,comparison_active;
static uint8_t comparison_rev;
static protection_options_t protection_config={0,30,3,2};
static struct {uint32_t media,config,decoder,rate;uint8_t format,zone;} hint;
static uint8_t hint_zone(uint8_t format,uint8_t track){
    return format==6?track/16u:format==5?(track<17?0:track<24?1:track<30?2:3):0;
}
static uint32_t decoder_key(void){
    apple_gcr_options_t apple=mfm_apple_get_options();
    uint32_t crc=ufi_crc32(mfm_get_tolerance(),(const uint8_t*)&apple,sizeof apple);
    return ufi_crc32(crc,mfm_apple_get_table(),64);
}
__attribute__((section(".backup"),aligned(8))) static protection_state_t protection_state;
int ufi_protection_options(const uint8_t* p,uint16_t n,uint8_t* out){
    if(n!=1 && !protection_options_decode(p,n,&protection_config))return UFI_ERR_BAD_ARGS;
    memset(out,0,24);out[0]=1;out[1]=56;out[2]=PROTECT_MASK;
    out[4]=(uint8_t)protection_config.flags;out[5]=(uint8_t)(protection_config.flags>>8);
    out[6]=protection_config.min_revs;out[7]=protection_config.min_repeat;
    out[8]=(uint8_t)protection_config.timing_permille;out[9]=(uint8_t)(protection_config.timing_permille>>8);
    out[10]=PROTECT_RECORDS;out[11]=8;out[12]=32;out[13]=32;out[14]=1; /* rule catalog v1 */
    return UFI_OK;
}
uint16_t ufi_compare_report(uint8_t* out,uint16_t capacity,uint32_t id){
    return comparison_available && comparison.generation==id?track_compare_pack(&comparison,out,capacity):0;
}
uint16_t ufi_compare_stats_report(uint8_t* out,uint16_t capacity,uint32_t id,uint8_t first,uint8_t count){
    return comparison_available && comparison.generation==id?track_compare_stats_pack(&comparison,first,count,out,capacity):0;
}
bool ufi_quality_variation_mask(uint32_t* mask){
    if(!mask || !available || !comparison_available || comparison.generation!=result.generation || result.revs<2 || result.status)return false;
    *mask=0;
    for(unsigned k=0;k<32;k++){
        uint64_t differences=0;
        for(unsigned j=4*k;j<4*k+4;j++)differences+=(uint64_t)comparison.windows[j].missing+comparison.windows[j].extra;
        if(differences>=3)*mask|=1u<<k;
    }
    return true;
}
uint16_t ufi_local_report(uint8_t* out,uint16_t capacity,uint32_t id){
    if(!available || result.generation!=id)return 0;
    uint16_t n=quality_local_pack(&result,out,capacity);if(!n)return 0;
    if(comparison_available && comparison.generation==id){
        out[14]=comparison.completed;
        for(unsigned k=0;k<QUALITY_LOCAL_WINDOWS;k++){
            uint64_t missing=0,extra=0;
            for(unsigned j=k*4;j<k*4+4;j++){missing+=comparison.windows[j].missing;extra+=comparison.windows[j].extra;}
            uint32_t m=missing>UINT32_MAX?UINT32_MAX:(uint32_t)missing;
            uint32_t e=extra>UINT32_MAX?UINT32_MAX:(uint32_t)extra;
            uint8_t* p=out+32+40*k;
            if(m)p[0]|=16;
            if(e)p[0]|=32;
            for(unsigned j=0;j<4;j++){p[32+j]=(uint8_t)(m>>(8*j));p[36+j]=(uint8_t)(e>>(8*j));}
        }
    }
    return n;
}
/* Per-sector outcome of the last verified WRITE (op 40, 1.18): schema, format, count,
 * flags, track byte as sent, side, then c, h, r, n, state per reference sector.  The
 * header is written when the verified WRITE is accepted (flags bit3 = read-back missing
 * until the matcher fills the rest); a WRITE without verify leaves it untouched. */
__attribute__((section(".dtcm"))) static uint8_t verify_report[VERIFY_REPORT_MAX];   /* DTCM: keeps the AXI reserve */
static bool verify_report_ready; /* .bss: unlike DTCM, reliably cleared at reset */

void ufi_quality_clear(void) {available=false;}

void ufi_quality_verify_begin(uint8_t track,uint8_t side) {
    memset(verify_report,0,sizeof verify_report);
    verify_report[0]=1;verify_report[3]=8;verify_report[4]=track;verify_report[5]=side;
    verify_report_ready=true;
}

uint16_t ufi_quality_verify_report(uint8_t* out) {
    if(!verify_report_ready){memset(out,0,6);out[0]=1;return 6;}
    const uint16_t n=(uint16_t)(6u+5u*verify_report[2]);
    memcpy(out,verify_report,n);out[0]=1;return n;   /* schema 1 also before the first verified WRITE (NOLOAD section) */
}

static int progress(void) {
    UFI_WATCHDOG_FEED();
    if (ufi_cancel_requested()) return UFI_ERR_CANCELLED;
    return HAL_GetTick()-started>budget_ms ? UFI_ERR_TIMEOUT : UFI_OK;
}
static void scout(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok) {
    (void)id;(void)data;
    if (ok && !decoder.field_bad && !(decoder.sector_flags&4)) (*(uint32_t*)ctx)++;
}
static void strict_scout(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok){
    if(decoder.sector_flags&(4u|MFM_SF_DATA_CHECKSUM|MFM_SF_HEADER_CHECKSUM|MFM_SF_NO_HEADER))return;
    scout(ctx,id,data,ok);
}
static int feed(bool details,uint8_t profile,uint8_t revs) {
    uint64_t carry=0;
    for (uint8_t rev=0;rev<revs;rev++) {
        int check=progress();if(check)return check;
        const flux_revolution_t* r=ufi_flux_get_revolution(rev);
        if (!r || !r->index_time) return UFI_ERR_DMA;
        uint32_t previous=0;
        if (details) {
            quality_revolution(&result,rev);
            result.revolution_ticks[rev]=r->index_time;result.flux_count[rev]=r->count;
        }
        for (uint32_t i=0;i<r->count;i++) {
            if (!(i&1023u)) {int ret=progress();if(ret) return ret;}
            uint32_t timestamp=r->samples[i].timestamp;
            if (timestamp<=previous || timestamp>r->index_time) return UFI_ERR_DMA;
            uint64_t ticks=timestamp-previous+carry;
            if (ticks>UINT32_MAX) return UFI_ERR_RANGE;
            if (details) {
                if (!profile && ticks>result.max_gap[rev]) result.max_gap[rev]=(uint32_t)ticks;
                result.extra.position=timestamp;result.extra.index=i;
                quality_interval(&result,(uint32_t)ticks);
            } else mfm_dec_interval(&decoder,(uint32_t)ticks);
            carry=0;previous=timestamp;
        }
        carry+=r->index_time-previous;
        if (details) {
            if (!profile && r->index_time-previous>result.max_gap[rev])
                result.max_gap[rev]=r->index_time-previous;
            quality_tail(&result,previous,r->index_time);
            quality_end_revolution(&result);
        }
    }
    return UFI_OK;
}

static int run(uint8_t format,uint32_t rate,bool adaptive,const verify_ref_t* ref,bool count_ok,uint8_t* level,const quality_options_t* options) {
    started=HAL_GetTick();available=true;
    uint8_t revs=ufi_flux_get_revolution_count();
    quality_init(&result,format,rate,revs);
    quality_details_attach(&result,&sector_details);
    if(options)result.extra.options=*options;
    budget_ms=options?options->budget_ms:10000u;
    result.track=g_capture.current_track;result.side=g_capture.current_side;result.position=g_capture.current_quarter;
    result.generation=++generation;
    if(options)protection_begin(&protection_state,&result,&protection_config);
    if (!revs || revs>QUALITY_REVS || g_capture.error_code) return UFI_ERR_BUFFER_FULL;
    if (!format || !rate) {
        static const uint32_t rates[]={250000,300000,500000,1000000,250000,300000,500000,1000000,
                                       125000,150000,250000,300000,500000,500000,250000,266667,285714,307692,500000,380228,346021,312500,280112,251256};
        uint32_t best=0;uint8_t chosen=0;uint32_t chosen_rate=0;
        for (uint8_t k=0;k<QUALITY_CANDIDATES;k++) {
            uint8_t f=k<4?1:k<8?2:k<13?3:k==13?4:k<18?5:6;
            if(f==6 && k>18 && !rate && (result.track>=80 || k!=19+result.track/16))continue;
            if ((format && f!=format) || (rate && rates[k]!=rate)) continue;
            uint32_t score=0;
            mfm_dec_init_format(&decoder,f,rates[k],scout,&score);
            int ret=feed(false,0,revs>2u?2u:revs);if(ret) return ret;
            result.candidates[k]=(uint16_t)(score>65535u?65535u:score);
            if (score>best) {best=score;chosen=f;chosen_rate=rates[k];result.ambiguous=0;}
            else if (score && score==best) result.ambiguous=1;
        }
        result.format=chosen;result.rate=chosen_rate;
    }
    uint64_t found=0;bool mismatch=false;
    verify_match_t match;
    uint8_t state[VERIFY_MAX_SECTORS];memset(state,0,sizeof state);
    static const uint8_t rank[4]={0,3,1,2};  /* ok beats different beats bad CRC beats missing */
    for (uint8_t p=0;p<(adaptive && result.format?3u:1u);p++) {
        if (ref) verify_match_begin(&match,ref,&decoder);
        quality_begin(&result,&decoder,p,ref?&match:NULL);
        int ret=feed(true,p,revs);if(ret) return ret;
        if (ref) {
            found|=match.found;mismatch|=match.mismatch;
            for (uint8_t i=0;i<ref->n && i<VERIFY_MAX_SECTORS;i++)
                if (rank[match.state[i]&3u]>rank[state[i]&3u]) state[i]=match.state[i];
        }
    }
    if (ref) {
        match.found=found;match.mismatch=mismatch;
        *level=verify_match_result(&match,count_ok);
        if(!verify_report_ready)ufi_quality_verify_begin(result.track,result.side);
        /* op 40 report: the per-sector outcome survives the level decision */
        const uint8_t n=ref->n>VERIFY_MAX_SECTORS?VERIFY_MAX_SECTORS:ref->n;
        verify_report[0]=1;verify_report[1]=ref->format?ref->format:(ref->amiga?2u:1u);verify_report[2]=n;
        verify_report[3]=(uint8_t)((ref->overflow?1u:0u)|(ref->conflicting?2u:0u)|(*level==VERIFY_CONTENT?4u:0u));
        for (uint8_t i=0;i<n;i++) {              /* track/side at 4/5 stay from ufi_quality_verify_begin() */
            uint8_t* e=verify_report+6+5u*i;
            e[0]=ref->s[i].c;e[1]=ref->s[i].h;e[2]=ref->s[i].r;e[3]=ref->s[i].n;e[4]=state[i];
        }
        if (!*level) return UFI_ERR_DMA;
    }
    return UFI_OK;
}

int ufi_quality_analyze(uint8_t format,uint32_t rate,bool adaptive) {
    int ret=run(format,rate,adaptive,NULL,false,NULL,NULL);
    result.status=(uint8_t)(ret<0?-ret:ret);result.elapsed_ms=HAL_GetTick()-started;return ret;
}
int ufi_quality_verify(const verify_ref_t* ref,bool count_ok,uint8_t* level) {
    int ret=run(ref->format?ref->format:(ref->amiga?2u:1u),ref->rate_bps,true,ref,count_ok,level,NULL);
    result.status=(uint8_t)(ret<0?-ret:ret);result.elapsed_ms=HAL_GetTick()-started;return ret;
}
uint16_t ufi_quality_report(uint8_t* out,uint16_t capacity) {
    return available?quality_pack(&result,out,capacity):0;
}

int ufi_quality_analyze_options(const quality_options_t* options) {
    if(options->features&QUALITY_FEATURE_FAST_HINT){
        int ret=ufi_quality_async_start(options);if(ret)return ret;
        do{ret=ufi_quality_async_step();}while(ret==UFI_ERR_BUSY);
        return ret;
    }
    int ret=run(options->format,options->rate,(options->flags&2)!=0,NULL,false,NULL,options);
    result.status=(uint8_t)(ret<0?-ret:ret);result.elapsed_ms=HAL_GetTick()-started;
    protection_finish(&result,NULL);return ret;
}


static struct {
    bool active,details,rev_started,hint;
    uint8_t candidate,profile,rev,revs,chosen;
    uint32_t edge,previous,score,best,chosen_rate;
    uint64_t carry;
    quality_options_t options;
    const verify_ref_t* reference;
    verify_match_t match;
    uint64_t found;
    uint8_t states[VERIFY_MAX_SECTORS],level;
    bool mismatch,count_ok;
} cooperative;
static const uint32_t detection_rates[]={250000,300000,500000,1000000,250000,300000,500000,1000000,
                                         125000,150000,250000,300000,500000,500000,250000,266667,285714,307692,500000,380228,346021,312500,280112,251256};
static int async_finish(int ret) {
    if(!ret && cooperative.reference){
        const verify_ref_t* ref=cooperative.reference;
        cooperative.match.found=cooperative.found;cooperative.match.mismatch=cooperative.mismatch;
        cooperative.level=verify_match_result(&cooperative.match,cooperative.count_ok);
        if(!verify_report_ready)ufi_quality_verify_begin(result.track,result.side);
        verify_report[1]=ref->format?ref->format:ref->amiga?2:1;verify_report[2]=ref->n;
        verify_report[3]=(uint8_t)((ref->overflow?1:0)|(ref->conflicting?2:0)|(cooperative.level==VERIFY_CONTENT?4:0));
        for(unsigned i=0;i<ref->n;i++){
            uint8_t* e=verify_report+6+5*i;memcpy(e,&ref->s[i],4);e[4]=cooperative.states[i];
        }
        if(!cooperative.level)ret=UFI_ERR_DMA;
    }
    cooperative.active=false;
    comparison_active=false;
    result.status=(uint8_t)(ret<0?-ret:ret);result.elapsed_ms=HAL_GetTick()-started;
    if(!ret && !cooperative.reference && (cooperative.options.features&QUALITY_FEATURE_FAST_HINT) &&
       !protection_config.flags && result.format){
        unsigned good=0;for(unsigned i=0;i<result.n;i++)if(result.sectors[i].nominal &&
            !(result.sectors[i].flags&(21u|MFM_SF_DATA_CHECKSUM|MFM_SF_HEADER_CHECKSUM|MFM_SF_NO_HEADER)))good++;
        if(good>=2){
            hint.media=ufi_flux_archive_media();hint.config=ufi_config_generation();hint.decoder=decoder_key();
            hint.format=result.format;hint.rate=result.rate;hint.zone=hint_zone(result.format,result.track);
        }
    }
    protection_finish(&result,comparison_available && comparison.generation==result.generation?&comparison:NULL);
    return ret;
}
static void async_pass(void) {
    cooperative.rev=0;cooperative.edge=cooperative.previous=0;cooperative.carry=0;cooperative.rev_started=false;
}
static bool async_candidate(void) {
    while(cooperative.candidate<QUALITY_CANDIDATES) {
        uint8_t k=cooperative.candidate,f=k<4?1:k<8?2:k<13?3:k==13?4:k<18?5:6;
        if(f==6 && k>18 && !cooperative.options.rate && (result.track>=80 || k!=19+result.track/16)){
            cooperative.candidate++;continue;
        }
        if((!cooperative.options.format || cooperative.options.format==f) &&
           (!cooperative.options.rate || cooperative.options.rate==detection_rates[k])) {
            cooperative.score=0;
            ufi_metric_add(&ufi_metrics.format_candidates,1);
            mfm_dec_init_format(&decoder,f,detection_rates[k],cooperative.hint?strict_scout:scout,&cooperative.score);
            async_pass();return true;
        }
        cooperative.candidate++;
    }
    return false;
}
int ufi_quality_async_archive(const quality_options_t* options,const uint8_t position[3]) {
    if(cooperative.active)return UFI_ERR_BUSY;
    memset(&cooperative,0,sizeof(cooperative));cooperative.options=*options;
    comparison_available=comparison_active=false;
    started=HAL_GetTick();budget_ms=options->budget_ms;available=true;
    cooperative.revs=ufi_flux_get_revolution_count();
    quality_init(&result,options->format,options->rate,cooperative.revs);result.extra.options=*options;
    quality_details_attach(&result,&sector_details);
    result.generation=++generation;result.track=g_capture.current_track;result.side=g_capture.current_side;result.position=g_capture.current_quarter;
    if(position){result.track=position[0];result.side=position[1];result.position=position[2];}
    protection_begin(&protection_state,&result,&protection_config);
    if(protection_config.flags&PROTECT_CONTENT)cooperative.options.report_mask|=1;
    if(!cooperative.revs || cooperative.revs>8 || g_capture.error_code)return async_finish(UFI_ERR_BUFFER_FULL);
    cooperative.active=true;
    if(!options->require_full_search && !options->format && !options->rate && (options->features&QUALITY_FEATURE_FAST_HINT) &&
       !(cooperative.options.report_mask&1) && !protection_config.flags && hint.media && hint.media==ufi_flux_archive_media() &&
       hint.config==ufi_config_generation() && hint.decoder==decoder_key() && hint.zone==hint_zone(hint.format,result.track)){
        for(unsigned k=0;k<QUALITY_CANDIDATES;k++){
            uint8_t f=k<4?1:k<8?2:k<13?3:k==13?4:k<18?5:6;
            if(f==hint.format && detection_rates[k]==hint.rate){cooperative.candidate=(uint8_t)k;cooperative.hint=true;break;}
        }
    }
    if((!options->format || !options->rate) && async_candidate())return UFI_OK;
    cooperative.details=true;quality_begin(&result,&decoder,0,NULL);async_pass();return UFI_OK;
}
int ufi_quality_async_start(const quality_options_t* options){return ufi_quality_async_archive(options,NULL);}
int ufi_quality_async_verify(const verify_ref_t* ref,bool count_ok){
    if(!ref || !ref->n || ref->n>VERIFY_MAX_SECTORS || !ref->rate_bps)return UFI_ERR_BAD_ARGS;
    quality_options_t options={0};options.flags=3;
    options.format=ref->format?ref->format:ref->amiga?2:1;options.rate=ref->rate_bps;options.budget_ms=10000;
    int ret=ufi_quality_async_archive(&options,NULL);if(ret)return ret;
    /* WRITE verification never enables heuristic protection or raw-revolution analysis. */
    result.protection=NULL;cooperative.options.report_mask=0;
    cooperative.reference=ref;cooperative.count_ok=count_ok;
    verify_match_begin(&cooperative.match,ref,&decoder);
    quality_begin(&result,&decoder,0,&cooperative.match);return UFI_OK;
}
uint8_t ufi_quality_async_verify_level(void){return cooperative.level;}
static int async_step(void) {
    if(!cooperative.active)return result.status?-(int)result.status:UFI_OK;
    int ret=progress();if(ret)return async_finish(ret);
    if(comparison_active){
        const flux_revolution_t* a=ufi_flux_get_revolution(0);
        const flux_revolution_t* b=ufi_flux_get_revolution(comparison_rev);
        if(!a || !b)return async_finish(UFI_ERR_DMA);
        if(track_compare_step(&comparison,(const uint32_t*)a->samples,a->count,
                             (const uint32_t*)b->samples,b->count,b->index_time,comparison_rev,256)){
            if(++comparison_rev>=cooperative.revs){comparison_available=true;return async_finish(UFI_OK);}
        }
        return UFI_ERR_BUSY;
    }
    uint32_t slice=HAL_GetTick();
    for(unsigned work=0;work<256;work++) {
        if(!(work&31u) && HAL_GetTick()-slice>=2)return UFI_ERR_BUSY;
        uint8_t limit=cooperative.details?cooperative.revs:(cooperative.revs>2?2:cooperative.revs);
        if(cooperative.rev>=limit) {
            if(!cooperative.details) {
                uint8_t k=cooperative.candidate;uint32_t score=cooperative.score;
                result.candidates[k]=(uint16_t)(score>65535?65535:score);
                if(cooperative.hint){
                    cooperative.hint=false;
                    if(score>=2){
                        cooperative.best=score;cooperative.chosen=hint.format;cooperative.chosen_rate=hint.rate;
                        cooperative.candidate=QUALITY_CANDIDATES;result.hint_used=1;
                        result.ambiguous=1; /* competing decoders were deliberately not exhausted */
                        ufi_metric_add(&ufi_metrics.format_hint_hits,1);
                    }else{cooperative.candidate=0;ufi_metric_add(&ufi_metrics.format_fallbacks,1);}
                }else{
                    if(score>cooperative.best){cooperative.best=score;cooperative.chosen=k<4?1:k<8?2:k<13?3:k==13?4:k<18?5:6;cooperative.chosen_rate=detection_rates[k];result.ambiguous=0;}
                    else if(score && score==cooperative.best)result.ambiguous=1;
                    cooperative.candidate++;
                }
                if(async_candidate())return UFI_ERR_BUSY;
                result.format=cooperative.chosen;result.rate=cooperative.chosen_rate;cooperative.details=true;
            } else {
                if(cooperative.reference){
                    static const uint8_t rank[4]={0,3,1,2};
                    cooperative.found|=cooperative.match.found;cooperative.mismatch|=cooperative.match.mismatch;
                    for(unsigned i=0;i<cooperative.reference->n;i++)
                        if(rank[cooperative.match.state[i]&3u]>rank[cooperative.states[i]&3u])cooperative.states[i]=cooperative.match.state[i];
                }
                cooperative.profile++;
                if(cooperative.profile>=((cooperative.options.flags&2) && result.format?3:1)){
                    if(cooperative.options.report_mask&1){
                        track_compare_init(&comparison,result.generation,cooperative.options.compare_tolerance,result.revolution_ticks[0]);
                        track_compare_stats_attach(&comparison,comparison_stats);
                        if(cooperative.revs>1){comparison_active=true;comparison_rev=1;return UFI_ERR_BUSY;}
                        comparison_available=true;
                    }
                    return async_finish(UFI_OK);
                }
            }
            if(cooperative.reference)verify_match_begin(&cooperative.match,cooperative.reference,&decoder);
            quality_begin(&result,&decoder,cooperative.profile,cooperative.reference?&cooperative.match:NULL);async_pass();return UFI_ERR_BUSY;
        }
        const flux_revolution_t* r=ufi_flux_get_revolution(cooperative.rev);
        if(!r || !r->index_time)return async_finish(UFI_ERR_DMA);
        if(!cooperative.rev_started) {
            if(cooperative.details){quality_revolution(&result,cooperative.rev);result.revolution_ticks[cooperative.rev]=r->index_time;result.flux_count[cooperative.rev]=r->count;}
            cooperative.rev_started=true;
        }
        if(cooperative.edge>=r->count) {
            cooperative.carry+=r->index_time-cooperative.previous;
            if(cooperative.details) {
                if(!cooperative.profile && r->index_time-cooperative.previous>result.max_gap[cooperative.rev])result.max_gap[cooperative.rev]=r->index_time-cooperative.previous;
                quality_tail(&result,cooperative.previous,r->index_time);quality_end_revolution(&result);
            }
            cooperative.rev++;cooperative.edge=cooperative.previous=0;cooperative.rev_started=false;continue;
        }
        uint32_t t=r->samples[cooperative.edge].timestamp;
        if(t<=cooperative.previous || t>r->index_time)return async_finish(UFI_ERR_DMA);
        uint64_t delta=t-cooperative.previous+cooperative.carry;
        if(delta>UINT32_MAX)return async_finish(UFI_ERR_RANGE);
        if(cooperative.details) {
            result.extra.position=t;result.extra.index=cooperative.edge;
            if(!cooperative.profile && delta>result.max_gap[cooperative.rev])result.max_gap[cooperative.rev]=(uint32_t)delta;
            quality_interval(&result,(uint32_t)delta);
        } else mfm_dec_interval(&decoder,(uint32_t)delta);
        cooperative.carry=0;cooperative.previous=t;cooperative.edge++;
    }
    return UFI_ERR_BUSY;
}
int ufi_quality_async_step(void){
    ufi_profile_mark_t profile=ufi_profile_begin();
    uint32_t before=HAL_GetTick();int ret=async_step();
    ufi_metric_add(&ufi_metrics.quality_steps,1);
    ufi_metric_max(&ufi_metrics.quality_max_ms,HAL_GetTick()-before);
    ufi_profile_end(UFI_CYCLE_DECODER,profile);return ret;
}
void ufi_quality_async_cancel(void){if(cooperative.active)(void)async_finish(UFI_ERR_CANCELLED);}
const quality_t* ufi_quality_result(void){return available?&result:NULL;}
void ufi_quality_async_progress(uint8_t* phase,uint8_t* candidate,uint8_t* rev,uint32_t* edge) {
    *phase=comparison_active?3:cooperative.details?2:1;*candidate=cooperative.details?cooperative.profile:cooperative.candidate;
    *rev=comparison_active?comparison_rev:cooperative.rev;*edge=comparison_active?comparison.j:cooperative.edge;
}
