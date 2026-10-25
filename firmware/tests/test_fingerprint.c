#include "ufi_verify.h"
#include "check.h"
#include <string.h>
static uint8_t data[1024];
static uint32_t reference(uint32_t crc,const uint8_t* p,unsigned n){
    crc=~crc;
    while(n--){crc^=*p++;for(unsigned k=0;k<8;k++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return ~crc;
}
static uint32_t hash(unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)h=(h^data[i])*16777619u;return h;}
int main(void){
    CHECK(ufi_crc32(0,(const uint8_t*)"123456789",9)==0xcbf43926u,"IEEE check vector");
    uint32_t random=0x12345678;
    for(unsigned i=0;i<sizeof data;i++){random=random*1664525u+1013904223u;data[i]=(uint8_t)(random>>24);}
    for(unsigned n=0;n<=sizeof data;n++){
        uint32_t seed=n*0x1020304u;
        CHECK(ufi_crc32(seed,data,n)==reference(seed,data,n),"CRC against bitwise reference length %u",n);
        unsigned split=n/2;
        CHECK(ufi_crc32(ufi_crc32(seed,data,split),data+split,n-split)==reference(seed,data,n),"incremental CRC %u",n);
        verify_fingerprint_t f;
        CHECK(ufi_fingerprint(data,n,split,&f) && f.crc==reference(0,data,n) && f.hash==hash(n) &&
              f.prefix_crc==reference(0,data,split) && f.prefix_hash==hash(split),"full and prefix fingerprints %u",n);
    }
    verify_fingerprint_t f;
    CHECK(!ufi_fingerprint(data,1,2,&f) && !ufi_fingerprint(NULL,1,0,&f) && !ufi_fingerprint(data,1,1,NULL),"invalid fingerprint arguments");
    CHECK(ufi_fingerprint(NULL,0,0,&f) && !f.crc && f.hash==2166136261u,"empty fingerprint");
    CHECK_SUMMARY("test_fingerprint");
}
