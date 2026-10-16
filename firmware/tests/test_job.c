#include "ufi_job.h"
#include "ufi_resources.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    assert(ufi_job_crc(0,(const uint8_t*)"123456789",9)==0xcbf43926u);
    assert(ufi_resources_acquire(UFI_OWNER_GW));
    assert(!ufi_resources_acquire(UFI_OWNER_NATIVE));
    assert(!ufi_resources_release(UFI_OWNER_NATIVE));
    assert(ufi_resources_release(UFI_OWNER_GW));
    assert(ufi_resources_acquire(UFI_OWNER_NATIVE));
    ufi_resources_touch(UINT32_MAX-1000);
    assert(!ufi_resources_idle_expired(1000,false));
    assert(ufi_resources_idle_expired(30000,false));
    assert(!ufi_resources_idle_expired(40000,true));
    assert(!ufi_resources_idle_expired(69999,false));
    assert(ufi_resources_idle_expired(70000,false));
    ufi_resources_release(UFI_OWNER_NATIVE);ufi_resources_acquire(UFI_OWNER_GW);
    assert(!ufi_resources_idle_expired(90000,false));
    ufi_resources_release(UFI_OWNER_GW);
    uint8_t header[16], data[]={1,2,3,4};
    assert(ufi_job_open(0x123456789abcdef0ull,1,0));
    assert(!ufi_job_open(1,2,0));
    assert(ufi_job_emit(header,data,4,1));
    uint32_t crc=ufi_job.crc;
    ufi_job_reset();assert(ufi_job_open(0x123456789abcdef0ull,2,0));
    assert(ufi_job_accept(header,data,4,1));
    assert(ufi_job.crc==crc && ufi_job.bytes==4 && ufi_job.packets==1);
    assert(!ufi_job_accept(header,data,4,2)); /* duplicate */
    header[8]=2;assert(!ufi_job_accept(header,data,4,2)); /* gap */
    header[8]=1;header[12]=4;header[0]^=1;
    assert(!ufi_job_accept(header,data,4,2)); /* foreign job */
    header[0]^=1;assert(ufi_job_accept(header,data,4,2));
    ufi_job.state=JOB_READY;assert(!ufi_job_accept(header,data,4,3));
    ufi_job_finish(0);assert(!ufi_job_live() && ufi_job.state==JOB_DONE);
    puts("test_job: ownership, CRC, duplicate/gap/foreign-job rejection passed");
    return 0;
}
