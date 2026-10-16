#include "ufi_hfe_v3.h"
#include <string.h>
static uint8_t reverse(uint8_t x){
    x=(uint8_t)((x>>4)|(x<<4));x=(uint8_t)(((x&0xcc)>>2)|((x&0x33)<<2));
    return (uint8_t)(((x&0xaa)>>1)|((x&0x55)<<1));
}
typedef struct {
    hfe_v3_read_t read;void* ctx;uint32_t len,pos;
    uint8_t rate,skip,value,remaining;
    bool weak,index;
} cursor_t;
static int byte(cursor_t* c,uint8_t* x){
    if(c->pos>=c->len)return -16;
    if(!c->read(c->ctx,c->pos++,x))return -13;
    *x=reverse(*x);return 0;
}
/* One cell, index marker, EOF, or negative error. */
static int next(cursor_t* c){
    c->index=false;
    while(!c->remaining){
        if(c->pos==c->len)return 0;
        uint8_t x;int e=byte(c,&x);if(e)return e;
        if(x==0xf0)continue;
        if(x==0xf1){c->index=true;return 2;}
        if(x==0xf2){e=byte(c,&c->rate);if(e)return e;if(!c->rate)return -16;continue;}
        c->skip=0;
        if(x==0xf3){
            e=byte(c,&c->skip);if(e)return e;
            if(!c->skip || c->skip>7)return -16;
            e=byte(c,&x);if(e)return e;
            if(x!=0xf4 && x>=(1u<<(8-c->skip)))return -16;
        } else if(x>=0xf5)return -16;
        c->weak=x==0xf4;c->value=c->weak?0:x;c->remaining=(uint8_t)(8-c->skip);
    }
    c->remaining--;return 1;
}
static uint32_t ticks(uint64_t units){return (uint32_t)((units*275u+18u)/36u);}
int hfe_v3_decode(hfe_v3_read_t read,void* ctx,uint32_t length,
                  uint32_t* out,uint32_t capacity,hfe_v3_report_t* r){
    if(!r)return -16;
    memset(r,0,sizeof(*r));
    if(!read || !length || length>32767)return -16;
    cursor_t c={.read=read,.ctx=ctx,.len=length};int n;
    while((n=next(&c))>0){
        if(n==2){r->index_count++;r->index_cell=r->cells;}
        else {r->cells++;if(c.weak)r->weak_cells++;}
    }
    if(n<0)return n;
    if(!r->cells || !c.rate)return -16;
    uint8_t initial=c.rate;uint64_t duration=0;
    c=(cursor_t){.read=read,.ctx=ctx,.len=length,.rate=initial};
    while((n=next(&c))>0)if(n==1)duration+=c.rate;
    if(n<0)return n;
    if((duration*275u+18u)/36u>UINT32_MAX)return -18;
    r->ticks=ticks(duration);
    if(!out)return 0;
    if(r->weak_cells || r->index_count>1)return -11;
    uint32_t start=r->index_count?r->index_cell:0,previous=0;
    uint64_t elapsed=0;
    for(unsigned pass=0;pass<2;pass++){
        uint32_t cell=0;
        c=(cursor_t){.read=read,.ctx=ctx,.len=length,.rate=initial};
        while((n=next(&c))>0){
            if(n!=1)continue;
            bool emit=pass==0?cell>=start:cell<start;cell++;
            if(!emit)continue;
            elapsed+=c.rate;
            if((c.value>>c.remaining)&1u){
                uint32_t now=ticks(elapsed),delta=now-previous;
                if(delta<220)return -18;
                if(r->flux_count>=capacity)return -10;
                out[r->flux_count++]=delta;previous=now;
            }
        }
        if(n<0)return n;
    }
    return r->flux_count>=3?0:-16;
}
static bool emit(hfe_v3_encoder_t* e,uint8_t value){
    if(e->failed || e->length>=e->capacity){e->failed=true;return false;}
    e->out[e->length++]=reverse(value);return true;
}
static bool flush_bits(hfe_v3_encoder_t* e){
    if(!e->bits)return !e->failed;
    bool ok;
    if(e->bits==8 && e->value>=0xf0){
        ok=emit(e,0xf3)&&emit(e,1)&&emit(e,(uint8_t)(e->value>>1))&&
           emit(e,0xf3)&&emit(e,7)&&emit(e,(uint8_t)(e->value&1));
    }else{
        ok=(e->bits==8 || (emit(e,0xf3)&&emit(e,(uint8_t)(8-e->bits))))&&emit(e,e->value);
    }
    e->bits=e->value=0;return ok;
}
bool hfe_v3_encode_begin(hfe_v3_encoder_t* e,uint8_t* out,uint32_t capacity,uint8_t period){
    memset(e,0,sizeof(*e));e->out=out;e->capacity=capacity;
    if(!out || !period || capacity>32767){e->failed=true;return false;}
    return emit(e,0xf2)&&emit(e,period)&&emit(e,0xf1);
}
bool hfe_v3_encode_cells(hfe_v3_encoder_t* e,uint32_t cells,bool transition){
    if(!cells || cells>262136u || e->failed)return false;
    for(uint32_t i=0;i<cells;i++){
        e->value=(uint8_t)((e->value<<1)|(transition && i+1==cells));e->bits++;
        if(e->bits==8 && !flush_bits(e))return false;
    }
    return true;
}
bool hfe_v3_encode_end(hfe_v3_encoder_t* e){return flush_bits(e);}
