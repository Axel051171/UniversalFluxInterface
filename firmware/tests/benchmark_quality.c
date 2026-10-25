/* Deterministic fault fixtures exercise production algorithms. No drive model. */
#include "ufi_compare.h"
#include "ufi_raw_verify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N 4096u
static uint32_t source[N],observed[N+1],deltas[N];
static track_compare_t comparison;
static compare_stats_t statistics[COMPARE_WINDOWS];
static raw_profile_t reference,profile;
static uint8_t wire[RAW_REPORT_SIZE];
static unsigned ordinal;
static unsigned faults,controls,precision_detected,histogram_detected;
static void fixture(const char* name,unsigned count,uint32_t period,unsigned missing,unsigned extra,bool histogram){
    track_compare_init(&comparison,1,3,N*1000);track_compare_stats_attach(&comparison,statistics);
    unsigned slices=1;
    while(!track_compare_step(&comparison,source,N,observed,count,period,1,256))assert(++slices<=34);
    unsigned m=0,e=0,matched=0,max=0;
    for(unsigned k=0;k<COMPARE_WINDOWS;k++){
        m+=comparison.windows[k].missing;e+=comparison.windows[k].extra;matched+=statistics[k].matched;
        if(statistics[k].maximum>max)max=statistics[k].maximum;
    }
    assert(m==missing && e==extra && matched==N-m);
    assert(raw_profile_build(&profile,observed,count,period,true));
    bool rough=raw_profile_compare(&reference,&profile,30,wire);assert(rough==histogram);
    if(missing || extra){faults++;precision_detected+=(m || e);histogram_detected+=!rough;}else controls++;
    printf("%s{\"name\":\"%s\",\"missing\":%u,\"extra\":%u,\"matched\":%u,\"maximum_ticks\":%u,\"slices_256\":%u,\"histogram_match\":%s}",
           ordinal++?",\n":"",name,m,e,matched,max,slices,rough?"true":"false");
}
int main(void){
    for(unsigned i=0;i<N;i++){source[i]=(i+1)*1000;deltas[i]=1000;}
    assert(raw_profile_build(&reference,deltas,N,N*1000,false));
    puts("{\"schema\":1,\"kind\":\"synthetic_algorithm_benchmark\",\"timer_hz\":275000000,\"tolerance_ticks\":3,\"cases\":[");
    memcpy(observed,source,sizeof source);fixture("clean",N,N*1000,0,0,true);
    for(unsigned i=0;i<N;i++){observed[i]=source[i]*2;}
    fixture("uniform_speed_x2",N,N*2000,0,0,true);
    for(unsigned i=0;i<N;i++){observed[i]=source[i]+(i==N-1?0:(i&1?2:-2));}
    fixture("jitter_within_tolerance",N,N*1000,0,0,true);
    memcpy(observed,source,sizeof source);observed[10]-=100;fixture("shift_hidden_by_histogram",N,N*1000,1,1,true);
    for(unsigned i=0,j=0;i<N;i++){if(i!=100)observed[j++]=source[i];}
    fixture("single_missing",N-1,N*1000,1,0,true);
    memcpy(observed,source,100*sizeof(uint32_t));observed[100]=100500;
    memcpy(observed+101,source+100,(N-100)*sizeof(uint32_t));fixture("single_extra",N+1,N*1000,0,1,true);
    for(unsigned i=0,j=0;i<N;i++){if(i<128 || i>=144)observed[j++]=source[i];}
    fixture("missing_burst_16",N-16,N*1000,16,0,true);
    printf("\n],\"metrics\":{\"fault_cases\":%u,\"control_cases\":%u,\"precise_detected\":%u,\"histogram_detected\":%u,\"precise_false_positives\":0,\"precise_false_negatives\":%u}}\n",
           faults,controls,precision_detected,histogram_detected,faults-precision_detected);return 0;
}
