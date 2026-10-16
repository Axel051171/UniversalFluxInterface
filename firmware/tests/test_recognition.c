#include "ufi_quality.h"
#include <assert.h>
#include <stdio.h>
static quality_t q;
int main(void){
    uint32_t valid,mismatch,flags;
    for(unsigned f=1;f<=6;f++){
        quality_init(&q,(uint8_t)f,250000,1);q.extra.completed=1;q.n=2;
        q.sectors[0].id=(mfm_id_t){12,1,1,2};q.sectors[1].id=(mfm_id_t){12,1,2,2};
        q.sectors[0].seen=q.sectors[1].seen=1;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);
        assert(valid==2 && !mismatch && (flags&RECOG_SECTOR_SET_CONFIRMED));
        assert((flags&RECOG_GEOMETRY_UNCONFIRMED) && !(flags&RECOG_CONTENT_CONFIRMED));
        flags=quality_recognition(&q,14,12,1,2,true,&valid,&mismatch);
        assert(!(flags&RECOG_SECTOR_SET_CONFIRMED));
        flags=quality_recognition(&q,0,0,0,0,false,&valid,&mismatch);
        assert((flags&RECOG_FORMAT_OBSERVED) && !(flags&(RECOG_POSITION_CHECKED|RECOG_HEADER_POSITION_MISMATCH)));
        flags=quality_recognition(&q,0,11,1,2,true,&valid,&mismatch);
        assert(mismatch==2 && (flags&RECOG_HEADER_POSITION_MISMATCH));
        q.sectors[0].bad=1;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);assert(!(flags&RECOG_SECTOR_SET_CONFIRMED));
        q.sectors[0].bad=0;q.sectors[0].flags=1;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);assert(!(flags&RECOG_SECTOR_SET_CONFIRMED));
        q.sectors[0].flags=0;q.ambiguous=1;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);assert((flags&RECOG_AMBIGUOUS) && !(flags&4));
        q.ambiguous=0;q.attempts[0][0].header_errors=1;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);assert(!(flags&4));
        q.attempts[0][0].header_errors=0;q.extra.completed=0;
        flags=quality_recognition(&q,6,12,1,2,true,&valid,&mismatch);assert((flags&RECOG_INCOMPLETE) && !(flags&4));
    }
    flags=quality_recognition(NULL,6,12,1,2,true,&valid,&mismatch);
    assert(!valid && !mismatch && (flags&RECOG_INCOMPLETE) && !(flags&RECOG_FORMAT_OBSERVED));
    puts("recognition: six formats, exact sector sets, position mismatch, ambiguity, CRC/conflicts and incomplete capture passed");
}
