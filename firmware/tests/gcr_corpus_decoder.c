#include "ufi_verify.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t image[174848];
static uint32_t deltas[100000];
static mfm_decoder_t decoder;
static unsigned track,sectors,base,observations;
static uint32_t found;
static void sector(void* ctx,const mfm_id_t* id,const uint8_t* data,bool ok){
    (void)ctx;
    assert(ok && id->c==track && !id->h && id->n==1 && id->r<sectors);
    assert(!memcmp(data,image+base+256*id->r,256));
    /* Upstream G64 writer uses literal ASCII "2A", independent of PETSCII BAM. */
    assert(data[256]=='A' && data[257]=='2');
    found|=1u<<id->r;observations++;
}
static uint32_t read32(FILE* f){uint8_t b[4];assert(fread(b,1,4,f)==4);return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
int main(int argc,char** argv){
    assert(argc==3);FILE* f=fopen(argv[1],"rb");assert(f && fread(image,1,sizeof(image),f)==sizeof(image));fclose(f);
    f=fopen(argv[2],"rb");assert(f && read32(f)==35);unsigned total=0;
    for(track=1;track<=35;track++){
        assert(read32(f)==track);uint32_t rate=read32(f),n=read32(f);assert(n<=100000);
        for(unsigned i=0;i<n;i++)deltas[i]=read32(f);
        sectors=track<=17?21:track<=24?19:track<=30?18:17;found=0;
        mfm_dec_init_format(&decoder,5,rate,sector,NULL);
        for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<n;i++)mfm_dec_interval(&decoder,deltas[i]);
        assert(found==(1u<<sectors)-1);
        verify_ref_t ref;assert(verify_ref_build_u32(&ref,&decoder,deltas,n,0,NULL));
        assert(ref.format==5 && ref.n==sectors && !ref.conflicting);
        verify_match_t match;verify_match_begin(&match,&ref,&decoder);
        for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<n;i++)verify_match_interval(&match,deltas[i]);
        assert(verify_match_result(&match,false)==VERIFY_CONTENT);
        total+=sectors;base+=sectors*256;
    }
    assert(fgetc(f)==EOF);fclose(f);
    printf("External cc1541 GCR vectors: 35 tracks, %u sectors byte-identical, %u valid observations; reference/readback passed\n",total,observations);
}
