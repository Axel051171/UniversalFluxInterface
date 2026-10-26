"""Production streaming scheduler: bounded cache work and ring-wrap ordering."""
from test_regressions import BASE, function, run


def main():
    code=BASE+r'''
#define STREAM_WORK_WORDS 1024u
#define ROOM 32u
#define TS_MARGIN 2750
#define DELTA_MAX (1u<<28)
#define FLUX_TIMER_FREQ 275000000u
#define GW_TICK_SHIFT 2u
#define HDR 8u
static bool gw,done;
static uint32_t rd,t_last,fill,cap=8192,now=10000000,produced=4096,consumed;
static uint8_t next_idx=1,cur;
static uint8_t buf[2][8202];
__attribute__((aligned(32))) static uint32_t store[4096];
static unsigned cache_calls,outputs,final_indexes;
static uint32_t last_delta;
static uint32_t ufi_flux_written(void){return produced;}
static uint32_t ufi_flux_now(void){return now;}
static uint32_t* ufi_flux_store(uint32_t* words){*words=4096;return store;}
static void SCB_InvalidateDCache_by_Addr(void* pointer,int32_t n){
    uintptr_t p=(uintptr_t)pointer;
    assert(!(p&31u) && !(n&31) && n>0 && p>=(uintptr_t)store && p+(uint32_t)n<=(uintptr_t)(store+4096));
    cache_calls++;
}
static bool ufi_flux_gw_past_end(uint32_t t){(void)t;return false;}
static void put_indexes_before(uint32_t t,bool final){(void)t;if(final)final_indexes++;}
static void put_delta(uint32_t d){assert(d==1100);last_delta=d;fill+=2;outputs++;}
static void ufi_flux_stream_consumed(uint32_t n){consumed=n;}
static uint32_t ufi_flux_gw_cursor(void){return t_last;}
static void put_n28(uint8_t* out,uint32_t value){(void)out;(void)value;}
static unsigned ufi_flux_index_count(void){return 1;}
static bool lead_skip;static uint32_t lead_start;             /* no lead-in here (4.25) */
static void ufi_flux_lead_in_base(uint32_t pos){(void)pos;assert(0);}
'''+function('ufi_stream.c','encode')+r'''
int main(void){
    for(unsigned i=0;i<4096;i++)store[i]=(i+1u)*1100u;
    encode(true);
    assert(rd==1024 && consumed==1024 && outputs==1024 && cache_calls==1 && ufi_metrics.cache_bytes==4096);
    assert(last_delta==1100 && !final_indexes && ufi_metrics.stream_samples==1024);
    fill=0;encode(true);assert(rd==2048 && outputs==2048 && cache_calls==2);
    rd=4090;produced=4106;t_last=4090u*1100u;fill=0;outputs=cache_calls=0;
    for(unsigned logical=4090;logical<4106;logical++)store[logical%4096]=(logical+1u)*1100u;
    uint32_t before=ufi_metrics.cache_bytes;encode(true);
    assert(rd==4106 && outputs==16 && cache_calls==2 && ufi_metrics.cache_bytes-before==96 && final_indexes==1);
    /* Running capture leaves the newest DMA word alone, then stops at the freshness margin. */
    rd=0;produced=3;t_last=0;fill=outputs=cache_calls=0;now=2200;
    for(unsigned i=0;i<3;i++)store[i]=(i+1u)*1100u;
    encode(false);assert(!rd && !outputs && cache_calls==1);
    now=100000;encode(false);assert(rd==2 && outputs==2 && consumed==2);
    puts("stream: bounded cache slices, chronological ring wrap and DMA freshness passed");
    return 0;
}
'''
    run('regression_stream_runtime',code)
    lead_in()


