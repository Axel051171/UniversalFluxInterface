"""Production frame parser fuzzing and generated composite descriptor checks."""
from test_regressions import BASE, ERRORS, function, run


def diagnostics_tests():
    code=BASE+ERRORS+r'''
#define FLUX_TIMER_FREQ 275000000u
char __heap_start[4],__heap_end[4],_edtcm[4],_estack[4];
static uint32_t irq_mask;
static uint32_t __get_PRIMASK(void){return irq_mask;}
static void __disable_irq(void){irq_mask=1;}
static void __set_PRIMASK(uint32_t value){irq_mask=value;}
static uint32_t* ufi_flux_store(uint32_t* words){*words=12345;return NULL;}
static uint32_t ufi_flux_written(void){return 0;}
static bool ufi_flux_store_is_psram(void){return true;}
static uint8_t ufi_resources_owner(void){return 2;}
static uint32_t ufi_native_tx_timeouts(void){return 17;}
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t* p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
'''+function('ufi_v2.c','cmd_diagnostics')+r'''
int main(void){
    uint8_t out[65],expected[64],request[2]={1,0};uint16_t size;
    memset(out,0xa5,sizeof out);ufi_metrics.capture_overruns=3;
    assert(!cmd_diagnostics(NULL,0,out,&size) && size==64 && out[64]==0xa5);
    assert(out[0]==1 && out[1]==1 && out[2]==2 && !out[3] && rd(out+4)==275000000);
    assert(rd(out+8)==12345 && rd(out+20)==3 && rd(out+40)==17 && rd(out+60)==30000);
    ufi_metrics_pack(expected,64);
    assert(!cmd_diagnostics(request,1,out,&size) && !memcmp(out,expected,64) && out[64]==0xa5);
    ufi_cycles_init(550000000,true);ufi_cycles_record(UFI_CYCLE_STREAM,20,220,0,550000000,true);
    request[0]=2;irq_mask=7;ufi_metrics.capture_index_loss=2;
    assert(!cmd_diagnostics(request,1,out,&size) && size==64 && irq_mask==7 && out[64]==0xa5);
    assert(out[0]==1 && out[1]==2 && out[2]==15 && out[3]==1 && rd(out+4)==550000000);
    assert(rd(out+32)==1 && rd(out+36)==200 && rd(out+60)==2);
    for(unsigned i=3;i<256;i++){request[0]=(uint8_t)i;memset(out,0xa5,sizeof out);
        assert(cmd_diagnostics(request,1,out,&size)==UFI_ERR_BAD_ARGS && !size && out[0]==0xa5);}
    request[0]=2;assert(cmd_diagnostics(request,2,out,&size)==UFI_ERR_BAD_ARGS && !size);
    puts("diagnostics: legacy/millisecond layouts, cycles, snapshot mask, bounds and unknown selectors passed");
    return 0;
}
'''
    run('regression_diagnostics',code)


def reply_bounds_tests():
    code=BASE+ERRORS+r'''
#define UFI_V2_HDR 8u
#define UFI_V2_MAX_PAYLOAD 4096u
#define UFI_V2_FRAME_MAX 4106u
enum {UFI_OWNER_GW=2};
static uint8_t tx[UFI_V2_FRAME_MAX],last[UFI_V2_FRAME_MAX];
static uint32_t sent,cancels;
static unsigned ufi_resources_owner(void){return 0;}
static void ufi_cancel_request(void){cancels++;}
static int ufi_native_tx_blocking(const uint8_t* p,uint32_t n){
    assert(n<=sizeof last);memcpy(last,p,n);sent=n;return UFI_OK;
}
static void put16(uint8_t* out,uint32_t value){out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8);}
'''
    for name in ('st','crc16','ufi_v2_frame','send'):code+=function('ufi_v2.c',name)
    code+=r'''
int main(void){
    memset(tx,0xa5,sizeof tx);
    assert(!ufi_v2_frame(tx,2,1,1,0,4097) && tx[0]==0xa5);
    assert(!ufi_v2_frame(NULL,2,1,1,0,0));
    uint8_t tiny=0x55;
    send(2,0x0d,34,0,&tiny,UINT16_MAX);
    assert(sent==10 && last[3]==0x0d && last[4]==34 && last[5]==10 && !last[6] && !last[7]);
    send(2,0x0d,35,0,NULL,1);
    assert(sent==10 && last[4]==35 && last[5]==10);
    static uint8_t payload[4096];memset(payload,0x5a,sizeof payload);
    send(2,0,36,0,payload,sizeof payload);
    assert(sent==4106 && !last[5] && last[6]==0 && last[7]==16 && last[8]==0x5a && last[4103]==0x5a && !cancels);
    return 0;
}
'''
    run('regression_reply_bounds',code)


