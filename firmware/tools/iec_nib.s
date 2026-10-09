; UFI 1541 drive code: raw GCR track chunks over the serial bus (firmware 1.14)
;
; Uploaded with M-W to $0310, started with M-E $0310.  Runs with the DOS suspended (SEI)
; and talks to VIA2 ($1C00) directly, like the DOS job code and nibtools:
;   $1C00 bit0-1 stepper phase, bit2 motor, bit3 LED, bit5-6 density, bit7 SYNC (0 = active)
;   $1C01 read data, byte-ready sets the V flag (SO input)
; Parameters / results at $0300-$030F, code $0310-$05FF, data buffer $0600-$07FF (512 bytes;
; $0700 is the DOS BAM buffer - the host sends "I0" afterwards).
;
; Mode 0: step to P_TRACK, find the track origin (the sync after the longest non-sync stretch,
;         i.e. the header sync of sector 0 on a formatted track), skip P_SYNC syncs, record
;         the sync length and read 512 bytes after it.  Status 0.
; Mode 1: step, then read 512 raw bytes without any reference (unformatted tracks).  Status 3.
; Mode 2: step only.  Status 0.
; Status 1 = no sync / no bytes (timeout), 2 = origin not found.

P_TRACK   = $0300        ; target half track (2 = track 1 ... 82 = track 41)
P_SYNC    = $0301        ; sync index from the origin
P_MODE    = $0302
P_DIR     = $0303        ; phase delta for a step toward higher tracks (1 or $FF)
R_STATUS  = $0304
R_SYNCLEN = $0305        ; sync length in ~11 us units (255 = longer)
R_MAXLO   = $0306        ; longest non-sync stretch in bytes (diagnostic)
R_MAXHI   = $0307
V_CUR     = $0308        ; current half track, set by the host after a DOS seek (0 = unknown)
V_CNT     = $0309        ; 16 bit
V_MAX     = $030B        ; 16 bit
V_THR     = $030D        ; 16 bit
V_TMP     = $030F
VIA_PB    = $1C00
VIA_PA    = $1C01
VIA_DDRA  = $1C03
VIA_PCR   = $1C0C
BUF0      = $0600
BUF1      = $0700

        .org $0310
ENTRY:
        SEI
        LDA #0
        STA R_STATUS         ; results of the previous run must not survive
        STA R_SYNCLEN
        LDA VIA_PB
        AND #$04
        BNE MOTORON
        LDA VIA_PB
        ORA #$0C             ; motor + LED on
        STA VIA_PB
        LDA #255
        JSR DELAY            ; spin-up ~650 ms
        LDA #255
        JSR DELAY
        JMP SETUP
MOTORON:
        LDA VIA_PB
        ORA #$0C
        STA VIA_PB
SETUP:
        LDA #$EE
        STA VIA_PCR          ; read mode
        LDA #$00
        STA VIA_DDRA         ; port A input
        JSR STEP
        JSR DENSITY
        LDA #24
        JSR DELAY            ; head settle ~30 ms
        LDA P_MODE
        CMP #2
        BEQ STEPONLY
        CMP #1
        BEQ RAWREAD
        JSR SCAN
        LDA R_STATUS
        BNE EXIT
        JSR FIND
        LDA R_STATUS
        BNE EXIT
        JSR SYNCLEN
        JSR READ512
        JMP EXIT
STEPONLY:
        LDA #0
        STA R_STATUS
        JMP EXIT
RAWREAD:
        CLV
        JSR READ512
        LDA R_STATUS
        BNE EXIT
        LDA #3
        STA R_STATUS
EXIT:
        LDA VIA_PB
        AND #$F7             ; LED off
        STA VIA_PB
        CLI
        RTS

; ---- delay: A x ~1.28 ms ----
DELAY:
        TAX
D1:     LDY #0
D2:     DEY
        BNE D2
        DEX
        BNE D1
        RTS

; ---- head: half steps from V_CUR to P_TRACK ----
STEP:
        LDA V_CUR
        BEQ STEPEND          ; position unknown: the host calibrates first
S1:     LDA P_TRACK
        CMP V_CUR
        BEQ STEPEND
        BCC SOUT
        LDA P_DIR
        JSR PHASE
        INC V_CUR
        JMP S1
SOUT:   LDA #0
        SEC
        SBC P_DIR
        JSR PHASE
        DEC V_CUR
        JMP S1
STEPEND:
        RTS
PHASE:                       ; A = phase delta
        STA V_TMP
        LDA VIA_PB
        AND #$03
        CLC
        ADC V_TMP
        AND #$03
        STA V_CNT
        LDA VIA_PB
        AND #$FC
        ORA V_CNT
        STA VIA_PB
        LDA #6
        JSR DELAY            ; ~7.7 ms per half step
        RTS

; ---- density bits for the current track ----
DENSITY:
        LDA V_CUR
        LSR                  ; track = half track / 2
        TAX
        LDA #$60             ; tracks 1-17
        CPX #18
        BCC DSET
        LDA #$40             ; 18-24
        CPX #25
        BCC DSET
        LDA #$20             ; 25-30
        CPX #31
        BCC DSET
        LDA #$00             ; 31+
