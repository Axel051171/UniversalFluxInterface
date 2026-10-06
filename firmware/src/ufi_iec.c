/**
 * UFI Flux Engine - IEC Bus Module
 *
 * Commodore IEC Serial Bus Protokoll für 1541/1571/1581 Laufwerke.
 * UFI ist immer Controller; Timing nach "How the VIC/64 Serial Bus Works"
 * (J. Butterfield): Bits LSB first, Bit 1 = DATA released.
 */

#include "ufi_firmware.h"
#include <stdio.h>
#include <string.h>

/* Board: each IEC line has a driver output (PD0-4 -> SN74LS07 open collector) and a
 * receiver input (PF4-8 <- 74LVC14A).  "pull" asserts the line via bus_out(), reads
 * use bus_in() (true = line low on the bus), see board.h. */

/* ============================================================================
 * IEC TIMING KONSTANTEN (in µs)
 * ============================================================================ */

#define IEC_T_AT        1000    // ATN / frame response limit (listener must answer)
#define IEC_T_S         70      // Bit Setup (talker, CLK asserted)
#define IEC_T_V         20      // Data Valid (talker, CLK released)
#define IEC_T_BB        100     // Between Bytes
#define IEC_T_EI        200     // EOI: talker hold-off before the listener acknowledges
#define IEC_T_EI_DETECT 250     // listener side: no CLK within this time = EOI
#define IEC_T_EI_ACK    60      // EOI acknowledge pulse
#define IEC_T_TALKER_US 100000  // talker may need long to fetch data (disk access)

/* ============================================================================
 * LOW LEVEL
 * ============================================================================ */

static inline void iec_delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

static inline void iec_release_clk(void)  { bus_out(&PIN_IEC_CLK_OUT, false); }
static inline void iec_pull_clk(void)     { bus_out(&PIN_IEC_CLK_OUT, true); }
static inline void iec_release_data(void) { bus_out(&PIN_IEC_DATA_OUT, false); }
static inline void iec_pull_data(void)    { bus_out(&PIN_IEC_DATA_OUT, true); }
static inline void iec_release_atn(void)  { bus_out(&PIN_IEC_ATN_OUT, false); }
static inline void iec_pull_atn(void)     { bus_out(&PIN_IEC_ATN_OUT, true); }

/* true = line held low by someone on the bus */
static inline bool iec_read_clk(void)     { return bus_in(&PIN_IEC_CLK_IN); }
static inline bool iec_read_data(void)    { return bus_in(&PIN_IEC_DATA_IN); }

