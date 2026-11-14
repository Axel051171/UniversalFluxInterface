"""LAN rules of the v2 dispatcher (L4): what the LAN may not do, LAN.CFG protection, drive
ownership between USB and LAN, the transport routing.  Production functions are extracted
from ufi_v2.c verbatim (test_regressions.function)."""
from test_regressions import BASE, ERRORS, function, run


def lan_rules_tests():
    code = BASE + ERRORS + r'''
#include "ufi_lan.h"
#include "ufi_lan_session.h"
enum {UFI_V2_WRITE=0x22,V2_GUARDED_WRITE=0x27,V2_ERASE=0x23,V2_PATTERN=0x24,V2_FORMAT=0x26,
      V2_COPY_START=0x63,V2_WRITE_START=0x64,V2_IEC_BLOCK=0x35,V2_IEC_MEM=0x33,V2_IEC_TARGET=0x36,
      V2_BOOTLOADER=0x04,V2_USB_MODE=0x05,V2_READ_FILE=0x51,V2_WRITE_FILE=0x52,V2_DELETE=0x53,V2_IEC_SEND=0x31};
enum {V2_PING=0x00,V2_INFO=0x01,V2_STATUS=0x02,V2_EVENTS=0x06,V2_WRITE_LIMITS=0x08,V2_DESCRIBE=0x09,
      V2_RESOURCE=0x0A,V2_JOB=0x0B,V2_FEATURES=0x0C,V2_DIAGNOSTICS=0x0D,V2_QUALITY=0x0E,V2_ANALYSIS_JOB=0x0F,
      V2_DUMP_STATUS=0x61,V2_NET=LAN_V2_NET,V2_AUTH=LAN_V2_AUTH};
enum {UFI_USB_FLUX=0,UFI_USB_SD=1,UFI_USB_FLOPPY=2,UFI_USB_GW=3,UFI_USB_COMPOSITE=4};
enum {UFI_OWNER_NONE,UFI_OWNER_NATIVE,UFI_OWNER_GW};
#define OWNER_IDLE_MS 30000u
static bool write_ok,busy,op_read,op_write,iec_reset_pending;
static uint8_t usb_mode,res_owner;
static uint32_t now=1000;
static bool ufi_lan_write_allowed_(void){return write_ok;}
#define ufi_lan_write_allowed ufi_lan_write_allowed_
static bool ufi_operation_busy(void){return busy;}
static bool ufi_job_live(void){return false;}
static bool ufi_dump_active(void){return false;}
static uint8_t ufi_usb_get_mode_(void){return usb_mode;}
#define ufi_usb_get_mode ufi_usb_get_mode_
static uint8_t ufi_resources_owner_(void){return res_owner;}
#define ufi_resources_owner ufi_resources_owner_
static uint32_t HAL_GetTick_(void){return now;}
#define HAL_GetTick HAL_GetTick_
typedef struct {uint8_t route; volatile uint8_t owner; uint8_t ev_route; uint32_t owner_ms;} v2_link_t;
static v2_link_t link;
'''
    for name in ('locked_write', 'is_lan_cfg', 'lan_refusal', 'owner_busy', 'ufi_v2_claim',
                 'ufi_v2_owner', 'ufi_v2_route', 'analysis_changes', 'request_readonly'):
        code += function('ufi_v2.c', name)
    code += r'''
static int refusal_of_file(uint8_t cmd,const char* name){
    uint8_t p[80]={0};const size_t n=strlen(name);memcpy(p+6,name,n);return lan_refusal(cmd,p,(uint16_t)(6+n));
}
int main(void){
    uint8_t p[8]={0};
    /* firmware update and USB re-enumeration never over LAN, whatever lan_write says */
    write_ok=true;
    assert(lan_refusal(V2_BOOTLOADER,p,0)==UFI_ERR_LAN_DENIED && lan_refusal(V2_USB_MODE,p,1)==UFI_ERR_LAN_DENIED);
    /* writes: every session-lock write plus file writes/deletes and raw IEC bytes */
    for(unsigned cmd=0;cmd<256;cmd++)for(unsigned op=0;op<6;op++){
        p[0]=p[2]=(uint8_t)op;
        const bool w=locked_write((uint8_t)cmd,p,8)||cmd==V2_WRITE_FILE||cmd==V2_DELETE||cmd==V2_IEC_SEND;
        const bool usb_only=cmd==V2_BOOTLOADER||cmd==V2_USB_MODE;
        write_ok=false;
        assert(lan_refusal((uint8_t)cmd,p,8)==(usb_only?UFI_ERR_LAN_DENIED:w?UFI_ERR_WRITE_PROT:0));
        write_ok=true;
        if(cmd!=V2_READ_FILE)assert(lan_refusal((uint8_t)cmd,p,8)==(usb_only?UFI_ERR_LAN_DENIED:0));
    }
    /* LAN.CFG (lan_key) cannot be read over LAN in any spelling; other files can */
    const char* hidden[]={"LAN.CFG","lan.cfg","/LAN.CFG","0:LAN.CFG","DIR/Lan.Cfg","LAN.CFG.","LAN.CFG  ","\\LAN.CFG"};
    for(unsigned i=0;i<sizeof hidden/sizeof hidden[0];i++)assert(refusal_of_file(V2_READ_FILE,hidden[i])==UFI_ERR_LAN_DENIED);
    const char* open[]={"UFI.CFG","XLAN.CFG","LAN.CF","LAN.CFGX","DUMP0001.SCP","NET.TXT"};
    for(unsigned i=0;i<sizeof open/sizeof open[0];i++)assert(!refusal_of_file(V2_READ_FILE,open[i]));
    assert(!lan_refusal(V2_READ_FILE,p,6));                      /* no name: the parser refuses it */
    write_ok=false;assert(refusal_of_file(V2_WRITE_FILE,"LAN.CFG")==UFI_ERR_WRITE_PROT);
    write_ok=true;assert(!refusal_of_file(V2_WRITE_FILE,"LAN.CFG"));   /* lan_write=1 may change it */

    /* ownership: first claim wins, the other transport is busy */
    assert(ufi_v2_route()==UFI_ROUTE_USB && !ufi_v2_owner());
    assert(ufi_v2_claim(UFI_ROUTE_LAN) && ufi_v2_owner()==UFI_ROUTE_LAN && ufi_v2_route()==UFI_ROUTE_LAN);
    assert(!ufi_v2_claim(UFI_ROUTE_USB));
    now+=10u*OWNER_IDLE_MS;assert(!ufi_v2_claim(UFI_ROUTE_USB));   /* a LAN session has no idle end */
    link.route=UFI_ROUTE_USB;assert(ufi_v2_route()==UFI_ROUTE_USB);link.route=0;   /* replies go to the asker */
    link.owner=UFI_ROUTE_NONE;
    assert(ufi_v2_claim(UFI_ROUTE_USB) && !ufi_v2_claim(UFI_ROUTE_LAN));
    now+=OWNER_IDLE_MS-1;assert(!ufi_v2_claim(UFI_ROUTE_LAN));
    now+=1;busy=true;assert(!ufi_v2_claim(UFI_ROUTE_LAN));       /* idle, but something runs */
    busy=false;op_read=true;assert(!ufi_v2_claim(UFI_ROUTE_LAN));
    op_read=false;assert(ufi_v2_claim(UFI_ROUTE_LAN) && ufi_v2_owner()==UFI_ROUTE_LAN);  /* 30 s USB silence */
    /* LAN only while USB speaks the UFI protocol and Greaseweazle does not hold the drive */
    link.owner=UFI_ROUTE_NONE;
    for(uint8_t m=0;m<=4;m++){usb_mode=m;assert(ufi_v2_claim(UFI_ROUTE_LAN)==(m==UFI_USB_FLUX||m==UFI_USB_COMPOSITE));link.owner=0;}
    usb_mode=UFI_USB_COMPOSITE;res_owner=UFI_OWNER_GW;assert(!ufi_v2_claim(UFI_ROUTE_LAN) && ufi_v2_claim(UFI_ROUTE_USB));
    /* Which requests need the drive ownership.  ANALYSIS_JOB ops that start something change
     * state in every form; ops with a "set" form change it only with a payload behind the op
     * byte.  Ops 37, 41, 45, 50, 52 and 55 were treated as queries until 2026-11-14: the other
     * transport could change them under a session that owned the drive. */
    static const uint8_t always[]={1,4,5,16,24,26,27,29,52};
    static const uint8_t with_payload[]={7,10,11,14,15,18,30,31,33,34,36,37,41,45,46,50,55};
    for(unsigned op=0;op<256;op++){
        bool a=false,w=false;
        for(unsigned i=0;i<sizeof always;i++)a|=always[i]==op;
        for(unsigned i=0;i<sizeof with_payload;i++)w|=with_payload[i]==op;
        uint8_t q[4]={(uint8_t)op,1,0,0};
        assert(request_readonly(V2_ANALYSIS_JOB,q,1)==!a);
        assert(request_readonly(V2_ANALYSIS_JOB,q,2)==!(a||w) && request_readonly(V2_ANALYSIS_JOB,q,4)==!(a||w));
    }
    assert(!request_readonly(V2_ANALYSIS_JOB,p,0));                 /* no op byte: not a query */
    for(unsigned cmd=0;cmd<256;cmd++){
        const bool query=cmd==V2_PING||cmd==V2_INFO||cmd==V2_STATUS||cmd==V2_DESCRIBE||cmd==V2_FEATURES||
            cmd==V2_DIAGNOSTICS||cmd==V2_WRITE_LIMITS||cmd==V2_EVENTS||cmd==V2_DUMP_STATUS||cmd==V2_RESOURCE||
            cmd==V2_QUALITY||cmd==V2_NET||cmd==V2_AUTH||cmd==V2_JOB;
        if(cmd==V2_ANALYSIS_JOB)continue;
        assert(request_readonly((uint8_t)cmd,p,0)==query);
        assert(request_readonly((uint8_t)cmd,p,8)==(query && cmd!=V2_JOB));
    }
    /* the consequence: a changing op from the other transport finds the drive owned */
    link.owner=UFI_ROUTE_NONE;usb_mode=UFI_USB_FLUX;res_owner=UFI_OWNER_NONE;
    assert(ufi_v2_claim(UFI_ROUTE_USB));
    { uint8_t lock[2]={37,1},tol[4]={41,1,30,0},read[1]={41};
      assert(!request_readonly(V2_ANALYSIS_JOB,lock,2) && !ufi_v2_claim(UFI_ROUTE_LAN));
      assert(!request_readonly(V2_ANALYSIS_JOB,tol,4) && request_readonly(V2_ANALYSIS_JOB,read,1)); }
    puts("LAN rules: USB-only ops, write class, LAN.CFG spellings, ownership, idle end, USB modes, query/changing ops passed");
    return 0;
}
'''
    run('regression_lan_rules', code)


if __name__ == '__main__':
    lan_rules_tests()
