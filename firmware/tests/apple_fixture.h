/* Synthetic Apple II 5.25-inch 16-sector 6-and-2 track generator (test only, never
 * linked into firmware).  Layout references: docs/REA_Floppy_Emu_Analyse_2026-10-17.md.
 * Cells are 4 us at 250 kbit/s; sync nibbles are FF with two trailing zero cells. */
#include <stdint.h>
static const uint8_t apl_codes[64]={
0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
static uint32_t apl_flux[100000],apl_count;
static uint64_t apl_pos,apl_last;
static void apl_bit(unsigned v){
    apl_pos++;
    if(v){uint64_t t=apl_pos*1100u;assert(apl_count<100000);apl_flux[apl_count++]=(uint32_t)(t-apl_last);apl_last=t;}
}
static void apl_byte(uint8_t v){for(int b=7;b>=0;b--)apl_bit((v>>b)&1u);}
static void apl_sync(unsigned n){for(unsigned i=0;i<n;i++){apl_byte(0xff);apl_bit(0);apl_bit(0);}}
static void apl_44(uint8_t v){apl_byte((uint8_t)((v>>1)|0xaa));apl_byte((uint8_t)(v|0xaa));}
static void apl_reset(void){apl_count=0;apl_pos=0;apl_last=0;}
/* marks (copy protections change them): address/data prologue, address/data epilogue */
static uint8_t apl_ap[3]={0xd5,0xaa,0x96},apl_dp[3]={0xd5,0xaa,0xad},apl_ae[2]={0xde,0xaa},apl_de[2]={0xde,0xaa};
static void apl_marks(uint8_t a0,uint8_t a1,uint8_t a2,uint8_t d0,uint8_t d1,uint8_t d2,uint8_t ae0,uint8_t ae1,uint8_t de0,uint8_t de1){
    apl_ap[0]=a0;apl_ap[1]=a1;apl_ap[2]=a2;apl_dp[0]=d0;apl_dp[1]=d1;apl_dp[2]=d2;
    apl_ae[0]=ae0;apl_ae[1]=ae1;apl_de[0]=de0;apl_de[1]=de1;
}
/* faults: 1 bad address checksum, 2 bad data checksum, 4 bad data epilogue */
/* 13-sector 5-and-3 sector (DOS 3.2): address prologue apl_ap (set D5 AA B5), 410 data nibbles
 * laid out as documented (low-bit block of 154, then high-bit block of 256), XOR chain, checksum. */
static const uint8_t apl_codes53[32]={0xab,0xad,0xae,0xaf,0xb5,0xb6,0xb7,0xba,0xbb,0xbd,0xbe,0xbf,0xd6,0xd7,0xda,0xdb,
                                      0xdd,0xde,0xdf,0xea,0xeb,0xed,0xee,0xef,0xf5,0xf6,0xf7,0xfa,0xfb,0xfd,0xfe,0xff};
static void apl_sector53(unsigned volume,unsigned track,unsigned sector,const uint8_t b[256],unsigned faults){
    apl_sync(10);apl_byte(apl_ap[0]);apl_byte(apl_ap[1]);apl_byte(apl_ap[2]);
    apl_44((uint8_t)volume);apl_44((uint8_t)track);apl_44((uint8_t)sector);apl_44((uint8_t)(volume^track^sector^((faults&1)?1:0)));
    apl_byte(apl_ae[0]);apl_byte(apl_ae[1]);apl_byte(0xeb);
    apl_sync(6);apl_byte(apl_dp[0]);apl_byte(apl_dp[1]);apl_byte(apl_dp[2]);
    uint8_t x[410];unsigned n=0;
    x[n++]=(uint8_t)(b[255]&7u);
    for(int k=2;k>=0;k--)for(unsigned j=0;j<51;j++)
        x[n++]=(uint8_t)(((b[5*j+k]&7u)<<2)|(((b[5*j+3]>>(2-k))&1u)<<1)|((b[5*j+4]>>(2-k))&1u));
    for(unsigned k=0;k<5;k++)for(int j=50;j>=0;j--)x[n++]=(uint8_t)(b[5*j+k]>>3);
    x[n++]=(uint8_t)(b[255]>>3);
    uint8_t prev=0;
    for(unsigned i=0;i<410;i++){apl_byte(apl_codes53[(x[i]^prev)&31u]);prev=x[i];}
    apl_byte(apl_codes53[(prev^((faults&2)?1:0))&31u]);
    apl_byte(apl_de[0]);apl_byte(apl_de[1]);apl_byte(0xeb);apl_sync(4);
}
/* address field only (valid), no data field: an orphan header */
static void apl_header_only(unsigned volume,unsigned track,unsigned sector){
    apl_sync(10);apl_byte(apl_ap[0]);apl_byte(apl_ap[1]);apl_byte(apl_ap[2]);
    apl_44((uint8_t)volume);apl_44((uint8_t)track);apl_44((uint8_t)sector);apl_44((uint8_t)(volume^track^sector));
    apl_byte(apl_ae[0]);apl_byte(apl_ae[1]);apl_byte(0xeb);apl_sync(4);
}
static void apl_sector(unsigned volume,unsigned track,unsigned sector,const uint8_t data[256],unsigned faults){
    apl_sync(10);apl_byte(apl_ap[0]);apl_byte(apl_ap[1]);apl_byte(apl_ap[2]);
    apl_44((uint8_t)volume);apl_44((uint8_t)track);apl_44((uint8_t)sector);
    apl_44((uint8_t)(volume^track^sector^((faults&1)?1:0)));
    apl_byte(apl_ae[0]);apl_byte(apl_ae[1]);apl_byte(0xeb);
    apl_sync(6);apl_byte(apl_dp[0]);apl_byte(apl_dp[1]);apl_byte(apl_dp[2]);
    uint8_t x[343];
    for(unsigned i=0;i<86;i++){
        unsigned a=data[i]&3u,b=i+86<256?data[i+86]&3u:0,c=i+172<256?data[i+172]&3u:0;
        unsigned sw[3]={((a&1)<<1)|(a>>1),((b&1)<<1)|(b>>1),((c&1)<<1)|(c>>1)};
        x[i]=(uint8_t)(sw[0]|(sw[1]<<2)|(sw[2]<<4));
    }
    for(unsigned i=0;i<256;i++)x[86+i]=(uint8_t)(data[i]>>2);
    uint8_t prev=0;
    for(unsigned i=0;i<342;i++){apl_byte(apl_codes[x[i]^prev]);prev=x[i];}
    apl_byte(apl_codes[(prev^((faults&2)?1:0))&63]);
    apl_byte(apl_de[0]);apl_byte((faults&4)?(uint8_t)(apl_de[1]^1u):apl_de[1]);apl_byte(0xeb);
    apl_sync(4);
}
