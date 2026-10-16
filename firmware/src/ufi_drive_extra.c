#include "ufi_drive_extra.h"
bool drive_extra_valid(const drive_extra_t* t){
    return t && t->density_us<=60000 && t->motor_step_ms<=2000 && t->write_settle_us<=60000 &&
        t->gate_lead_us<=200 && t->gate_tail_us<=200 && !t->reserved;
}
