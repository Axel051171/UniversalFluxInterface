#include "ufi_metrics.h"
#include "check.h"
#include <string.h>
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
    uint8_t out[65];memset(out,0xa5,sizeof out);
    CHECK(!ufi_metrics_pack(out,63) && out[0]==0xa5,"short output untouched");
    CHECK(!ufi_metrics_pack(NULL,64),"null output refused");
    ufi_metric_add(&ufi_metrics.stream_samples,UINT32_MAX-1);
    ufi_metric_add(&ufi_metrics.stream_samples,10);
    ufi_metric_max(&ufi_metrics.quality_max_ms,5);ufi_metric_max(&ufi_metrics.quality_max_ms,2);
    CHECK(ufi_metrics.stream_samples==UINT32_MAX && ufi_metrics.quality_max_ms==5,"saturating totals and maximum");
    ufi_metrics.format_candidates=42;ufi_metrics.fingerprint_bytes=529;
    CHECK(ufi_metrics_pack(out,64)==64 && out[64]==0xa5,"bounded report");
    CHECK(out[0]==1 && out[1]==1 && out[2]==13 && rd(out+4)==1000,"schema, mode, count, clock units");
    CHECK(rd(out+20)==5 && rd(out+28)==UINT32_MAX && rd(out+36)==42 && rd(out+52)==529,"counter layout");
    for(unsigned i=56;i<64;i++)CHECK(!out[i],"reserved byte %u",i);
    ufi_cycle_snapshot_t snapshot;
    ufi_cycles_init(550000000,true);
    for(unsigned i=0;i<UFI_CYCLE_COUNT;i++)ufi_cycles_record(i,UINT32_MAX-9,10,0,550000000,true);
    ufi_cycles_record(UFI_CYCLE_MAIN,100,110,1,550000000,true);
    ufi_cycles_snapshot(&snapshot);
    CHECK(snapshot.maximum_ms==7809 && snapshot.flags==1,"DWT window and availability");
    CHECK(snapshot.channel[0].calls==2 && snapshot.channel[0].maximum==20,"wrap and maximum");
    for(unsigned i=0;i<UFI_CYCLE_COUNT;i++)CHECK(snapshot.channel[i].maximum==20,"channel %u",i);
    CHECK(!ufi_cycles_pack(NULL,out,64) && !ufi_cycles_pack(&snapshot,NULL,64),"null cycle arguments");
    memset(out,0xa5,sizeof out);CHECK(!ufi_cycles_pack(&snapshot,out,63) && out[0]==0xa5,"short cycles output untouched");
    ufi_metrics.capture_rdata_loss=7;ufi_metrics.capture_index_loss=9;ufi_cycles_snapshot(&snapshot);
    CHECK(ufi_cycles_pack(&snapshot,out,64)==64 && out[64]==0xa5,"cycle bounds");
    CHECK(out[0]==1 && out[1]==2 && out[2]==15 && out[3]==1 && rd(out+4)==550000000,"cycle schema");
    CHECK(rd(out+8)==2 && rd(out+12)==20 && rd(out+56)==7 && rd(out+60)==9,"cycle wire and loss layout");
    ufi_cycles_record(UFI_CYCLE_COUNT,0,1,0,550000000,true); /* invalid channel ignored */
    ufi_cycles_record(UFI_CYCLE_MAIN,0,1,7808,550000000,true);
    ufi_cycles_record(UFI_CYCLE_DECODER,0,99,1,480000000,true);
    ufi_cycles_record(UFI_CYCLE_WRITE_DMA,0,99,1,550000000,false);
    ufi_cycles_snapshot(&snapshot);
    CHECK((snapshot.flags&22)==22 && !(snapshot.flags&1),"invalid span, clock change, unavailable retained");
    CHECK(snapshot.channel[0].calls==2 && snapshot.channel[4].calls==1,"invalid samples excluded");
    ufi_cycles_init(0,false);ufi_cycles_record(0,0,999,0,0,true);ufi_cycles_snapshot(&snapshot);
    CHECK(!snapshot.channel[0].calls && !(snapshot.flags&1),"unavailable clock is not a cycle measurement");
    ufi_cycles_init(1,true);ufi_cycles_snapshot(&snapshot);
    CHECK(snapshot.maximum_ms==UINT32_MAX,"millisecond window saturates for low clocks");
    CHECK_SUMMARY("test_metrics");
}
