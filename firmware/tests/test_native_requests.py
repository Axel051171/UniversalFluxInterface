"""Exercise native request rejection and compact preparation before side effects."""
from test_regressions import BASE, ERRORS, function, run


def main():
    code = BASE + ERRORS + r'''
#define REVOLUTIONS_STREAM 8u
#define FLUX_TIMER_FREQ 275000000u
enum {JOB_OPEN};
static struct {unsigned kind,state;} ufi_job;
static bool op_read;
static uint8_t read_seq,hard_sectors=9;
static unsigned starts,line_calls;
static int start_error;
static bool ufi_job_live(void){return false;}
static bool ufi_drive_is_apple(void){return false;}
static uint32_t get16(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8;}
static void ufi_flux_hard_sectors(uint8_t n){hard_sectors=n;line_calls++;}
static void ufi_flux_evidence_context(uint8_t p[6]){memset(p,0,6);p[4]=hard_sectors;}
static void ufi_stream_begin_v2(uint8_t seq){(void)seq;}
static int ufi_capture_start_q(uint8_t c,uint8_t h,uint8_t rev,uint32_t period,uint8_t quarter){
    (void)c;(void)h;(void)rev;(void)period;(void)quarter;starts++;return start_error;
}
'''
    code += function('ufi_v2.c', 'cmd_read') + r'''
int main(void){
    uint8_t p[12]={0,0,2,0,0,0,5};
    assert(!cmd_read(7,p,3) && starts==1 && op_read && read_seq==7);
    unsigned before=starts,calls=line_calls;
    p[3]=8;assert(cmd_read(8,p,6)==UFI_ERR_BAD_ARGS && starts==before && line_calls==calls);
    p[3]=1;p[4]=0xd1;p[5]=7;assert(cmd_read(8,p,6)==UFI_ERR_BAD_ARGS && starts==before);
    p[4]=p[5]=0;assert(!cmd_read(8,p,4));
    p[3]=2;assert(!cmd_read(8,p,7) && hard_sectors==5);
    p[6]=1;calls=line_calls;assert(cmd_read(8,p,7)==UFI_ERR_BAD_ARGS && line_calls==calls);
    p[6]=7;start_error=UFI_ERR_SEEK_FAIL;
    assert(cmd_read(8,p,7)==UFI_ERR_SEEK_FAIL && hard_sectors==5);start_error=0;
    for(unsigned flags=0;flags<256;flags++)for(unsigned len=0;len<=sizeof(p);len++){
        p[3]=(uint8_t)flags;calls=line_calls;before=starts;
        int ret=cmd_read(9,p,(uint16_t)len);
        if(ret)assert(calls==line_calls && starts==before);
    }
    puts("native READ: exhaustive flags/lengths, no malformed side effects, failure rollback passed");
}
'''
    run('regression_native_read_requests', code)
    code = BASE + ERRORS + r'''
#include "ufi_write_codec.h"
static uint32_t buffer[8],*write_buffer=buffer;
static struct {bool compact;uint8_t quarter;uint32_t bytes_expected;compact_dec_t dec;} g_write;
static unsigned preparations;
static int ufi_write_prepare(uint8_t c,uint8_t h,uint32_t count,bool verify){
    (void)c;(void)h;(void)count;(void)verify;preparations++;return 0;
}
'''
    code += function('ufi_write.c', 'ufi_write_prepare_compact') + r'''
int main(void){
    assert(ufi_write_prepare_compact(0,0,3,2,true)==UFI_ERR_BUFFER_FULL && !preparations);
    assert(ufi_write_prepare_compact(0,0,3,16,true)==UFI_ERR_BUFFER_FULL && !preparations);
    assert(!ufi_write_prepare_compact(0,0,3,15,true) && preparations==1 && g_write.compact);
    assert(!ufi_write_prepare_compact(0,0,3,3,false) && preparations==2);
    puts("compact WRITE: size rejection precedes preparation/evidence invalidation passed");
}
'''
    run('regression_compact_preparation', code, ('src/ufi_write_codec.c',))

    # Session write lock (op 37, 4.6): the board keeps it until reset, the jumper stays
    # separate, and the v2 dispatcher refuses exactly the disk/drive-changing requests.
    code = BASE + ERRORS + r'''
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
static const struct {int port,pin;} PIN_WLOCK;
static int jumper;
static int HAL_GPIO_ReadPin(int port,int pin){(void)port;(void)pin;return jumper;}
static bool session_lock;
'''
    for name in ('ufi_board_soft_lock','ufi_board_soft_locked','ufi_board_jumper_locked','ufi_board_write_locked'):
        code += function('ufi_board.c', name)
    code += r'''
enum {UFI_V2_WRITE=0x22,V2_GUARDED_WRITE=0x27,V2_ERASE=0x23,V2_PATTERN=0x24,V2_FORMAT=0x26,
      V2_COPY_START=0x63,V2_WRITE_START=0x64,V2_IEC_BLOCK=0x35,V2_IEC_MEM=0x33,V2_IEC_TARGET=0x36};
'''
    code += function('ufi_v2.c', 'locked_write') + r'''
int main(void){
    assert(!ufi_board_soft_locked() && !ufi_board_write_locked() && !ufi_board_jumper_locked());
    jumper=1;assert(ufi_board_jumper_locked() && ufi_board_write_locked() && !ufi_board_soft_locked());
    jumper=0;ufi_board_soft_lock();ufi_board_soft_lock();
    assert(ufi_board_soft_locked() && ufi_board_write_locked() && !ufi_board_jumper_locked());
    uint8_t p[8]={0};
    for(unsigned cmd=0;cmd<256;cmd++)for(unsigned op=0;op<6;op++){
        p[0]=p[2]=(uint8_t)op;
        const bool always=cmd==0x22||cmd==0x27||cmd==0x23||cmd==0x24||cmd==0x26||cmd==0x63||cmd==0x64;
        const bool iec=(cmd==0x35 && op==1)||((cmd==0x33||cmd==0x36) && (op==0||op==2||op==3));
        assert(locked_write((uint8_t)cmd,p,8)==(always||iec));
        assert(locked_write((uint8_t)cmd,p,0)==always);       /* empty payload: only the plain commands */
    }
    p[2]=0;assert(!locked_write(0x36,p,2) && locked_write(0x36,p,3));  /* TARGET needs the MEM op byte */
    puts("session write lock: board state, jumper separation, v2 refusal set passed");
}
'''
    run('regression_session_write_lock', code)


if __name__ == '__main__':
    main()