/* Wait until a line reaches the given state; false on timeout */
static bool iec_wait(bool (*read)(void), bool asserted, uint32_t timeout_us) {
    const uint32_t start = DWT->CYCCNT;
    const uint32_t limit = timeout_us * (SystemCoreClock / 1000000);
    while (read() != asserted) {
        if ((DWT->CYCCNT - start) > limit) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_iec_init(void) {
    /* GPIO modes are set by board_gpio_init(); release every line */
    bus_out(&PIN_IEC_ATN_OUT, false);
    bus_out(&PIN_IEC_CLK_OUT, false);
    bus_out(&PIN_IEC_DATA_OUT, false);
    bus_out(&PIN_IEC_SRQ_OUT, false);
    bus_out(&PIN_IEC_RESET_OUT, false);
}

/* ============================================================================
 * BUS RESET
 * ============================================================================ */

int ufi_iec_reset(void) {
    bus_out(&PIN_IEC_RESET_OUT, true);      // RESET low for 20 ms
    HAL_Delay(20);
    bus_out(&PIN_IEC_RESET_OUT, false);

    HAL_Delay(500);                         // 1541 boot time

    iec_release_clk();
    iec_release_data();
    iec_release_atn();
    return UFI_OK;
}

/* ============================================================================
 * ATN STEUERUNG
 * ============================================================================ */

int ufi_iec_atn(bool active) {
    if (active) {
        iec_pull_atn();
        iec_pull_clk();                     // controller becomes talker under ATN
        iec_release_data();
    } else {
        iec_release_atn();
    }
    iec_delay_us(100);
    return UFI_OK;
}

/* ============================================================================
 * BYTE SENDEN (UFI = Talker)
 * ============================================================================ */

int ufi_iec_send_byte(uint8_t byte, bool eoi) {
    iec_pull_clk();
    iec_release_data();

    // 1. A listener must be present: it holds DATA asserted
    if (!iec_wait(iec_read_data, true, IEC_T_AT)) {
        return UFI_ERR_IEC_NRFD;
    }

    // 2. Ready to send: release CLK, wait until all listeners release DATA
    iec_release_clk();
    if (!iec_wait(iec_read_data, false, IEC_T_TALKER_US)) {
        return UFI_ERR_TIMEOUT;
    }

    // 3. EOI: hold off >200 us, listener acknowledges with a DATA pulse
    if (eoi) {
        if (!iec_wait(iec_read_data, true, IEC_T_EI + IEC_T_AT)) {
            return UFI_ERR_TIMEOUT;
        }
        if (!iec_wait(iec_read_data, false, IEC_T_AT)) {
            return UFI_ERR_TIMEOUT;
        }
    }

    // 4. 8 bits, LSB first: CLK asserted = setup, CLK released = bit valid
    for (int i = 0; i < 8; i++) {
        iec_pull_clk();
        if (byte & (1 << i)) {
            iec_release_data();             // 1 = released
        } else {
            iec_pull_data();                // 0 = asserted
        }
        iec_delay_us(IEC_T_S);
        iec_release_clk();
        iec_delay_us(IEC_T_V);
    }

    // 5. Frame end: CLK asserted, DATA released, listener acknowledges with DATA
    iec_pull_clk();
    iec_release_data();
    if (!iec_wait(iec_read_data, true, IEC_T_AT)) {
        return UFI_ERR_IEC_NOACK;
    }

    iec_delay_us(IEC_T_BB);
    return UFI_OK;
}

/* ============================================================================
 * BYTE EMPFANGEN (UFI = Listener, nach Turnaround)
 * ============================================================================ */

int ufi_iec_receive_byte(uint8_t* byte, bool* eoi) {
    *byte = 0;
    *eoi = false;

    // 1. Talker ready to send: CLK released
    if (!iec_wait(iec_read_clk, false, IEC_T_TALKER_US)) {
        return UFI_ERR_TIMEOUT;
    }

    // 2. Ready for data: release DATA
    iec_release_data();

    // 3. Talker asserts CLK within 200 us, otherwise this is the last byte (EOI)
    if (!iec_wait(iec_read_clk, true, IEC_T_EI_DETECT)) {
        *eoi = true;
        iec_pull_data();
        iec_delay_us(IEC_T_EI_ACK);
        iec_release_data();
        if (!iec_wait(iec_read_clk, true, IEC_T_AT)) {
            return UFI_ERR_TIMEOUT;
        }
    }

    // 4. 8 bits, LSB first: sample DATA while CLK is released
    for (int i = 0; i < 8; i++) {
        if (!iec_wait(iec_read_clk, false, IEC_T_AT)) {
            return UFI_ERR_TIMEOUT;
        }
        if (!iec_read_data()) {
            *byte |= (uint8_t)(1u << i);
        }
        if (!iec_wait(iec_read_clk, true, IEC_T_AT)) {
            return UFI_ERR_TIMEOUT;
        }
    }

    // 5. Frame acknowledge: assert DATA
    iec_pull_data();
    return UFI_OK;
}

/* Controller -> listener after TALK: hold DATA, release ATN + CLK, talker takes CLK */
static int iec_turnaround(void) {
    iec_pull_data();
    iec_release_atn();
    iec_release_clk();
    return iec_wait(iec_read_clk, true, IEC_T_AT) ? UFI_OK : UFI_ERR_TIMEOUT;
}

/* ============================================================================
 * LISTEN/TALK BEFEHLE
 * ============================================================================ */

int ufi_iec_listen(uint8_t device) {
    ufi_iec_atn(true);
    return ufi_iec_send_byte(0x20 | (device & 0x1F), false);
}

int ufi_iec_talk(uint8_t device) {
    ufi_iec_atn(true);
    return ufi_iec_send_byte(0x40 | (device & 0x1F), false);
}

int ufi_iec_secondary(uint8_t channel) {
    return ufi_iec_send_byte(0x60 | (channel & 0x0F), false);    // DATA / reopen
}

static int iec_open(uint8_t channel) {
    return ufi_iec_send_byte(0xF0 | (channel & 0x0F), false);    // OPEN
}

static int iec_close(uint8_t channel) {
    return ufi_iec_send_byte(0xE0 | (channel & 0x0F), false);    // CLOSE
}

int ufi_iec_unlisten(void) {
    ufi_iec_atn(true);
    int ret = ufi_iec_send_byte(0x3F, false);
    ufi_iec_atn(false);
    iec_release_clk();
    return ret;
}

int ufi_iec_untalk(void) {
    iec_release_data();                     // stop acknowledging as listener
    ufi_iec_atn(true);
    int ret = ufi_iec_send_byte(0x5F, false);
    ufi_iec_atn(false);
    iec_release_clk();
    return ret;
}

/* Send bytes to an already listening device, last one with EOI */
static int iec_send_bytes(const uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        int ret = ufi_iec_send_byte(data[i], i == len - 1);
        if (ret != UFI_OK) {
            return ret;
        }
    }
    return UFI_OK;
}

/* LISTEN device, OPEN channel with file name, UNLISTEN */
static int iec_open_file(uint8_t device, uint8_t channel, const char* name) {
    int ret = ufi_iec_listen(device);
    if (ret == UFI_OK) ret = iec_open(channel);
    if (ret == UFI_OK) {
        ufi_iec_atn(false);
        ret = iec_send_bytes((const uint8_t*)name, (uint16_t)strlen(name));
    }
    int ret2 = ufi_iec_unlisten();
    return (ret != UFI_OK) ? ret : ret2;
}

static int iec_close_file(uint8_t device, uint8_t channel) {
    int ret = ufi_iec_listen(device);
    if (ret == UFI_OK) ret = iec_close(channel);
    int ret2 = ufi_iec_unlisten();
    return (ret != UFI_OK) ? ret : ret2;
}

/* TALK device on channel and read until EOI or max_len; returns bytes read or error */
static int iec_read_channel(uint8_t device, uint8_t channel, uint8_t* buf, uint16_t max_len) {
    int ret = ufi_iec_talk(device);
    if (ret == UFI_OK) ret = ufi_iec_secondary(channel);
    if (ret == UFI_OK) ret = iec_turnaround();

    uint16_t n = 0;
    if (ret == UFI_OK) {
        bool eoi = false;
        while (!eoi && n < max_len) {
            ret = ufi_iec_receive_byte(&buf[n], &eoi);
            if (ret != UFI_OK) break;
            n++;
        }
    }
    int ret2 = ufi_iec_untalk();
    if (ret != UFI_OK) return ret;
    if (ret2 != UFI_OK) return ret2;
    return n;
}

/* ============================================================================
 * HIGH-LEVEL FUNKTIONEN
 * ============================================================================ */

// Befehl an Laufwerk senden (z.B. "I0" für Initialize), Kommandokanal 15
int ufi_iec_command(uint8_t device, const char* cmd, uint8_t len) {
    int ret = ufi_iec_listen(device);
    if (ret == UFI_OK) ret = ufi_iec_secondary(15);
    if (ret == UFI_OK) {
        ufi_iec_atn(false);
        ret = iec_send_bytes((const uint8_t*)cmd, len);
    }
    int ret2 = ufi_iec_unlisten();
    return (ret != UFI_OK) ? ret : ret2;
}

// Status vom Laufwerk lesen (Kommandokanal 15), nullterminiert
int ufi_iec_read_status(uint8_t device, char* buffer, uint8_t max_len) {
    if (max_len == 0) {
        return UFI_ERR_BUFFER_FULL;
    }
    int n = iec_read_channel(device, 15, (uint8_t*)buffer, max_len - 1);
    buffer[(n > 0) ? n : 0] = '\0';
    return n;
}

// Block lesen: Puffer-Kanal 2 per "#" öffnen, U1 lesen lassen, 256 Bytes holen
int ufi_iec_read_block(uint8_t device, uint8_t track, uint8_t sector,
                       uint8_t* buffer, uint16_t* len) {
    char cmd[20];
    *len = 0;

    int ret = iec_open_file(device, 2, "#");
    if (ret != UFI_OK) return ret;

    int n = snprintf(cmd, sizeof(cmd), "U1:2 0 %u %u", track, sector);
    ret = ufi_iec_command(device, cmd, (uint8_t)n);
    if (ret == UFI_OK) {
        int got = iec_read_channel(device, 2, buffer, 256);
        if (got < 0) {
            ret = got;
        } else {
            *len = (uint16_t)got;
        }
    }
    int ret2 = iec_close_file(device, 2);
    if (ret != UFI_OK) return ret;
    if (ret2 != UFI_OK) return ret2;
    return (*len == 256) ? UFI_OK : UFI_ERR_TIMEOUT;
}

// Directory lesen: "$" auf Kanal 0 öffnen, bis EOI lesen, schließen
int ufi_iec_read_directory(uint8_t device, uint8_t* buffer, uint16_t max_len,
                           uint16_t* len) {
    *len = 0;
    int ret = iec_open_file(device, 0, "$");
    if (ret != UFI_OK) return ret;

    int got = iec_read_channel(device, 0, buffer, max_len);
    if (got > 0) {
        *len = (uint16_t)got;
    }
    int ret2 = iec_close_file(device, 0);
    if (got < 0) return got;
    return ret2;
}
