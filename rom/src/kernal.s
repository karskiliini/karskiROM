; kernal.s — karskiROM custom C64 KERNAL ROM
;
; Base: Original KERNAL 901227-03
; Assembler: ca65 (cc65 toolchain)
;
; Structure:
;   $E000-$ECFF  Unmodified (screen editor, keyboard, RS-232, etc.)
;   $ED00-$ED10  IEC setup tail (binary, bridges to disassembled section)
;   $ED11-$EEB3  IEC serial bus protocol (disassembled, will be modified)
;   $EEB4-$F49D  Unmodified (delay, tape, I/O management, etc.)
;   $F49E-$F5DC  LOAD and SAVE (binary for now, may modify later)
;   $F5DD-$FF80  Unmodified (misc routines, init, NMI/IRQ)
;   $FF81-$FFF5  KERNAL jump table
;   $FFFA-$FFFF  Hardware vectors (NMI, RESET, IRQ)

.include "defs.inc"

; Forward references to routines in binary-included sections.
; These are used by the disassembled IEC code but live outside
; the disassembled range.
iec_or_status    = $FE1C    ; OR A into ST ($90); in KERNAL $FE1C
iec_find_file    = $F070    ; Find logical file
iec_find_device  = $F0A4    ; Find device in file tables

.segment "CODE"

; ============================================================
; $E000-$ED10: Unmodified KERNAL (screen editor through IEC setup)
; ============================================================
.incbin "../original/kernal-901227-03.bin", $0000, $0D11

; ============================================================
; $ED11-$EEB3: IEC Serial Bus Protocol
;
; These routines handle all communication over the IEC serial bus.
; This is the section we modify for JiffyDOS fast transfer support.
; ============================================================

; ---- Send byte under ATN (command byte) ----
; Entry: A = command byte on stack (pushed by caller)
; Used by LISTEN, TALK, SECOND, OPEN, CLOSE, UNTALK, UNLISTEN
;
iec_send_atn_byte:                      ; $ED11
        pha                             ; save command byte
        bit     C3PO                    ; deferred byte pending?
        bpl     @no_deferred            ; no, skip
        sec
        ror     BTEFNR2                 ; set EOI flag for deferred byte
        jsr     iec_send_atn_main       ; send the deferred byte first
        lsr     C3PO                    ; clear deferred flag
        lsr     BTEFNR2                 ; clear EOI flag
@no_deferred:                           ; $ED20
        pla                             ; restore command byte
        sta     BTEFNR                  ; store as byte to send
        sei
        jsr     iec_release_data        ; release DATA line
        cmp     #IEC_UNLISTEN           ; is it UNLISTEN ($3F)?
        bne     @assert_atn             ; no, skip CLK release
        jsr     iec_release_clk         ; yes, release CLK first (UNLISTEN special case)
@assert_atn:                            ; $ED2E
        lda     CIA2_PRA
        ora     #IEC_ATN_OUT            ; assert ATN
        sta     CIA2_PRA

; ---- ATN byte send: assert CLK, release DATA, delay ----
iec_atn_send_setup:                     ; $ED36
        sei
        jsr     iec_assert_clk          ; assert CLK (not ready)
        jsr     iec_release_data        ; release DATA
        jsr     iec_delay_1ms           ; ~1ms delay for device response

; ---- Send byte on serial bus (main send routine) ----
; Entry: BTEFNR = byte to send, BTEFNR2 bit 7 = EOI flag
;
iec_send_atn_main:                      ; $ED40
        sei
        jsr     iec_release_data        ; release DATA
        jsr     iec_debounce            ; read bus (C=DATA_IN)
        bcs     iec_timeout_error       ; DATA_IN=1 (no device ack) → timeout

iec_clk_release_for_byte:               ; $ED49
        jsr     iec_release_clk         ; release CLK = "ready to send"

