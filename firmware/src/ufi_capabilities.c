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
    /* Rate bits follow the ordered list in the protocol, not media density. */
    static const uint32_t rates[]={0x0f,0x0f,0x37,0x04,0x1c1,0x3e04};
    uint32_t flags=UFI_CAP_UNCONFIRMED|UFI_WRITE_UNQUALIFIED;
    if(!(port[2]&1))flags|=UFI_CAP_NO_FLUX_PORT;
    if(busy)flags|=UFI_CAP_BUSY;
    if(port[7])flags|=UFI_CAP_LINE_CONFLICT;
    memset(out,0,UFI_CAP_MATRIX_SIZE);out[0]=1;out[1]=6;out[2]=32;out[3]=32;
    put(out+4,ufi_config_generation());put(out+8,flags);put(out+12,63);
    put(out+16,FLUX_TIMER_FREQ);out[20]=port[1];out[21]=23;out[22]=24;out[23]=25;
    out[24]=26; /* analysis start guarded by configuration and media */
    out[25]=27;out[26]=28;out[27]=29; /* multitrack create/status/control */
    out[28]=30;out[29]=31;out[30]=32; /* checkpoint, raw timing options/report */
    for(unsigned f=1;f<=6;f++){
        uint8_t* p=out+32+32*(f-1);p[0]=(uint8_t)f;p[1]=15;
        /* Admission only: source, media, protection, interval limits still checked. */
        p[2]=busy?0:((flags&(UFI_CAP_NO_FLUX_PORT|UFI_CAP_LINE_CONFLICT))?2:15);
        p[3]=10; /* decoder and content matcher covered by offline family fixtures */
        p[4]=0;  /* no physical capture/write/verify qualification */
        p[5]=3;  /* implemented raw-flux backends: classic and extended */
        p[6]=(p[2]&4)?((port[2]&8)?3:1):0; /* extended excludes Apple */
        p[7]=7;  /* source required, interval preflight, physical setup required */
        put(p+8,rates[f-1]);put(p+12,WRITE_MIN_DELTA);put(p+16,WRITE_MAX_DELTA);
        put(p+20,WAVE_MIN_TICKS);put(p+24,UINT32_MAX);put(p+28,flags);
    }
}
