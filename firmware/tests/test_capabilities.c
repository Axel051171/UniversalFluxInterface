#include "ufi_capabilities.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
#define ROW(id) (out+32+32*(id))
int main(void)
{
    uint8_t port[24]={1,1,9},out[UFI_CAP_MATRIX_SIZE];     /* Shugart drive: flux + density output */
    ufi_capability_matrix(port,false,out);
    assert(UFI_CAP_MATRIX_SIZE==320 && out[0]==2 && out[1]==9 && out[2]==32 && out[3]==32 && rd(out+8)==3);
    assert(out[20]==1 && out[21]==23 && out[30]==32 && !out[31]);
    for(unsigned id=0;id<9;id++)assert(ROW(id)[0]==id && !ROW(id)[4] && rd(ROW(id)+28)==3);
    for(unsigned id=1;id<=6;id++){                          /* the six families as in schema 1 */
        const uint8_t* p=ROW(id);
        assert(p[1]==15 && p[2]==15 && p[3]==10 && p[5]==3 && p[6]==3 && p[7]==7);
        assert(rd(p+12)==440 && rd(p+16)==65536 && rd(p+20)==220 && rd(p+24)==UINT32_MAX);
    }
    /* DD, HD and ED are decoder modes, never physical write qualifications. */
    assert((rd(ROW(1)+8)&13)==13 && !ROW(1)[4]);
    /* row 0: raw flux without decoder; row 7: Apple decoder only on a non-Apple drive; row 8: IEC off */
    assert(ROW(0)[1]==5 && ROW(0)[2]==5 && ROW(0)[3]==5 && ROW(0)[5]==3 && ROW(0)[6]==3 && !rd(ROW(0)+8) && rd(ROW(0)+12)==440 && rd(ROW(0)+24)==UINT32_MAX);
    assert(ROW(7)[1]==7 && ROW(7)[2]==2 && ROW(7)[3]==2 && ROW(7)[5]==1 && !ROW(7)[6] && rd(ROW(7)+8)==1 && !rd(ROW(7)+20));
    assert(ROW(8)[1]==13 && !ROW(8)[2] && ROW(8)[3]==13 && !ROW(8)[5] && !ROW(8)[6] && ROW(8)[7]==5 && !rd(ROW(8)+8) && !rd(ROW(8)+12));
    /* Apple drive: classic backend only, Apple row fully admissible, raw row on the classic backend */
    port[2]=1;ufi_capability_matrix(port,false,out);
    assert(ROW(1)[6]==1 && ROW(0)[6]==1 && ROW(7)[2]==7 && ROW(7)[6]==1 && !ROW(8)[2]);
    /* line conflict: decoder paths only */
    port[7]=1;ufi_capability_matrix(port,false,out);
    assert((rd(out+8)&32) && ROW(1)[2]==2 && !ROW(1)[6] && !ROW(0)[2] && ROW(7)[2]==2);
    /* no flux port (nothing selected): no physical path anywhere */
    port[1]=0;port[2]=0;port[7]=0;ufi_capability_matrix(port,false,out);
    assert((rd(out+8)&8) && ROW(1)[2]==2 && !ROW(0)[2] && !ROW(8)[2] && !out[20]);
    /* IEC drive selected: only the IEC row is admissible */
    port[1]=5;ufi_capability_matrix(port,false,out);
    assert((rd(out+8)&8) && ROW(8)[2]==13 && ROW(1)[2]==2 && !ROW(0)[2] && ROW(7)[2]==2 && out[20]==5);
    /* busy: every admission mask 0, IEC too */
    ufi_capability_matrix(port,true,out);assert((rd(out+8)&16) && !ROW(8)[2] && !ROW(1)[2]);
    port[1]=1;port[2]=9;ufi_capability_matrix(port,true,out);
    assert((rd(out+8)&16) && !ROW(1)[2] && !ROW(1)[6] && !ROW(0)[2] && !ROW(7)[2]);
    uint32_t token=ufi_config_generation();ufi_config_changed();ufi_config_changed();
    assert(token!=ufi_config_generation());
    puts("capabilities: schema 2 rows (raw flux, six families, Apple, IEC), format/backend separation, ED qualification, busy and port constraints passed");
}
