"""Production precision verifier with independent capture and U11 storage doubles."""
from test_regressions import ERRORS, FW, run


def main():
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\n' + ERRORS + r'''
typedef struct {uint32_t timestamp;} flux_sample_t;
typedef struct {flux_sample_t* samples;uint32_t count,index_time;} flux_revolution_t;
static uint32_t reference[8192],now;
static bool reserved,cancelled,psram=true;
static flux_sample_t samples[2][4];
static flux_revolution_t revolutions[2]={{samples[0],4,1000},{samples[1],4,2000}};
static uint32_t HAL_GetTick(void){return now;}
#define UFI_WATCHDOG_FEED() ((void)0)
static bool ufi_cancel_requested(void){return cancelled;}
int ufi_flux_reference_reserve(bool enable){if(enable && !psram)return UFI_ERR_NOT_IMPL;reserved=enable;return 0;}
uint32_t* ufi_flux_reference(uint32_t* words){*words=reserved?8192:0;return reserved?reference:NULL;}
uint8_t ufi_flux_get_revolution_count(void){return 2;}
const flux_revolution_t* ufi_flux_get_revolution(uint8_t n){return n<2?&revolutions[n]:NULL;}
'''
    source = (FW/'src/ufi_precision.c').read_text()
    for header in ('ufi_firmware.h', 'ufi_cancel.h'):
        source = source.replace(f'#include "{header}"', '')
    code += source + r'''
int main(void){
    uint8_t request[12]={50,1,0,0,3},out[288];
    for(unsigned f=0;f<256;f++){
        request[2]=(uint8_t)f;
        int ret=ufi_precision_options(request,12,out);
        assert((ret==0)==(f==0 || f==1 || f==3));
    }
    request[2]=3;assert(!ufi_precision_options(request,12,out) && reserved && out[1]==3);
    assert(ufi_precision_enabled());
    const uint32_t deltas[]={100,100,200,400};
    for(unsigned i=0;i<4;i++){samples[0][i].timestamp=(uint32_t[]){100,200,400,800}[i];samples[1][i].timestamp=2*samples[0][i].timestamp;}
    assert(!ufi_precision_source(deltas,4,1000,0) && reference[3]==800);
    assert(!ufi_precision_verify());
    assert(ufi_precision_report(0,8,out)==128 && out[3]==6 && out[24]==1);
    assert(!ufi_precision_options(request,1,out) && out[17] && rd(out+20));
    assert(!ufi_precision_verify_begin() && ufi_precision_verify_begin()==UFI_ERR_BUSY);
    assert(ufi_precision_verify_step()==UFI_ERR_BUSY);ufi_precision_verify_cancel();assert(!available && !verify.active);
    assert(!ufi_precision_verify_begin());unsigned steps=0;int step;
    do{step=ufi_precision_verify_step();assert(++steps<32);}while(step==UFI_ERR_BUSY);
    assert(!step && steps>=4 && available);
    /* Same count and duration, different angular position: precise strict failure. */
    samples[1][1].timestamp=600;
    assert(ufi_precision_verify()==UFI_ERR_DMA);
    assert(ufi_precision_report(24,3,out)==68 && rd(out+32+12+0)==1); /* missing at 200/1000 */
    assert(ufi_precision_report(38,1,out)==44 && rd(out+36)==1); /* extra at 300/1000 */
    request[2]=1;assert(!ufi_precision_options(request,12,out));
    assert(!ufi_precision_source(deltas,4,1000,0) && !ufi_precision_verify()); /* advisory */
    assert(ufi_precision_report(128,8,out)==32 && !out[2]);
    assert(!ufi_precision_report(129,1,out) && !ufi_precision_report(0,9,out));
    ufi_precision_reset();assert(!ufi_precision_report(0,8,out));
    assert(ufi_precision_verify()==UFI_ERR_BAD_ARGS);
    assert(ufi_precision_source(deltas,4,1000,1)==UFI_ERR_BAD_ARGS);
    assert(ufi_precision_source(deltas,8193,1000,0)==UFI_ERR_NOT_IMPL);
    const uint32_t bad[]={100,0,400};assert(ufi_precision_source(bad,3,1000,0)==UFI_ERR_RANGE);
    assert(ufi_precision_source(deltas,4,799,0)==UFI_ERR_RANGE);
    cancelled=true;assert(ufi_precision_source(deltas,4,1000,0)==UFI_ERR_CANCELLED);cancelled=false;
    assert(!ufi_precision_source(deltas,4,1000,0));samples[1][1].timestamp=100;
    assert(ufi_precision_verify()==UFI_ERR_DMA && !available); /* nonmonotonic capture rejected */
    request[2]=0;assert(!ufi_precision_options(request,12,out) && !reserved && !ufi_precision_verify());
    psram=false;request[2]=1;assert(ufi_precision_options(request,12,out)==UFI_ERR_NOT_IMPL);
    puts("precision verify: retained source, two revolutions, speed, angular faults, strict/advisory, malformed capture and cancellation passed");
}
'''
    run('regression_precision', code, ('src/ufi_compare.c',))


if __name__ == '__main__':
    main()
