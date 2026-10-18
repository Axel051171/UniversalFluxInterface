#include "ufi_capabilities.h"
#include "ufi_write_codec.h"
#include "ufi_write_wave.h"
#include <string.h>
static uint32_t config_generation=1;
void ufi_config_changed(void)
{
    if(!++config_generation)config_generation=1;
}
uint32_t ufi_config_generation(void)
{
    return config_generation;
}
static void put(uint8_t* p,uint32_t v)
{
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));
}
void ufi_capability_matrix(const uint8_t port[24],bool busy,uint8_t out[UFI_CAP_MATRIX_SIZE])
{
    /* Rate bits follow the ordered list in the protocol, not media density.  Index = row id:
     * 0 raw flux (format independent), 1..7 decoder families, 8 IEC block path. */
    static const uint32_t rates[UFI_CAP_ROWS]={0,0x0f,0x0f,0x37,0x04,0x1c1,0x3e04,0x01,0};
    uint32_t flags=UFI_CAP_UNCONFIRMED|UFI_WRITE_UNQUALIFIED;
    const bool flux=(port[2]&1)!=0;
    const bool apple=flux && !(port[2]&8);              /* flux drive without density output */
    const bool iec=!flux && port[1]!=0;                 /* selected, but not on the flux engine */
    if(!flux)flags|=UFI_CAP_NO_FLUX_PORT;
    if(busy)flags|=UFI_CAP_BUSY;
    if(port[7])flags|=UFI_CAP_LINE_CONFLICT;
    const bool physical=!busy && flux && !port[7];     /* capture / raw write admissible at all */
    memset(out,0,UFI_CAP_MATRIX_SIZE);out[0]=2;out[1]=UFI_CAP_ROWS;out[2]=32;out[3]=32;
    put(out+4,ufi_config_generation());put(out+8,flags);put(out+12,63);
    put(out+16,FLUX_TIMER_FREQ);out[20]=port[1];out[21]=23;out[22]=24;out[23]=25;
    out[24]=26; /* analysis start guarded by configuration and media */
    out[25]=27;out[26]=28;out[27]=29; /* multitrack create/status/control */
    out[28]=30;out[29]=31;out[30]=32; /* checkpoint, raw timing options/report */
    for(unsigned id=0;id<UFI_CAP_ROWS;id++){
        uint8_t* p=out+32+32*id;p[0]=(uint8_t)id;
        /* Admission only: source, media, protection, interval limits still checked. */
        if(id==UFI_CAP_ROW_RAW){
            p[1]=5;                       /* capture and raw-flux write, no decoder, no content verify */
            p[2]=physical?5:0;
            p[3]=5;                       /* stream parser and write codec covered offline */
            p[5]=3;                       /* classic and extended backends */
            p[6]=physical?(apple?1:3):0;  /* extended excludes Apple */
            p[7]=7;                       /* source required, interval preflight, physical setup */
            put(p+12,WRITE_MIN_DELTA);put(p+16,WRITE_MAX_DELTA);put(p+20,WAVE_MIN_TICKS);put(p+24,UINT32_MAX);
        } else if(id==UFI_CAP_ROW_IEC){
            p[1]=13;                      /* read blocks, write blocks, read-back verify; no flux decoder */
            p[2]=(!busy && iec)?13:0;
            p[3]=13;                      /* IEC regressions cover the three paths offline */
            p[7]=5;                       /* source required, physical setup required */
        } else if(id==UFI_CAP_ROW_APPLE){
            p[1]=7;                       /* capture, decoder, raw write; no content verify reference */
            p[2]=busy?0:((physical && apple)?7:2);
            p[3]=2;                       /* decoder covered by the MOOF corpus */
            p[5]=1;                       /* classic backend only */
            p[6]=(physical && apple)?1:0;
            p[7]=7;
            put(p+8,rates[id]);put(p+12,WRITE_MIN_DELTA);put(p+16,WRITE_MAX_DELTA);
        } else {
            p[1]=15;
            p[2]=busy?0:((flags&(UFI_CAP_NO_FLUX_PORT|UFI_CAP_LINE_CONFLICT))?2:15);
            p[3]=10; /* decoder and content matcher covered by offline family fixtures */
            p[5]=3;  /* implemented raw-flux backends: classic and extended */
            p[6]=(p[2]&4)?(apple?1:3):0; /* extended excludes Apple */
            p[7]=7;  /* source required, interval preflight, physical setup required */
            put(p+8,rates[id]);put(p+12,WRITE_MIN_DELTA);put(p+16,WRITE_MAX_DELTA);
            put(p+20,WAVE_MIN_TICKS);put(p+24,UINT32_MAX);
        }
        p[4]=0;  /* no physical capture/write/verify qualification yet */
        put(p+28,flags);
    }
}
