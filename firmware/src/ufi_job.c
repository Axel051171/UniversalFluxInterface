#include "ufi_job.h"
#include <string.h>
ufi_job_t ufi_job;
static uint32_t get32(const uint8_t *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
uint32_t ufi_job_crc(uint32_t crc,const uint8_t *p,uint32_t n)
{
    crc=~crc;
    while(n--) {crc^=*p++; for(unsigned i=0;i<8;i++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return ~crc;
}
bool ufi_job_live(void) {return ufi_job.state>=JOB_OPEN && ufi_job.state<=JOB_READY;}
bool ufi_job_open(uint64_t id,uint8_t kind,uint32_t now)
{
    if(!id || (kind!=1 && kind!=2) || ufi_job_live())return false;
    ufi_job=(ufi_job_t){.id=id,.kind=kind,.state=JOB_OPEN,.last_ms=now};
    return true;
}
static bool advance(const uint8_t *p,uint32_t n,uint32_t now)
{
    if(!ufi_job_live() || ufi_job.state==JOB_READY || n>UINT32_MAX-ufi_job.bytes || ufi_job.packets==UINT32_MAX)return false;
    ufi_job.crc=ufi_job_crc(ufi_job.crc,p,n); ufi_job.bytes+=n; ufi_job.packets++;
    ufi_job.state=JOB_ACTIVE;ufi_job.last_ms=now;return true;
}
bool ufi_job_accept(const uint8_t *h,const uint8_t *p,uint32_t n,uint32_t now)
{
    uint64_t id=get32(h)|(uint64_t)get32(h+4)<<32;
    if(id!=ufi_job.id || get32(h+8)!=ufi_job.packets || get32(h+12)!=ufi_job.bytes)return false;
    return advance(p,n,now);
}
bool ufi_job_emit(uint8_t *h,const uint8_t *p,uint32_t n,uint32_t now)
{
    put32(h,(uint32_t)ufi_job.id);put32(h+4,(uint32_t)(ufi_job.id>>32));
    put32(h+8,ufi_job.packets);put32(h+12,ufi_job.bytes);
    return advance(p,n,now);
}
void ufi_job_finish(uint8_t status) {ufi_job.status=status;ufi_job.state=status?JOB_FAILED:JOB_DONE;}
void ufi_job_reset(void) {memset(&ufi_job,0,sizeof(ufi_job));}