iec_check_eoi_flag:                     ; $ED4C
        bit     BTEFNR2                 ; EOI flag set?
        bpl     iec_wait_listener_ready ; no, skip EOI handshake

; EOI signaling: wait for listener DATA pulse (ack)
iec_eoi_wait_data_hi:                   ; $ED50
        jsr     iec_debounce
        bcc     iec_eoi_wait_data_hi    ; loop while DATA_IN=0

iec_eoi_wait_data_lo:                   ; $ED55
        jsr     iec_debounce
        bcs     iec_eoi_wait_data_lo    ; loop while DATA_IN=1

; Wait for listener to release DATA = "ready to receive"
iec_wait_listener_ready:                ; $ED5A
        jsr     iec_debounce
        bcc     iec_wait_listener_ready ; loop while DATA_IN=0

; ---- Bit send loop: 8 bits, LSB first ----
iec_send_bit_setup:                     ; $ED5F
        jsr     iec_assert_clk          ; CLK low = setting up bit
        lda     #$08
        sta     CNTDN                   ; bit counter = 8

iec_send_bit_debounce:                  ; $ED66
        lda     CIA2_PRA                ; debounce the bus read
        cmp     CIA2_PRA
        bne     iec_send_bit_debounce
        asl     a                       ; C=DATA_IN (listener still there?)

iec_send_bit_check:                     ; $ED6F
        bcc     iec_framing_error       ; DATA_IN=0 → listener asserted = error
        ror     BTEFNR                  ; shift next bit into carry
        bcs     @bit_is_one
        jsr     iec_assert_data         ; bit=0: assert DATA
        bne     @send_clk_pulse         ; (always taken)
@bit_is_one:                            ; $ED7A
        jsr     iec_release_data        ; bit=1: release DATA
@send_clk_pulse:                        ; $ED7D
        jsr     iec_release_clk         ; CLK high = bit is valid
        nop                             ; timing: hold CLK high
        nop
        nop
        nop

; Re-assert CLK and check for more bits
iec_send_bit_finish:                    ; $ED84
        lda     CIA2_PRA
        and     #<~IEC_DATA_OUT         ; clear DATA out bit
        ora     #IEC_CLK_OUT            ; set CLK out bit (assert CLK)
        sta     CIA2_PRA
        dec     CNTDN
        bne     iec_send_bit_debounce   ; more bits → loop

; ---- Wait for listener byte ACK (DATA low) ----
        lda     #$04                    ; Timer B timeout value
        sta     CIA1_TBHI
        lda     #$19                    ; start one-shot timer
        sta     CIA1_CRB
        lda     CIA1_ICR                ; clear any pending interrupt
@wait_ack:
        lda     CIA1_ICR
        and     #$02                    ; Timer B expired?
        bne     iec_framing_error       ; yes → timeout/framing error
        jsr     iec_debounce
        bcs     @wait_ack              ; loop while DATA_IN=1 (no ack yet)
        cli
        rts                             ; success

; ---- Error handlers ----
iec_timeout_error:                      ; $EDAD
        lda     #$80                    ; timeout flag
        .byte   $2C                     ; BIT abs trick: skip next LDA
iec_framing_error:                      ; $EDB0
        lda     #$03                    ; framing error flag
iec_set_status_timeout:                 ; $EDB2
        jsr     iec_or_status           ; OR into status byte
        cli
        clc
        bcc     iec_release_atn_and_cleanup ; branch always → release ATN, CLK, DATA

; ---- SECOND / TKSA: send secondary address ----
; Entry: A = secondary address byte
;
iec_second:                             ; $EDB9
        sta     BTEFNR                  ; store byte to send
        jsr     iec_atn_send_setup      ; assert CLK, release DATA, delay

; ---- Release ATN ----
iec_release_atn:                        ; $EDBE
        lda     CIA2_PRA
        and     #<~IEC_ATN_OUT          ; clear ATN bit
        sta     CIA2_PRA
        rts

