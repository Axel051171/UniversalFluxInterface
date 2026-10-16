/* Run the production decoder against independent SCP captures converted to u32. */
#include "ufi_mfm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t image[368640];
static uint32_t seen,track,valid,invalid;
static void sector(void* ctx,const mfm_id_t* id,const uint8_t* p,bool ok) {
    (void)ctx;if(!ok){invalid++;return;}
    assert(id->c==track/2 && id->h==track%2 && id->n==2 && id->r>=1 && id->r<=9);
    assert(!memcmp(p,image+(track*9+id->r-1)*512,512));
    seen|=1u<<(id->r-1);valid++;
}
static uint32_t word(FILE* f){uint8_t b[4];assert(fread(b,1,4,f)==4);return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
int main(int argc,char** argv) {
    assert(argc==3);FILE* f=fopen(argv[1],"rb");assert(f);assert(fread(image,1,sizeof(image),f)==sizeof(image));fclose(f);
    f=fopen(argv[2],"rb");assert(f);uint32_t tracks=word(f);assert(tracks==80);
    for(unsigned k=0;k<tracks;k++) {
        track=word(f);assert(track==k);seen=0;uint32_t count=word(f);
        mfm_decoder_t d;mfm_dec_init(&d,250000,sector,NULL);
        for(unsigned i=0;i<count;i++)mfm_dec_interval(&d,word(f));
        if(seen!=511){fprintf(stderr,"track %u: missing mask %x\n",track,511^seen);return 1;}
    }
    assert(fgetc(f)==EOF);fclose(f);
    printf("Independent SCP corpus: 80 tracks, 720 sectors byte-identical, %u valid observations, %u rejected CRC observations\n",valid,invalid);
}
