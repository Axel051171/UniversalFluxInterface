#include "ufi_hfe_v3.h"
#include "ufi_image.h"
#include <stdio.h>
#include <stdlib.h>
static uint8_t original[32768],encoded[32767],header[512];
static uint32_t flux[262144];
static uint16_t word(const uint8_t* p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static bool read_byte(void* p,uint32_t off,uint8_t* byte){*byte=((uint8_t*)p)[off];return true;}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    FILE* f=fopen(argv[1],"rb");if(!f)return 2;
    if(fread(header,1,512,f)!=512)return 2;
    hfe_header_t h;if(!hfe_parse_header(header,&h) || h.v3)return 2;
    unsigned sides=0;unsigned long cells=0;
    for(unsigned c=0;c<h.tracks;c++){
        uint8_t lut[4];if(fseek(f,h.lut_block*512+c*4,SEEK_SET) || fread(lut,1,4,f)!=4)return 2;
        unsigned start=word(lut),length=word(lut+2)/2;
        for(unsigned side=0;side<h.sides;side++){
            for(unsigned offset=0;offset<length;offset+=256){
                unsigned chunk=length-offset;if(chunk>256)chunk=256;
                if(fseek(f,(start+offset/256)*512+side*256,SEEK_SET) || fread(original+offset,1,chunk,f)!=chunk)return 2;
            }
            hfe_v3_encoder_t e;unsigned rate=(18000000+h.rate_bps/2)/h.rate_bps;
            if(!hfe_v3_encode_begin(&e,encoded,sizeof(encoded),(uint8_t)rate))return 1;
            unsigned edges=0;
            for(unsigned i=0;i<length*8;i++){
                bool edge=(original[i/8]>>(i%8))&1;edges+=edge;
                if(!hfe_v3_encode_cells(&e,1,edge))return 1;
            }
            if(!hfe_v3_encode_end(&e))return 1;
            hfe_v3_report_t r;int ret=hfe_v3_decode(read_byte,encoded,e.length,flux,262144,&r);
            if((edges>=3 && ret) || (edges<3 && ret!=-16) || r.cells!=length*8 || r.flux_count!=edges || r.weak_cells)return 1;
            unsigned at=0;uint32_t previous=0;
            for(unsigned i=0;i<length*8;i++)if((original[i/8]>>(i%8))&1){
                uint32_t now=(uint32_t)(((uint64_t)(i+1)*rate*275+18)/36);
                if(flux[at++]!=now-previous)return 1;
                previous=now;
            }
            sides++;cells+=length*8;
        }
    }
    fclose(f);
    printf("External Transylvania HFE: %u sides, %lu cells, every transition preserved by firmware v3 codec\n",sides,cells);
    return sides==84?0:1;
}
