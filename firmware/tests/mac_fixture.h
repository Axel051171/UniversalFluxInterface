/* Synthetic Macintosh 3.5-inch track generator, test-only inverse checksum.
 * Layout references: docs/Floppy_Emu_Review.md. Never linked into firmware. */
#include <stdint.h>
static const uint8_t mac_codes[64]={
0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
static uint32_t mac_flux[100000],mac_count,mac_rate=500000;
static uint64_t mac_pos,mac_last;
static void mac_byte(uint8_t v){
    for(int b=7;b>=0;b--){
        mac_pos++;
        if((v>>b)&1){uint64_t t=mac_pos*275000000/mac_rate;
            assert(mac_count<100000);mac_flux[mac_count++]=(uint32_t)(t-mac_last);mac_last=t;}
    }
}
static void mac_sync(void){for(unsigned i=0;i<16;i++)mac_byte(0xff);}
static void mac_sector(unsigned cyl,unsigned head,unsigned sector,const uint8_t data[524],unsigned faults){
    mac_sync();mac_byte(0xd5);mac_byte(0xaa);mac_byte(0x96);
    uint8_t hdr[5]={(uint8_t)(cyl&63),(uint8_t)sector,(uint8_t)((head<<5)|(cyl>>6)),0x22,0};
    for(unsigned i=0;i<4;i++){hdr[4]^=hdr[i];}
    if(faults&1)hdr[4]^=1;
    for(unsigned i=0;i<5;i++)mac_byte(mac_codes[hdr[i]]);
    mac_byte(0xde);mac_byte(0xaa);mac_sync();mac_byte(0xd5);mac_byte(0xaa);mac_byte(0xad);
    mac_byte(mac_codes[(sector+((faults&4)?1:0))&63]);
    uint16_t a=0,b=0,c=0;unsigned at=0;
    while(at<524){
        a=(uint16_t)((a&255)<<1);if(a&256)a++;
        uint8_t x=(uint8_t)(data[at]^a);c+=data[at++];if(a&256){c++;a&=255;}
        uint8_t y=(uint8_t)(data[at]^c);b+=data[at++];if(c>255){b++;c&=255;}
        bool last=at==524;uint8_t z=0;
        if(!last){z=(uint8_t)(data[at]^b);a+=data[at++];if(b>255){a++;b&=255;}}
        mac_byte(mac_codes[((x>>2)&48)|((y>>4)&12)|(z>>6)]);
        mac_byte(mac_codes[x&63]);mac_byte(mac_codes[y&63]);if(!last)mac_byte(mac_codes[z&63]);
    }
    mac_byte(mac_codes[((a>>6)&3)|((b>>4)&12)|((c>>2)&48)]);
    mac_byte(mac_codes[c&63]);mac_byte(mac_codes[b&63]);mac_byte(mac_codes[(a^((faults&2)?1:0))&63]);
    mac_byte(0xde);mac_byte((faults&8)?0xab:0xaa);mac_sync();
}
static void mac_reset(uint32_t rate){mac_count=0;mac_pos=mac_last=0;mac_rate=rate;}
