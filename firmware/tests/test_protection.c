#include "ufi_protection.h"
#include "check.h"
#include <string.h>
static quality_t q;
static protection_state_t state;
static sector_details_t details;
static mfm_decoder_t dec;
static track_compare_t comparison;
static uint8_t payload[1024],wire[289],track_data[9*512];
static uint32_t flux[100000];
static protection_options_t options={0,30,3,2};
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void init(uint16_t flags){
    memset(payload,0,sizeof(payload));quality_init(&q,1,250000,3);q.generation=42;
    q.extra.completed=7;for(unsigned r=0;r<3;r++)q.revolution_ticks[r]=1000000;
    quality_details_attach(&q,&details);options.flags=flags;protection_begin(&state,&q,&options);
    quality_begin(&q,&dec,0,NULL);track_compare_init(&comparison,42,3,1000000);comparison.completed=6;
}
static void observation(unsigned r,unsigned id,uint32_t position,bool valid){
    q.revolution=(uint8_t)r;q.extra.position=position;
    mfm_id_t key={0,0,(uint8_t)id,2};dec.cb(dec.ctx,&key,payload,valid);
}
static const protection_record_t* find(unsigned rule){
    for(unsigned i=0;i<state.count;i++)if(state.records[i].rule==rule)return &state.records[i];
    return NULL;
}
static void config_tests(void){
    uint8_t p[8]={55,1,0,0,3,2,30,0};protection_options_t out={0};
    for(unsigned flags=0;flags<128;flags++){
        p[2]=(uint8_t)flags;CHECK(protection_options_decode(p,8,&out)==(flags<=63),"flag mask %u",flags);
    }
    p[2]=1;p[3]=1;CHECK(!protection_options_decode(p,8,&out),"reserved high flags");p[3]=0;
    protection_options_t before=out;p[4]=1;CHECK(!protection_options_decode(p,8,&out) && !memcmp(&out,&before,sizeof out),"atomic invalid configuration");
    p[4]=8;p[5]=8;p[6]=250;CHECK(protection_options_decode(p,8,&out),"upper bounds");
    p[6]=251;CHECK(!protection_options_decode(p,8,&out),"timing threshold maximum");
    CHECK(!protection_options_decode(p,7,&out) && !protection_options_decode(NULL,8,&out),"length and pointer checks");
}
/* Independent bit/flux fixture writer. Standard MFM clocks, explicit illegal sync. */
static uint32_t fixture_position,fixture_gap;
static uint8_t fixture_previous;
static uint32_t fixture_cell=550;
static void raw_bit(unsigned value){
    fixture_position+=fixture_cell;fixture_gap+=fixture_cell;
    if(value){q.extra.position=fixture_position;quality_interval(&q,fixture_gap);fixture_gap=0;}
}
static void raw_word(uint16_t word){for(int k=15;k>=0;k--)raw_bit((word>>k)&1u);fixture_previous=(uint8_t)(word&1u);}
static void mfm_byte(uint8_t byte){
    for(int k=7;k>=0;k--){unsigned data=(byte>>k)&1u;raw_bit(!(fixture_previous || data));raw_bit(data);fixture_previous=(uint8_t)data;}
}
static void signature_fixture(unsigned rev,int fault){
    quality_revolution(&q,(uint8_t)rev);fixture_position=fixture_gap=0;fixture_previous=0;
    q.revolution_ticks[rev]=55000000;
    for(unsigned i=0;i<40;i++)mfm_byte(0);
    raw_word(fault==1?0x4489:0x8914);mfm_byte(fault==2?5:6);
    uint8_t data[512];memcpy(data,"Rob Northen Comp",16);
    uint32_t shift=0x123456;
    for(unsigned i=16;i<512;i++){data[i]=(uint8_t)(shift>>15);shift=((shift<<1)&0x7fffffu)|(((shift>>22)^shift)&1u);}
    if(fault==3)data[3]^=1;
    if(fault==4)data[400]^=1;
    if(fault==5)memset(data+16,0,496);
    unsigned limit=fault==6?400:512;
    for(unsigned i=0;i<limit;i++){
        if(fault==8 && i==400){
            uint16_t word=0;unsigned previous=fixture_previous;
            for(int k=7;k>=0;k--){unsigned d=(data[i]>>k)&1u;word=(uint16_t)((word<<2)|(!previous && !d?2:0)|d);previous=d;}
            raw_word((uint16_t)(word^0x8000u)); /* valid payload, deliberately illegal clock */
        }else mfm_byte(data[i]);
    }
    if(fault!=6)mfm_byte(fault==7?1:0);
    for(unsigned i=0;i<8;i++)mfm_byte(0);
    q.extra.completed|=(uint8_t)(1u<<rev);
}
int main(void){
    config_tests();init(0);
    CHECK(!q.protection && protection_pack(&q,0,8,wire,288)==32 && wire[1]==PROTECT_DISABLED,"default is disabled");
    init(PROTECT_CONTENT);for(unsigned r=0;r<3;r++)observation(r,1,200000,true);
    protection_finish(&q,&comparison);CHECK(state.state==PROTECT_NO_INDICATOR && state.checked==PROTECT_CONTENT && !state.count,"clean observations");
    init(PROTECT_CONTENT);for(unsigned r=0;r<3;r++){payload[0]=(uint8_t)r;observation(r,1,200000,true);}
    protection_finish(&q,&comparison);
    CHECK(find(PR_CONTENT) && find(PR_CONTENT)->level==2 && find(PR_CONTENT)->revs==7,"valid content variants across independent revolutions");
    CHECK(state.state==PROTECT_CANDIDATE,"variation is a candidate, never a signature");
    init(PROTECT_CONTENT);for(unsigned r=0;r<3;r++){observation(r,1,200000,true);q.profile=1;payload[0]=1;observation(r,1,200000,true);payload[0]=0;q.profile=0;}
    protection_finish(&q,&comparison);CHECK(!state.count,"PLL alternatives are not independent revolutions");
    init(PROTECT_CONTENT|PROTECT_ERRORS);dec.sector_flags=MFM_SF_DATA_CHECKSUM;
    for(unsigned r=0;r<3;r++){payload[0]=(uint8_t)r;observation(r,1,200000,false);}
    protection_finish(&q,&comparison);
    CHECK(find(PR_CONTENT_BAD_CRC) && (find(PR_CONTENT_BAD_CRC)->qualifiers&4) && !find(PR_STABLE_BAD_CRC),"changing bad-CRC data separated from stable errors");
    init(PROTECT_ERRORS);dec.sector_flags=MFM_SF_DATA_CHECKSUM;
    for(unsigned r=0;r<3;r++)observation(r,1,200000,false);
    protection_finish(&q,NULL);CHECK(find(PR_STABLE_BAD_CRC) && find(PR_STABLE_BAD_CRC)->level==2,"stable bad CRC is a candidate");
    init(PROTECT_ERRORS);dec.sector_flags=MFM_SF_DATA_CHECKSUM;
    for(unsigned r=0;r<3;r++){payload[0]=(uint8_t)(r!=0);observation(r,1,200000,false);}
    protection_finish(&q,NULL);
    CHECK(find(PR_STABLE_BAD_CRC) && find(PR_STABLE_BAD_CRC)->revs==6,"A,B,B retains the repeated second bad-CRC variant");
    init(PROTECT_ERRORS);dec.sector_flags=MFM_SF_DATA_CHECKSUM;
    for(unsigned r=0;r<3;r++)for(unsigned id=1;id<=32;id++){payload[0]=(uint8_t)r;observation(r,id,200000,false);}
    protection_finish(&q,NULL);
    CHECK(state.bad_count==64 && (state.limitations&PROTECT_OVERFLOW) && state.insufficient==PROTECT_ERRORS,"bad-CRC variant exhaustion is explicit incomplete coverage");
    init(PROTECT_ERRORS);dec.sector_flags=MFM_SF_DATA_CHECKSUM|MFM_SF_HEADER_CHECKSUM;
    for(unsigned r=0;r<3;r++)observation(r,1,200000,false);
    protection_finish(&q,NULL);CHECK(!state.count && state.state==PROTECT_INSUFFICIENT,"untrusted headers cannot establish repeated sectors");
    init(PROTECT_LAYOUT|PROTECT_CONTENT|PROTECT_POSITION);
    for(unsigned r=0;r<3;r++){payload[0]=0;observation(r,1,200000,true);payload[0]=1;observation(r,1,600000,true);}
    protection_finish(&q,&comparison);
    CHECK(find(PR_DUPLICATE) && find(PR_DUPLICATE)->level==2 && !find(PR_CONTENT),"static duplicate sectors are not weak content");
    CHECK(state.insufficient&PROTECT_CONTENT,"duplicate identity ambiguity explicit");
    init(PROTECT_POSITION);
    unsigned orders[3][3]={{1,3,2},{3,2,1},{1,3,2}};
    for(unsigned r=0;r<3;r++)for(unsigned k=0;k<3;k++)observation(r,orders[r][k],100000+200000*k,true);
    protection_finish(&q,NULL);CHECK(!find(PR_ORDER),"fixed interleave and circular index rotation accepted");
    init(PROTECT_POSITION);orders[2][1]=2;orders[2][2]=3;
    for(unsigned r=0;r<3;r++)for(unsigned k=0;k<3;k++)observation(r,orders[r][k],100000+200000*k,true);
    protection_finish(&q,NULL);CHECK(find(PR_ORDER) && find(PR_ORDER)->level==1,"changing rotational order is an observation");
    init(PROTECT_TIMING);
    for(unsigned k=0;k<4;k++)q.local[k]=(quality_local_t){.sum=128000u*100,.count=100,.min=128000,.max=128000,.multiples=6};
    q.local[1].sum=140800u*100;q.local[1].min=q.local[1].max=140800;
    protection_finish(&q,NULL);CHECK(find(PR_RATE_CHANGE) && find(PR_RATE_CHANGE)->value==100,"local density change reported in permille");
    CHECK(state.state==PROTECT_ANOMALY,"density alone never proves protection");
    init(PROTECT_CONTENT);observation(0,1,200000,true);protection_finish(&q,NULL);
    CHECK(state.state==PROTECT_INSUFFICIENT && (state.limitations&PROTECT_NO_COMPARE),"insufficient data never means unprotected");
    init(PROTECT_LAYOUT);for(unsigned r=0;r<3;r++)for(unsigned k=0;k<64;k++){observation(r,k,100000+1000*k,true);observation(r,k,600000+1000*k,true);}
    protection_finish(&q,NULL);
    CHECK(state.count==48 && state.dropped==16 && (state.limitations&PROTECT_OVERFLOW),"bounded report overflow explicit");
    protection_finish(&q,NULL);CHECK(state.dropped==16,"finalization is idempotent with overflow");
    memset(wire,0xa5,sizeof wire);CHECK(protection_pack(&q,0,8,wire,288)==288 && wire[288]==0xa5 && rd(wire+16)==16,"bounded page header and canary");
    CHECK(!protection_pack(&q,49,1,wire,288) && !protection_pack(&q,0,9,wire,288),"page cursor and count bounds");
    CHECK(protection_pack(&q,48,8,wire,32)==32 && !wire[3],"end cursor");
    CHECK(!protection_pack(&q,0,1,wire,63),"short output rejected");
    init(PROTECT_SIGNATURE);q.format=0;for(unsigned r=0;r<3;r++)signature_fixture(r,0);
    protection_finish(&q,NULL);
    CHECK(find(PR_COPYLOCK_SECTOR6) && state.state==PROTECT_KNOWN_SIGNATURE && state.signature_revs==7,"modern Copylock sector-6 signature, full LFSR and footer");
    CHECK(state.checked==PROTECT_SIGNATURE && !state.insufficient,"raw signature independent of ordinary decoder");
    q.status=6;protection_finish(&q,NULL);
    CHECK(state.state==PROTECT_ANOMALY && state.insufficient==PROTECT_SIGNATURE,"failed capture cannot produce confirmed signature status");
    for(int fault=1;fault<=8;fault++){
        init(PROTECT_SIGNATURE);q.format=0;for(unsigned r=0;r<3;r++)signature_fixture(r,fault);
        protection_finish(&q,NULL);CHECK(!find(PR_COPYLOCK_SECTOR6),"Copylock false-positive control %d",fault);
    }
    init(PROTECT_SIGNATURE);q.format=0;signature_fixture(0,0);protection_finish(&q,NULL);
    CHECK(state.state==PROTECT_ANOMALY && (state.insufficient&PROTECT_SIGNATURE),"one matching revolution remains an observation");
    init(PROTECT_SIGNATURE);q.format=0;fixture_cell=577;
    for(unsigned r=0;r<3;r++)signature_fixture(r,0);
    protection_finish(&q,NULL);CHECK(state.state==PROTECT_KNOWN_SIGNATURE,"Copylock DD scanner follows five percent speed variation");fixture_cell=550;
    /* Real ordinary IBM track is a negative control through all production hooks. */
    init(PROTECT_MASK);mfm_track_fmt_t fmt={250000,200000,9,24,false,0};
    uint32_t n=mfm_encode_track(&fmt,0,0,track_data,flux,100000),period=55000000;
    uint32_t carry=0;q.extra.completed=0;
    for(unsigned r=0;r<3;r++){
        quality_revolution(&q,(uint8_t)r);q.revolution_ticks[r]=period;uint32_t position=0;
        for(unsigned i=0;i<n;i++){position+=flux[i];q.extra.position=position;quality_interval(&q,flux[i]+(i?0:carry));}
        carry=period-position;quality_tail(&q,position,period);quality_end_revolution(&q);
    }
    comparison.reference_period=period;protection_finish(&q,&comparison);
    CHECK(q.n==9 && !state.count && state.state==PROTECT_NO_INDICATOR,"ordinary IBM track: all flags, zero false positives");
    CHECK(!state.insufficient && state.checked==PROTECT_MASK,"ordinary track full configured coverage");
    for(unsigned format=1;format<=7;format++){
        init(PROTECT_CONTENT|PROTECT_ERRORS|PROTECT_LAYOUT|PROTECT_POSITION);q.format=(uint8_t)format;
        quality_begin(&q,&dec,0,NULL);
        for(unsigned r=0;r<3;r++)for(unsigned k=1;k<=2;k++){
            q.revolution=(uint8_t)r;q.extra.position=200000*k;
            mfm_id_t id={(uint8_t)(format==5?1:0),0,(uint8_t)k,(uint8_t)(format==4 || format==5 || format==7?1:2)};
            dec.cb(dec.ctx,&id,payload,true);
        }
        protection_finish(&q,&comparison);
        CHECK(state.state==PROTECT_NO_INDICATOR && !state.count,"ordinary sector evidence across decoder family %u",format);
    }
    init(PROTECT_LAYOUT);mfm_id_t large={0,0,1,4};
    dec.diagnostic(dec.ctx,&large,SF_HEADER_BAD|SF_LENGTH,SF_LENGTH,0,0,1);protection_finish(&q,NULL);
    CHECK(find(PR_HEADER_LENGTH) && find(PR_HEADER_LENGTH)->level==1 && state.insufficient==PROTECT_LAYOUT,"unsupported IBM length is a limited observation");
    init(PROTECT_LAYOUT);
    for(unsigned r=0;r<3;r++){q.revolution=(uint8_t)r;q.extra.position=200000;mfm_id_t side={0,1,1,2};dec.cb(dec.ctx,&side,payload,true);}
    protection_finish(&q,NULL);CHECK(find(PR_HEADER_POSITION) && find(PR_HEADER_POSITION)->value==1 && !find(PR_HEADER_POSITION)->reference,"wrong side reported with expected C/H pair");
    init(PROTECT_TIMING);q.max_gap[0]=200000;protection_finish(&q,NULL);
    CHECK(find(PR_LONG_GAP) && find(PR_LONG_GAP)->level==1 && (find(PR_LONG_GAP)->qualifiers&2),"long gap alone is localized only as a track observation");
    CHECK_SUMMARY("test_protection");
}