; ---- Talk turnaround: switch from listener to talker role ----
; After TALK+SECOND under ATN, C64 becomes listener.
; Entry: A = secondary address byte
;
iec_turnaround:                         ; $EDC7
        sta     BTEFNR
        jsr     iec_atn_send_setup      ; send under ATN
        sei
        jsr     iec_assert_data         ; assert DATA (listener present)
        jsr     iec_release_atn         ; release ATN
        jsr     iec_release_clk         ; release CLK
@wait_talker:
        jsr     iec_debounce            ; wait for device to assert CLK
        bmi     @wait_talker            ; loop while CLK_IN=1 (released)
        cli
        rts

; ---- CIOUT: buffered byte output to serial bus ----
; Entry: A = byte to send
;
iec_ciout:                              ; $EDDD
        bit     C3PO                    ; deferred byte pending?
        bmi     @send_deferred          ; yes → send it first
        sec
        ror     C3PO                    ; set deferred flag
        bne     @store_byte             ; (always taken)
@send_deferred:                         ; $EDE6
        pha
        jsr     iec_send_atn_main       ; send the deferred byte
        pla
@store_byte:                            ; $EDEB
        sta     BTEFNR                  ; store new byte as deferred
        clc
        rts

; ---- UNTALK ----
iec_untalk:                             ; $EDEF
        sei
iec_untalk_body:                        ; $EDF0
        jsr     iec_assert_clk          ; assert CLK
        lda     CIA2_PRA
        ora     #IEC_ATN_OUT            ; assert ATN
        sta     CIA2_PRA
        lda     #IEC_UNTALK             ; $5F
        .byte   $2C                     ; BIT abs trick: skip LDA #$3F

; ---- UNLISTEN ----
iec_unlisten:                           ; $EDFE
        lda     #IEC_UNLISTEN           ; $3F

iec_untk_unls_send:                     ; $EE00
        jsr     iec_send_atn_byte       ; send command under ATN
iec_release_atn_and_cleanup:            ; $EE03 — error handler branches here
        jsr     iec_release_atn         ; release ATN

iec_untk_unls_cleanup:                  ; $EE06
        txa                             ; save X
        ldx     #$0A                    ; delay counter
@delay: dex
        bne     @delay
        tax                             ; restore X
        jsr     iec_release_clk         ; release CLK
        jmp     iec_release_data        ; release DATA (and return)

; ============================================================
; ACPTR — Receive byte from serial bus (as listener)
; Returns: A = received byte, carry clear
; Status byte ($90) bit 6 set if EOI detected
; ============================================================
iec_acptr:                              ; $EE13
        sei
        lda     #$00
        sta     CNTDN                   ; clear bit counter / EOI flag
        jsr     iec_release_clk         ; release CLK (listener ready)

@wait_talker_ready:                     ; $EE1B
        jsr     iec_debounce            ; N=CLK_IN
        bpl     @wait_talker_ready      ; loop while CLK_IN=0 (asserted)

; Talker released CLK = ready to send. Start timer for EOI detection.
iec_acptr_timer_setup:                  ; $EE20
        lda     #$01
        sta     CIA1_TBHI               ; Timer B high = 1 (256 cycles)
        lda     #$19
        sta     CIA1_CRB                ; start one-shot Timer B

iec_acptr_release_data:                 ; $EE2A
        jsr     iec_release_data        ; release DATA = "ready to receive"

iec_acptr_timer_check1:                 ; $EE2D
        lda     CIA1_ICR                ; clear pending flags

iec_acptr_timer_loop:                   ; $EE30
        lda     CIA1_ICR
        and     #$02                    ; Timer B expired?
        bne     iec_acptr_eoi           ; yes → EOI detected
        jsr     iec_debounce            ; N=CLK_IN
        bmi     iec_acptr_timer_loop    ; CLK_IN=1 (released) → keep waiting
        bpl     iec_acptr_receive_byte  ; CLK_IN=0 (asserted) → bit transfer

