/* Independent old algorithms verify the optimized production translation unit. */
#include "check.h"
#include <limits.h>
#include <time.h>
#include "../src/ufi_mfm.c"

static uint16_t reference_crc(uint16_t crc,const uint8_t* data,uint32_t count){
    while(count--){
        crc^=(uint16_t)(*data++<<8);
        for(unsigned bit=0;bit<8;bit++)
            crc=(crc&0x8000u)?(uint16_t)((crc<<1)^0x1021u):(uint16_t)(crc<<1);
    }
    return crc;
}
static uint8_t reference_unpack(uint32_t raw){
    uint8_t value=0;
    for(int bit=7;bit>=0;bit--)value=(uint8_t)((value<<1)|((raw>>(2*bit))&1u));
    return value;
}
/* Same cell consumer, unchanged original arithmetic: compare every decoder byte. */
static void reference_interval(mfm_decoder_t* d,uint32_t ticks){
    const int64_t t=(int64_t)ticks*256+d->phase;
    int64_t n=(t+d->period/2)/d->period;if(n<1)n=1;
    if(d->margin_permille && n>=1 && n<=8 && d->state!=S_SEARCH){
        int64_t residual=t-n*d->period;if(residual<0)residual=-residual;
        if((uint64_t)residual*1000u>(uint64_t)d->period*d->margin_permille)d->margin_errors++;
    }
    if(n>8){
        mfm_dec_diagnostic_tail(d,2);
        if(d->state!=S_SEARCH && d->check_encoding)d->code_errors++;
        d->state=S_SEARCH;if(d->encoding>=3)d->id_ok=false;
        d->phase=0;d->period=d->nominal;
        for(unsigned i=0;i<8;i++)cell(d,0);
        return;
    }
    const int64_t error=t-n*(int64_t)d->period;
    int64_t period=(int64_t)d->period+error/(n*d->period_divisor);
    if(period<d->pmin)period=d->pmin;
    if(period>d->pmax)period=d->pmax;
    d->period=(uint32_t)period;d->phase=(int32_t)(error/d->phase_divisor);
    for(int64_t i=1;i<n;i++)cell(d,0);
    cell(d,1);
}
static uint32_t random_state=0x723;
static uint32_t next_random(void){
    random_state^=random_state<<13;random_state^=random_state>>17;
    random_state^=random_state<<5;return random_state;
}
static uint8_t payload[4096];
static void validate(void){
    CHECK(mfm_crc16(0xffff,(const uint8_t*)"123456789",9)==0x29b1,"CRC16 known vector");
    for(unsigned i=0;i<sizeof payload;i++)payload[i]=(uint8_t)next_random();
    for(unsigned seed=0;seed<=UINT16_MAX;seed++){
        uint8_t data[3]={(uint8_t)seed,(uint8_t)(seed>>8),(uint8_t)(seed*73u)};
        CHECK(mfm_crc16((uint16_t)seed,data,3)==reference_crc((uint16_t)seed,data,3),"all CRC seed states");
        CHECK(raw_to_byte(seed)==reference_unpack(seed),"all 16-bit MFM words");
        uint32_t upper=seed|(next_random()&0xffff0000u);
        CHECK(raw_to_byte(upper)==reference_unpack(upper),"upper/clock bits discarded");
    }
    for(unsigned count=0;count<=sizeof payload;count++){
        uint16_t crc=mfm_crc16(0xa55a,payload,count);unsigned split=count/2;
        CHECK(crc==reference_crc(0xa55a,payload,count),"CRC block lengths and tails");
        CHECK(mfm_crc16(mfm_crc16(0xa55a,payload,split),payload+split,count-split)==crc,"CRC incremental chaining");
    }
    const int64_t numerators[]={INT64_MIN,INT64_MAX,(int64_t)INT32_MIN-1,INT32_MIN,
        (int64_t)INT32_MIN+1,-1001,-1,0,1,1001,INT32_MAX,(int64_t)INT32_MAX+1,UINT32_MAX};
    const uint32_t denominators[]={1,2,4,8,16,31,32,127,255,2040,INT32_MAX,(uint32_t)INT32_MAX+1,UINT32_MAX};
    for(unsigned i=0;i<sizeof numerators/sizeof *numerators;i++)
        for(unsigned k=0;k<sizeof denominators/sizeof *denominators;k++)
            CHECK(pll_divide(numerators[i],denominators[k])==numerators[i]/denominators[k],"signed division boundaries");
    for(unsigned i=0;i<200000;i++){
        uint64_t magnitude=((uint64_t)next_random()<<32)|next_random();
        int64_t numerator=(i&1)?(int32_t)next_random():(int64_t)(magnitude>>1);
        if(i&2)numerator=-numerator;
        uint32_t denominator=next_random()|1u;if(i&4)denominator=(denominator&255u)+1u;
        CHECK(pll_divide(numerator,denominator)==numerator/denominator,"random signed quotient equivalence");
    }
    mfm_decoder_t expected,actual;
    const uint32_t rates[]={125000,150000,250000,300000,500000,1000000,266667,380228};
    for(unsigned i=0;i<100000;i++){
        if(!(i%997)){
            mfm_dec_init_format(&expected,(uint8_t)(1+i%7),rates[i%8],NULL,NULL);
            mfm_dec_profile(&expected,(uint8_t)(i%3));expected.margin_permille=110;
        }
        uint32_t ticks=(expected.period>>8)*(1u+next_random()%8u)+(next_random()%31u);
        if(!(i%17))ticks=next_random();
        if(!(i%29))ticks=0;
        if(!(i%47))expected.phase=(int32_t)next_random();
        actual=expected;reference_interval(&expected,ticks);mfm_dec_interval(&actual,ticks);
        CHECK(!memcmp(&expected,&actual,sizeof expected),"full decoder state matches old PLL arithmetic");
    }
}
#ifdef UFI_BENCHMARK
static volatile uint32_t sink;
static uint16_t (*volatile crc_function)(uint16_t,const uint8_t*,uint32_t);
static uint8_t (*volatile unpack_function)(uint32_t);
static double crc_time(uint16_t (*function)(uint16_t,const uint8_t*,uint32_t)){
    crc_function=function;clock_t begin=clock();uint16_t crc=0xffff;
    for(unsigned i=0;i<2048;i++)crc=crc_function((uint16_t)(crc^i),payload,sizeof payload);
    sink=crc;return (double)(clock()-begin)/CLOCKS_PER_SEC;
}
static double unpack_time(uint8_t (*function)(uint32_t)){
    unpack_function=function;clock_t begin=clock();uint32_t result=0;
    for(uint32_t i=0;i<4194304u;i++)result+=unpack_function(i);
    sink=result;return (double)(clock()-begin)/CLOCKS_PER_SEC;
}
static int compare_time(const void* a,const void* b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(void){
    validate();if(checks_failed)return 1;
    double crc_old[5],crc_new[5],raw_old[5],raw_new[5];
    for(unsigned i=0;i<5;i++){
        /* Alternate order to reduce a systematic warm-up advantage. */
        if(i&1){crc_new[i]=crc_time(mfm_crc16);crc_old[i]=crc_time(reference_crc);
            raw_new[i]=unpack_time(raw_to_byte);raw_old[i]=unpack_time(reference_unpack);}
        else{crc_old[i]=crc_time(reference_crc);crc_new[i]=crc_time(mfm_crc16);
            raw_old[i]=unpack_time(reference_unpack);raw_new[i]=unpack_time(raw_to_byte);}
    }
    qsort(crc_old,5,sizeof(double),compare_time);qsort(crc_new,5,sizeof(double),compare_time);
    qsort(raw_old,5,sizeof(double),compare_time);qsort(raw_new,5,sizeof(double),compare_time);
    printf("{\"scope\":\"host_only_not_mcu_throughput\",\"checks\":%d,\"repetitions\":5,"
        "\"crc_bytes\":8388608,\"unpack_words\":4194304,"
        "\"crc_reference_seconds\":%.6f,\"crc_optimized_seconds\":%.6f,"
        "\"unpack_reference_seconds\":%.6f,\"unpack_optimized_seconds\":%.6f}\n",
        checks_run,crc_old[2],crc_new[2],raw_old[2],raw_new[2]);
    return 0;
}
#else
int main(void){validate();CHECK_SUMMARY("test_mfm_fastpaths");}
#endif