DSET:   STA V_TMP
        LDA VIA_PB
        AND #$9F
        ORA V_TMP
        STA VIA_PB
        RTS

; ---- wait for sync start (bit7 = 0) / sync end (bit7 = 1); carry set = timeout ~1 s ----
WAITSYNC:
        LDA #0
        STA V_CNT
        STA V_CNT+1
W1:     LDA VIA_PB
        BPL WOK
        INC V_CNT
        BNE W1
        INC V_CNT+1
        BNE W1
        SEC
        RTS
WOK:    CLC
        RTS
WAITEND:
        LDA #0
        STA V_CNT
        STA V_CNT+1
W2:     LDA VIA_PB
        BMI WOK
        INC V_CNT
        BNE W2
        INC V_CNT+1
        BNE W2
        SEC
        RTS

; ---- stretch: sync is active on entry; count bytes after it up to the next sync ----
STRETCH:
        JSR WAITEND
        BCS STTO
        LDA #0
        STA V_CNT
        STA V_CNT+1
        CLV
ST1:    LDA VIA_PB
        BPL STDONE
        BVC ST1
        CLV
        INC V_CNT
        BNE ST1
        INC V_CNT+1
        LDA V_CNT+1
        CMP #$20             ; > 8191 bytes without a sync: unformatted
        BCC ST1
STTO:   SEC
        RTS
STDONE: CLC
        RTS

; ---- scan 48 syncs (> 1 revolution): longest stretch -> V_MAX, threshold -> V_THR ----
SCAN:
        LDA #0
        STA V_MAX
        STA V_MAX+1
        JSR WAITSYNC
        BCS SCTO
        LDX #48
SC1:    JSR STRETCH
        BCS SCTO
        LDA V_CNT+1
        CMP V_MAX+1
        BCC SCNEXT
        BNE SCSET
        LDA V_CNT
        CMP V_MAX
        BCC SCNEXT
SCSET:  LDA V_CNT
        STA V_MAX
        LDA V_CNT+1
        STA V_MAX+1
SCNEXT: DEX
        BNE SC1
        LDA V_MAX
        STA R_MAXLO
        LDA V_MAX+1
        STA R_MAXHI
        SEC
        LDA V_MAX
        SBC #48
        STA V_THR
        LDA V_MAX+1
        SBC #0
        STA V_THR+1
        RTS
SCTO:   LDA #1
        STA R_STATUS
        RTS

; ---- find the origin (stretch >= threshold), then skip P_SYNC syncs; sync active on return ----
FIND:
        JSR WAITSYNC
        BCS FTO
        LDX #64
F1:     JSR STRETCH
        BCS FTO
        LDA V_CNT+1
        CMP V_THR+1
        BCC FNEXT
        BNE FFOUND
        LDA V_CNT
        CMP V_THR
        BCC FNEXT
FFOUND: LDX P_SYNC
        BEQ FOK
F2:     JSR WAITEND
        BCS FTO
        JSR WAITSYNC
        BCS FTO
        DEX
        BNE F2
FOK:    RTS
FNEXT:  DEX
        BNE F1
        LDA #2
        STA R_STATUS
        RTS
FTO:    LDA #1
        STA R_STATUS
        RTS

; ---- sync length (sync active on entry), returns right after the sync ends ----
SYNCLEN:
        LDY #0
SL1:    INY
        BEQ SLCAP
        LDA VIA_PB
        BPL SL1
        CLV                  ; right after the sync end: the first byte is ready ~26 us later
        STY R_SYNCLEN        ; (counted from the detection of the sync start, ~2 bytes late)
        RTS
SLCAP:  LDY #255
        STY R_SYNCLEN
        JSR WAITEND
        CLV
        RTS

; ---- 512 bytes: byte-ready via V; during a sync (no byte-ready) one $FF per ~byte time,
;      so sync marks appear as $FF runs like in NIB/G64 images; ~3.6 ms timeout per byte ----
READ512:                     ; V cleared by the caller (SYNCLEN / RAWREAD)
        LDY #0
        LDX #0
R1:     BVS R1B
        LDA VIA_PB
        BMI R1W              ; no sync: keep waiting
        LDA #$FF             ; sync active: synthesize a byte
        STA BUF0,Y
        LDX #1
R1D:    DEX
        BNE R1D
        JMP R1N
R1W:    DEX
        BNE R1
        BEQ RTO
R1B:    LDA VIA_PA
        CLV
        STA BUF0,Y
R1N:    LDX #0
        INY
        BNE R1
R2:     BVS R2B
        LDA VIA_PB
        BMI R2W
        LDA #$FF
        STA BUF1,Y
        LDX #1
R2D:    DEX
        BNE R2D
        JMP R2N
R2W:    DEX
        BNE R2
        BEQ RTO
R2B:    LDA VIA_PA
        CLV
        STA BUF1,Y
R2N:    LDX #0
        INY
        BNE R2
        LDA #0
        STA R_STATUS
        RTS
RTO:    LDA #1
        STA R_STATUS
        RTS