# Encoder pipeline with the production put_* / encode / lead_trim / lead_open on a synthetic capture
# whose timer wraps inside revolution 0 and whose ring wraps (more samples than words).
LEAD_HARNESS = r'''
#define STREAM_BUF 8192u
#define ROOM 32u
#define STREAM_WORK_WORDS 1024u
#define TS_MARGIN 2750
#define DELTA_MAX (1u<<28)
#define FLUX_TIMER_FREQ 275000000u
#define GW_TICK_SHIFT 2u
#define HDR 8u
#define WORDS 8192u
#define UFI_V2_EVT_READ_PROGRESS 0x85u
#define LEAD_GUARD %s
#define LEAD_TICKS %s
enum {CAPTURE_IDLE,CAPTURE_WAITING_INDEX,CAPTURE_RUNNING,CAPTURE_COMPLETE};
static struct {int state;} g_capture;
static uint8_t buf[2][HDR+STREAM_BUF+2u];
static uint8_t cur;
static uint32_t fill,cap=STREAM_BUF;
static bool done,gw,v2;
static uint32_t rd,t_last,gw_rem,tx_start_ms;
static uint8_t next_idx,n_final;
static uint32_t lead,lead_start,lead_floor;
static bool lead_floored,lead_skip;
__attribute__((aligned(32))) static uint32_t store[WORDS];
static uint32_t produced,consumed,now_ticks,base_pos=UINT32_MAX;
static uint32_t idx_t[8];
static uint8_t idx_n;
static uint32_t ufi_flux_written(void){return produced;}
static uint32_t ufi_flux_now(void){return now_ticks;}
static uint32_t* ufi_flux_store(uint32_t* w){*w=WORDS;return store;}
static void SCB_InvalidateDCache_by_Addr(void* p,int32_t n){
    uintptr_t a=(uintptr_t)p;assert(!(a&31u) && !(n&31) && n>0);
    assert(a>=(uintptr_t)store && a+(uint32_t)n<=(uintptr_t)(store+WORDS));
}
static bool ufi_flux_gw_past_end(uint32_t t){(void)t;return false;}
static uint32_t ufi_flux_gw_cursor(void){return 0;}
static void ufi_flux_stream_consumed(uint32_t n){assert(n>=consumed && n<=produced);consumed=n;}
static uint8_t ufi_flux_index_count(void){return idx_n;}
static uint32_t ufi_flux_index_time(uint8_t k){return idx_t[k];}
static void ufi_flux_lead_in_base(uint32_t pos){assert(base_pos==UINT32_MAX);base_pos=pos;}
static bool ufi_v2_read_progress(void){return false;}
static void ufi_v2_event(uint8_t c,const void* p,uint16_t n){(void)c;(void)p;(void)n;}
static uint32_t HAL_GetTick(void){return 0;}
'''