def parser_tests():
    code=BASE+r'''
#define UFI_V2_HDR 8u
#define UFI_V2_MAX_PAYLOAD 4096u
#define UFI_V2_DATA 4
#define UFI_V2_REQUEST 1
#define UFI_V2_WRITE 0x22
static uint8_t fb[4106];
static uint32_t have, requests, errors;
static bool op_write;
static uint8_t write_seq;
static uint32_t get16(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8;}
static void frame_error(void) {errors++;}
static void write_data(uint8_t*p,uint16_t n) {(void)p;(void)n;}
static void ufi_board_activity(void) {}
static void request(uint8_t cmd,uint8_t seq,uint8_t*p,uint16_t n) {
    (void)p;assert(n<=4096);if(cmd==0 && seq==123 && n==0)requests++;
}
'''
    for name in ('crc16','resync','check_frame','parse_byte'):
        code+=function('ufi_v2.c',name)
    code+=r'''
int main(void) {
uint32_t rng=0x1431541u;
for(unsigned k=0;k<100000;k++) {
    rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
    parse_byte((uint8_t)rng);assert(have<=sizeof(fb));
}
/* Drain an arbitrary partial payload, then recover a valid frame. */
for(unsigned k=0;k<sizeof(fb)*2;k++)parse_byte(0);
uint8_t valid[]={0x55,0xaa,1,0,123,0,0,0,0,0};
uint16_t crc=crc16(valid+2,6);valid[8]=(uint8_t)crc;valid[9]=(uint8_t)(crc>>8);
unsigned before=requests;
for(unsigned k=0;k<sizeof(valid);k++)parse_byte(valid[k]);
assert(requests==before+1);
valid[9]^=1;for(unsigned k=0;k<sizeof(valid);k++)parse_byte(valid[k]);
assert(requests==before+1 && errors);
return 0;
}
'''
    run('regression_parser_fuzz',code)


def descriptor_tests():
    code=BASE+r'''
#define BULK_IN 0x83u
#define BULK_OUT 3u
static uint8_t config[128],compat[40],properties[142];
static uint16_t config_len;
static void le16(uint8_t*p,uint16_t v) {p[0]=v;p[1]=v>>8;}
static void le32(uint8_t*p,uint32_t v) {le16(p,v);le16(p+2,v>>16);}
static uint8_t *cdc_config(uint16_t *n) {
    static uint8_t cdc[]={9,2,67,0,2,1,0,0xc0,50,
      9,4,0,0,1,2,2,1,0,5,0x24,0,0x10,1,5,0x24,1,0,1,4,0x24,2,2,5,0x24,6,0,1,
      7,5,0x82,3,8,0,16,9,4,1,0,2,0x0a,0,0,0,7,5,1,2,64,0,0,7,5,0x81,2,64,0,0};
    *n=sizeof(cdc);return cdc;
}
static struct {uint8_t *(*GetFSConfigDescriptor)(uint16_t*);} USBD_CDC={cdc_config};
'''
    code+=function('ufi_composite.c','descriptors')
    code+=r'''
int main(void) {
descriptors();assert(config_len==98 && config[2]==98 && config[4]==3);
unsigned ifaces=0,eps=0,iads=0;
for(unsigned i=0;i<config_len;i+=config[i]) {
    assert(config[i]>=2 && i+config[i]<=config_len);
    if(config[i+1]==4)ifaces++;
    if(config[i+1]==5)eps++;
    if(config[i+1]==11)iads++;
}
assert(ifaces==3 && eps==5 && iads==1);
assert(compat[16]==2 && memcmp(compat+18,"WINUSB",6)==0);
assert(properties[0]==142 && properties[10]==132 && properties[60]==78);
assert(properties[64]=='{' && properties[138]=='}' && properties[140]==0);
return 0;
}
'''
    run('regression_composite_descriptors',code)


def clock_tests():
    code=BASE+r'''
#define TIM_SR_UIF 1u
static struct {uint32_t CNT,SR;} timer;
#define TIM2 (&timer)
static uint32_t timer_high,mask;
static uint32_t __get_PRIMASK(void) {return mask;}
static void __disable_irq(void) {mask=1;}
static void __set_PRIMASK(uint32_t m) {mask=m;}
'''
    code+=function('ufi_flux.c','ufi_flux_now64')
    code+=r'''
int main(void) {
timer.CNT=UINT32_MAX-5;uint64_t before=ufi_flux_now64();assert(mask==0);
timer.CNT=3;timer.SR=TIM_SR_UIF;
assert(ufi_flux_now64()==(1ull<<32)+3 && timer_high==1 && mask==0);
assert(ufi_flux_now64()==before+9 && timer_high==1);
mask=1;timer.CNT=8;assert(ufi_flux_now64()==(1ull<<32)+8 && mask==1);
timer_high=1234;timer.CNT=5;timer.SR=TIM_SR_UIF;
assert(ufi_flux_now64()==(1235ull<<32)+5 && mask==1);
return 0;
}
'''
    run('regression_clock64',code)


if __name__=='__main__':
    diagnostics_tests()
    reply_bounds_tests()
    parser_tests()
    descriptor_tests()
    clock_tests()
