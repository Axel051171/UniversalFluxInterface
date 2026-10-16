#include "ufi_quality.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static mfm_decoder_t dec;
static quality_t q;
static uint8_t bits[20000];
static uint32_t read32(FILE* f){uint8_t p[4];if(fread(p,1,4,f)!=4)exit(2);return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(int argc,char** argv){
    if(argc!=2)return 2;FILE* f=fopen(argv[1],"rb");if(!f)return 2;
    unsigned tracks=read32(f),sectors=0;
    for(unsigned t=0;t<tracks;t++){
        unsigned c=read32(f),h=read32(f),count=read32(f),bytes=(count+7)/8;
        if(c>=80 || h>1 || bytes>sizeof(bits) || fread(bits,1,bytes,f)!=bytes)return 2;
        quality_init(&q,6,500000,2);q.track=(uint8_t)c;q.side=(uint8_t)h;quality_begin(&q,&dec,0,NULL);
        uint32_t gap=0;
        for(unsigned rev=0;rev<2;rev++){
            quality_revolution(&q,(uint8_t)rev);
            for(unsigned b=0;b<count;b++){
                gap+=550;
                if(bits[b/8]&(128u>>(b%8))){quality_interval(&q,gap);gap=0;}
            }
            quality_end_revolution(&q);
        }
        uint8_t report[48];quality_observation_pack(&q,report,sizeof(report));
        if(q.n!=12-c/16 || (report[4]&~4u)){
            fprintf(stderr,"track %u/%u: sectors=%u flags=%u header=%u data=%u code=%u\n",c,h,q.n,report[4],dec.header_errors,dec.data_errors,dec.code_errors);return 1;
        }
        sectors+=q.n;
    }
    if(fgetc(f)!=EOF)return 2;fclose(f);
    printf("MOOF corpus: %u tracks, %u sectors, header/data checksums and two-revolution consistency passed\n",tracks,sectors);
    return 0;
}