LEAD_MAIN = r'''
#define N_SAMPLES 30000u
static uint32_t ts[N_SAMPLES];
static uint8_t out[1u<<20];
static uint32_t out_n;
static void flush(void){memcpy(out+out_n,&buf[cur][HDR],fill);out_n+=fill;fill=0;cur^=1u;}
static uint32_t lcg;
static uint32_t rnd(void){lcg=lcg*1103515245u+12345u;return lcg>>8;}
static const uint32_t I0=0xFFF80000u;               /* the timer wraps inside revolution 0 */
/* Same synthetic capture as the golden digest: transitions from 9 M ticks before index 0 */
static void make_capture(void){
    lcg=12345u;uint32_t t=I0-9000000u;
    for(uint32_t n=0;n<N_SAMPLES;n++){
        uint32_t c=rnd()%16u,d;
        d=c<10u?1u+rnd()%239u:c<14u?240u+rnd()%3328u:c<15u?3568u+rnd()%60000u:1u+rnd()%5000u;
        t+=d;ts[n]=t;
    }
    idx_t[0]=I0;
    for(unsigned k=1;k<4;k++)idx_t[k]=I0+k*5000000u+(uint32_t)k*777u;
    uint32_t tie=0;while((int32_t)(ts[tie]-idx_t[2])<0)tie++;idx_t[2]=ts[tie];   /* on a transition */
}
/* Independent decoder of the documented code (USB_Protokoll 2.2/4.25): times relative to index 0 */
static int64_t lead_t[N_SAMPLES],rev_t[N_SAMPLES],fd_t[8];
static uint32_t lead_n,rev_n,fd_n,rev_of[N_SAMPLES];
static int64_t header;
static void decode(bool with_lead){
    uint32_t i=0;int64_t t=0;bool in_lead=false;lead_n=rev_n=fd_n=0;header=-1;
    if(with_lead){
        assert(out_n>=6 && out[0]==0xFF && out[1]==1);
        header=out[2]|(uint32_t)out[3]<<8|(uint32_t)out[4]<<16|(uint32_t)out[5]<<24;
        t=-header;i=6;in_lead=true;
    }
    while(i<out_n){
        uint8_t b=out[i];int64_t d;
        if(b>=1 && b<=0xEF){d=b;i++;}
        else if(b>=0xF0 && b<=0xFC){d=240+((b-0xF0)<<8)+out[i+1];i+=2;}
        else if(b==0xFD || b==0xFE || b==0xFF){
            uint32_t at=i+(b==0xFF?2u:1u);
            uint32_t v=out[at]|(uint32_t)out[at+1]<<8|(uint32_t)out[at+2]<<16|(uint32_t)out[at+3]<<24;
            i=at+4;
            if(b==0xFF){assert(out[at-1]==2 && in_lead && t+v==0);in_lead=false;continue;}
            if(b==0xFD){assert(!in_lead);fd_t[fd_n++]=t+v;continue;}
            d=v;
        }else{assert(0);d=0;}
        assert(d>=1);t+=d;
        if(in_lead)lead_t[lead_n++]=t;else{rev_t[rev_n]=t;rev_of[rev_n++]=fd_n;}
    }
    assert(!in_lead && fd_n==3);
}
static void check(uint32_t window,const char* what){
    /* the lead-in: every transition strictly inside (-window, 0), in order */
    uint32_t n=0,k=0;while((int32_t)(ts[n]-I0)<=-(int32_t)window)n++;
    assert(lead_n==0 || base_pos==n);
    for(;(int32_t)(ts[n]-I0)<0;n++,k++)assert(k<lead_n && lead_t[k]==(int32_t)(ts[n]-I0));
    assert(k==lead_n && header==(window?(int64_t)window:-1) && (lead_n==0 || lead_t[0]>-(int64_t)window));
    /* then every transition from index 0 up to index 3, none twice, none missing */
    for(k=0;(int32_t)(ts[n]-idx_t[3])<0;n++,k++){
        assert(k<rev_n && rev_t[k]==(int64_t)(int32_t)(ts[n]-I0));
        unsigned r=0;while(r<3 && (int32_t)(ts[n]-idx_t[r+1])>=0)r++;
        assert(rev_of[k]==r);                   /* a transition on an index belongs to the next revolution */
    }
    assert(k==rev_n);
    for(unsigned r=0;r<3;r++)assert(fd_t[r]==(int64_t)(uint32_t)(idx_t[r+1]-I0));
    printf("lead-in %s: %u lead-in + %u transitions, header %lld ticks\n",what,lead_n,rev_n,(long long)header);
}
/* stream_service() order: trim while waiting, open at index 0, encode, flush; first/arm = first stored sample */
static uint32_t run_capture(uint32_t with_lead,uint32_t first,uint32_t isr_delay,bool late_service){
    memset(&buf,0,sizeof buf);cur=0;fill=0;done=false;rd=0;out_n=0;n_final=0;next_idx=1;
    lead=with_lead;lead_floored=lead_skip=false;base_pos=UINT32_MAX;produced=consumed=0;
    g_capture.state=CAPTURE_WAITING_INDEX;idx_n=0;
    now_ticks=with_lead?ts[first]-1u:I0;
    bool started=false,halted=false;unsigned guard=0;uint32_t stored=0;
    while(!done && guard++<200000u){
        now_ticks+=150000u;
        if(g_capture.state==CAPTURE_WAITING_INDEX && (int32_t)(now_ticks-I0-isr_delay)>=0)g_capture.state=CAPTURE_RUNNING;
        if(g_capture.state!=CAPTURE_WAITING_INDEX){idx_n=0;while(idx_n<4u && (int32_t)(now_ticks-idx_t[idx_n])>=0)idx_n++;}
        if(idx_n==4u){halted=true;g_capture.state=CAPTURE_COMPLETE;}
        while(first+stored<N_SAMPLES && (int32_t)(now_ticks-ts[first+stored])>=0 &&
              (!halted || (int32_t)(ts[first+stored]-idx_t[3])<20000)){
            assert(stored-consumed<WORDS);store[stored%WORDS]=ts[first+stored];stored++;   /* ring never overwritten */
        }
        produced=stored;
        if(g_capture.state==CAPTURE_WAITING_INDEX){
            if(lead && !late_service)lead_trim();
            continue;
        }
        if(late_service && (int32_t)(now_ticks-I0-1000000u)<0)continue;   /* streamer 3.6 ms late */
        if(!started){t_last=idx_t[0];if(lead)lead_open();started=true;}
        if(halted && !n_final){n_final=3;if(next_idx>n_final)done=true;}
        if(!done)encode(halted);
        flush();
    }
    assert(done);
    uint32_t h=2166136261u;for(uint32_t i=0;i<out_n;i++)h=(h^out[i])*16777619u;
    return h;
}
int main(void){
    make_capture();
    uint32_t first=0;while((int32_t)(ts[first]-I0)<0)first++;
    /* without the flag: byte-identical to the stream before the lead-in existed (digest taken from
     * the unchanged ufi_stream.c at 349de23 with this capture) */
    uint32_t h=run_capture(0,first,0,false);
    assert(out_n==8790u && h==0x21dcb95fu);
    decode(false);assert(!lead_n);check(0,"off");
    /* seam edges: one transition exactly on index 0 (revolution 0), one exactly on the window start (outside) */
    ts[first]=I0;
    uint32_t edge=0;while((int32_t)(ts[edge]-(I0-LEAD_TICKS))<0)edge++;
    ts[edge]=I0-LEAD_TICKS;
    /* lead-in: trimmed while waiting, full window */
    run_capture(LEAD_TICKS,0,0,false);decode(true);check(LEAD_TICKS,"trimmed");
    /* the streamer only runs once the capture ended: every older sample is skipped in encode() */
    run_capture(LEAD_TICKS,0,0,true);decode(true);check(LEAD_TICKS,"late streamer");
    assert(!lead_floored);
    /* index ISR 3 guards late: trimming reached into the window, the header reports the shorter one */
    run_capture(LEAD_TICKS,0,3u*LEAD_GUARD,false);decode(true);
    assert(lead_floored && header<LEAD_TICKS && (uint32_t)(I0-(uint32_t)header)==lead_floor);
    check((uint32_t)header,"late index ISR");
    puts("stream lead-in: byte-identical without the flag, no lost or doubled transition at the seam, ring trim passed");
    return 0;
}
'''


def lead_in():
    import re
    from test_regressions import FW
    source = (FW / 'src' / 'ufi_stream.c').read_text(encoding='utf-8')
    guard = re.search(r'#define LEAD_GUARD (\([^)]*\))', source).group(1)
    header = (FW / 'include' / 'ufi_firmware.h').read_text(encoding='utf-8')
    ticks = re.search(r'#define UFI_READ_LEAD_IN_TICKS (\([^)]*\))', header).group(1)
    code = BASE + LEAD_HARNESS % (guard, ticks)
    for name in ('put_u32', 'put_n28', 'put_delta_gw', 'put_delta', 'put_index', 'put_lead',
                 'put_indexes_before', 'encode', 'lead_trim', 'lead_open'):
        code += function('ufi_stream.c', name)
    run('regression_stream_lead_in', code + LEAD_MAIN)


if __name__=='__main__':main()
