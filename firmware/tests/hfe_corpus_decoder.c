/* Independent HxC image cells through the production sector decoder. */
#include "ufi_quality.h"
#include <stdio.h>
#include <stdlib.h>
static quality_t q;
static mfm_decoder_t dec;
static uint8_t bits[32768];
static uint32_t word(FILE* f){uint8_t p[4];if(fread(p,1,4,f)!=4)exit(2);return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(int argc,char** argv){
    if(argc!=2)return 2;FILE* f=fopen(argv[1],"rb");if(!f)return 2;
    unsigned tracks=word(f),sectors=0,empty=0;
    for(unsigned t=0;t<tracks;t++){
        unsigned c=word(f),h=word(f),format=word(f),rate=word(f),bytes=word(f);
        if(bytes>sizeof(bits) || fread(bits,1,bytes,f)!=bytes)return 2;
        quality_init(&q,(uint8_t)format,rate,2);quality_begin(&q,&dec,0,NULL);
        uint64_t cells=0,prev=0;
        for(unsigned rev=0;rev<2;rev++){
            quality_revolution(&q,(uint8_t)rev);
            for(unsigned b=0;b<bytes*8;b++){
                cells++;
                if(bits[b/8]&(1u<<(b%8))){
                    uint64_t now=(cells*MFM_TICK_HZ+rate)/(2u*rate);
                    quality_interval(&q,(uint32_t)(now-prev));prev=now;
                }
            }
            quality_end_revolution(&q);
        }
        if(c>=80){if(q.n)return 1;empty++;continue;}
        if(q.n!=(format==2?11:9)){
            fprintf(stderr,"HFE %u/%u: expected %u sectors, got %u\n",c,h,format==2?11:9,q.n);return 1;
        }
        for(unsigned s=0;s<q.n;s++){
            quality_sector_t* v=&q.sectors[s];
            if(v->seen!=3 || v->bad || (v->flags&1) || v->id.c!=c || v->id.h!=h || v->id.n!=2 ||
               (format==2?(v->id.r<1 || v->id.r>11):!((v->id.r>=1 && v->id.r<=9) || (v->id.r>=0xc1 && v->id.r<=0xc9)))){
                fprintf(stderr,"HFE track %u/%u fmt%u sector%u seen%u bad%u flags%u CH=%u/%u\n",c,h,format,v->id.r,v->seen,v->bad,v->flags,v->id.c,v->id.h);return 1;
            }
        }
        if(q.attempts[1][0].header_errors || q.attempts[1][0].data_errors)return 1;
        sectors+=q.n;
    }
    if(fgetc(f)!=EOF)return 2;fclose(f);
    printf("HFE corpus: %u track sides, %u valid sectors, %u undecoded/empty sides; checksums and two-revolution consistency passed\n",tracks,sectors,empty);
    return sectors==5000 && empty==16?0:1;
}
