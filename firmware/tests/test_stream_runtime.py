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


if __name__=='__main__':main()
