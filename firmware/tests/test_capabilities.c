#include "ufi_capabilities.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void)
{
    uint8_t port[24]={1,1,9},out[UFI_CAP_MATRIX_SIZE];
    ufi_capability_matrix(port,false,out);
    assert(out[0]==1 && out[1]==6 && rd(out+8)==3);
    for(unsigned i=0;i<6;i++){
        const uint8_t* p=out+32+32*i;
        assert(p[0]==i+1 && p[1]==15 && p[2]==15 && p[3]==10 && !p[4]);
        assert(p[6]==3 && rd(p+12)==440 && rd(p+20)==220);
    }
    /* DD, HD and ED are decoder modes, never physical write qualifications. */
    assert((rd(out+40)&13)==13 && !out[36]);
    port[2]=1;ufi_capability_matrix(port,false,out);assert(out[38]==1); /* Apple */
    port[7]=1;ufi_capability_matrix(port,false,out);
    assert((rd(out+8)&32) && out[34]==2 && !out[38]);
    port[2]=0;port[7]=0;ufi_capability_matrix(port,false,out);
    assert((rd(out+8)&8) && out[34]==2);
    port[2]=9;ufi_capability_matrix(port,true,out);
    assert((rd(out+8)&16) && !out[34] && !out[38]);
    uint32_t token=ufi_config_generation();ufi_config_changed();ufi_config_changed();
    assert(token!=ufi_config_generation());
    puts("capabilities: format/backend separation, ED qualification, busy and port constraints passed");
}