; ---- EOI handling ----
iec_acptr_eoi:                          ; $EE3E
        lda     CNTDN
        beq     @first_eoi              ; first time → acknowledge
        lda     #$02                    ; second time → timeout error
        jmp     iec_set_status_timeout

@first_eoi:                             ; $EE47
        jsr     iec_assert_data         ; pulse DATA low (EOI ack)
        jsr     iec_release_clk         ; release CLK
        lda     #$40
        jsr     iec_or_status           ; set status bit 6 = EOI
        inc     CNTDN                   ; mark EOI detected
        bne     iec_acptr_timer_setup   ; always taken → retry with timer

; ---- Receive 8 bits, LSB first ----
iec_acptr_receive_byte:                 ; $EE56
        lda     #$08
        sta     CNTDN                   ; 8 bits to receive

@wait_clk_hi:                           ; $EE5A
        lda     CIA2_PRA                ; debounce read
        cmp     CIA2_PRA
        bne     @wait_clk_hi
        asl     a                       ; N=CLK_IN, C=DATA_IN
        bpl     @wait_clk_hi           ; loop while CLK_IN=0 (bit not ready)
        ror     BUFPNT                  ; shift DATA_IN (carry) into receive buffer

@wait_clk_lo:                           ; $EE67
        lda     CIA2_PRA                ; debounce read
        cmp     CIA2_PRA
        bne     @wait_clk_lo
        asl     a                       ; N=CLK_IN
        bmi     @wait_clk_lo           ; loop while CLK_IN=1 (wait for next bit)

        dec     CNTDN
        bne     @wait_clk_hi           ; more bits → loop

; ---- Byte received: acknowledge ----
iec_acptr_byte_done:                    ; $EE76
        jsr     iec_assert_data         ; pull DATA low = byte ACK
        bit     ST                      ; check EOI flag (bit 6 → V)
        bvc     @no_eoi_cleanup
        jsr     iec_untk_unls_cleanup   ; EOI: release CLK + DATA
@no_eoi_cleanup:                        ; $EE80
        lda     BUFPNT                  ; load received byte
        cli
        clc
        rts

; ============================================================
; IEC bus line manipulation subroutines
; These are the low-level primitives used by all IEC routines.
; ============================================================

; Release CLK output (bit 4 of CIA2 PRA)
iec_release_clk:                        ; $EE85
        lda     CIA2_PRA
        and     #<~IEC_CLK_OUT
        sta     CIA2_PRA
        rts

; Assert CLK output (drive LOW)
iec_assert_clk:                         ; $EE8E
        lda     CIA2_PRA
        ora     #IEC_CLK_OUT
        sta     CIA2_PRA
        rts

; Release DATA output (bit 5 of CIA2 PRA)
iec_release_data:                       ; $EE97
        lda     CIA2_PRA
        and     #<~IEC_DATA_OUT
        sta     CIA2_PRA
        rts

; Assert DATA output (drive LOW)
iec_assert_data:                        ; $EEA0
        lda     CIA2_PRA
        ora     #IEC_DATA_OUT
        sta     CIA2_PRA
        rts

; Debounce bus read: read CIA2 PRA twice, retry if mismatch.
; Returns: N = CLK_IN (1=released, 0=asserted)
;          C = DATA_IN (1=released, 0=asserted)
; (After ASL: original bit 7 → C, original bit 6 → N)
iec_debounce:                           ; $EEA9
        lda     CIA2_PRA
        cmp     CIA2_PRA
        bne     iec_debounce
        asl     a
        rts

; ============================================================
; $EEB3: ~1ms delay loop (no bus accesses)
; ============================================================
iec_delay_1ms:                          ; $EEB3
        txa

; ============================================================
; $EEB4-$FFFF: Rest of KERNAL (unmodified)
; ============================================================
.incbin "../original/kernal-901227-03.bin", $0EB4

; End of KERNAL ROM ($E000-$FFFF = 8192 bytes)
