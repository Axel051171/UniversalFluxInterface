static bool old_analysis_changes(const uint8_t* p, uint16_t len)
{
    if (!len) {
        return true;                    /* no op byte: the parser refuses it */
    }
    switch (p[0]) {
        case 1: case 4: case 5: case 16:        /* job start, abort, self-test start, media notify */
        case 24: case 26: case 27: case 29:     /* compare-and-start, plan create, plan step */
        case 52:                                /* re-analysis of the stored capture: starts a job */
            return true;
        case 7: case 10: case 11: case 14: case 15: case 18:
        case 30: case 31: case 33: case 34: case 36:
        case 37:                                /* session write lock */
        case 41: case 45:                       /* speed check tolerance, precompensation profile */
        case 46:                                /* track survey start / abort */
        case 50: case 55:                       /* precision and protection options */
            return len > 1u;
        default:
            return false;
    }
}

static bool old_request_readonly(uint8_t cmd, const uint8_t* p, uint16_t len)
{
    switch (cmd) {
        case V2_PING: case V2_INFO: case V2_STATUS: case V2_DESCRIBE: case V2_FEATURES:
        case V2_DIAGNOSTICS: case V2_WRITE_LIMITS: case V2_EVENTS: case V2_DUMP_STATUS:
        case V2_RESOURCE: case V2_QUALITY: case V2_NET: case V2_AUTH:
            return true;
        case V2_JOB:
            return len == 0;
        case V2_ANALYSIS_JOB:
            return !old_analysis_changes(p, len);
        default:
            return false;
    }
}

static bool old_allowed_during_dump(uint8_t cmd)
{
    switch (cmd) {
        case V2_PING: case V2_INFO: case V2_STATUS: case V2_EVENTS: case V2_POWER:
        case V2_USB_POWER: case V2_SD_INFO: case V2_DUMP_STATUS: case V2_DUMP_ABORT: case V2_RTC: case V2_SYNC:
        case V2_RAILS:
        case V2_INDEX_SIM: case V2_WRITE_LIMITS: case V2_DESCRIBE: case V2_RESOURCE: case V2_JOB: case V2_DIAGNOSTICS: case V2_FEATURES:
        case V2_NET: case V2_AUTH:
            return true;
        default:
            return false;
    }
}

static bool old_uses_drive(uint8_t cmd)
{
    switch (cmd) {
        case V2_PING: case V2_INFO: case V2_STATUS: case V2_EVENTS: case V2_WRITE_LIMITS: case V2_DESCRIBE: case V2_RESOURCE: case V2_JOB: case V2_DIAGNOSTICS: case V2_FEATURES:
        case V2_ABORT: case V2_DUMP_STATUS: case V2_DUMP_ABORT: case V2_QUALITY: case V2_ANALYSIS_JOB:
        case V2_RTC: case V2_SYNC: case V2_RAILS:   /* A10: board sensing, no drive involved */
        case V2_NET: case V2_AUTH:                  /* L4/L5: LAN status and challenge */
            return false;
        default: return true; /* includes IEC, file I/O and mode/power changes */
    }
}

static bool old_locked_write(uint8_t cmd, const uint8_t* p, uint16_t len)
{
    switch (cmd) {
        case UFI_V2_WRITE: case V2_GUARDED_WRITE: case V2_ERASE: case V2_PATTERN:
        case V2_FORMAT: case V2_COPY_START: case V2_WRITE_START:
            return true;
        case V2_IEC_BLOCK:                  /* op 1 = U2 block write */
            return len >= 1u && p[0] == 1u;
        case V2_IEC_MEM:                    /* M-W, M-E, DOS command */
            return len >= 1u && (p[0] == 0u || p[0] == 2u || p[0] == 3u);
        case V2_IEC_TARGET:                 /* schema, device, then the MEM payload */
            return len >= 3u && (p[2] == 0u || p[2] == 2u || p[2] == 3u);
        default:
            return false;
    }
}

static int old_lan_refusal(uint8_t cmd, const uint8_t* p, uint16_t len)
{
    if (cmd == V2_BOOTLOADER || cmd == V2_USB_MODE) {
        return UFI_ERR_LAN_DENIED;
    }
    if (cmd == V2_READ_FILE && len > 6u && is_lan_cfg(&p[6], (uint16_t)(len - 6u))) {
        return UFI_ERR_LAN_DENIED;
    }
    if ((old_locked_write(cmd, p, len) || cmd == V2_WRITE_FILE || cmd == V2_DELETE || cmd == V2_IEC_SEND) &&
        !ufi_lan_write_allowed()) {
        return UFI_ERR_WRITE_PROT;
    }
    return 0;
}
