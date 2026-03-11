; defs.inc — C64 KERNAL zero-page, I/O, and constant definitions
;
; References: "Mapping the Commodore 64" and KERNAL disassembly

; ---- Status and flags ----
ST          = $90       ; I/O status byte
VERCK       = $93       ; LOAD/VERIFY flag (0=LOAD, 1=VERIFY)
C3PO        = $94       ; Serial bus: byte deferred flag (bit 7)
BTEFNR      = $95       ; Serial bus: byte to send
BTEFNR2     = $A3       ; Serial bus: EOI flag / bit counter
CNTDN       = $A5       ; Serial bus: bit counter for serial I/O
BUFPNT      = $A4       ; Serial bus: received byte buffer

; ---- Device/file management ----
LA          = $B8       ; Current logical file number
SA          = $B9       ; Current secondary address
FA          = $BA       ; Current device number
FNADR       = $BB       ; Pointer to filename (lo/hi)
FNLEN       = $B7       ; Filename length
LDTND       = $98       ; Number of open files
DESSION     = $99       ; Current input device
MSESSION    = $9A       ; Current output device
MSGFLG      = $9D       ; System message flag

; ---- LOAD/SAVE addresses ----
STAL        = $C1       ; Start address (lo/hi) for LOAD/SAVE
STAH        = $C2
MEMUSS      = $C3       ; End address (lo/hi) for LOAD
EAL         = $AE       ; LOAD pointer (lo/hi) — current store address
EAH         = $AF

; ---- Keyboard ----
STKEY       = $91       ; STOP key flag ($FF = no key)

; ---- CIA2 (IEC serial bus I/O) ----
CIA2_PRA    = $DD00     ; Port A: IEC bus output + VIC bank
                        ;   bit 3 = ATN OUT  (1=assert)
                        ;   bit 4 = CLK OUT  (1=assert)
                        ;   bit 5 = DATA OUT (1=assert)
                        ;   bit 6 = CLK IN   (active low)
                        ;   bit 7 = DATA IN  (active low)

; ---- CIA1 (timers, keyboard) ----
CIA1_TBLO   = $DC06     ; Timer B latch low
CIA1_TBHI   = $DC07     ; Timer B latch high
CIA1_ICR    = $DC0D     ; Interrupt control register
CIA1_CRB    = $DC0F     ; Timer B control register

; ---- IEC bus bit masks for CIA2_PRA ----
IEC_ATN_OUT  = %00001000   ; bit 3
IEC_CLK_OUT  = %00010000   ; bit 4
IEC_DATA_OUT = %00100000   ; bit 5
IEC_CLK_IN   = %01000000   ; bit 6  (active low: 0=HIGH)
IEC_DATA_IN  = %10000000   ; bit 7  (active low: 0=HIGH)

; ---- IEC command bytes ----
IEC_LISTEN   = $20
IEC_TALK     = $40
IEC_UNLISTEN = $3F
IEC_UNTALK   = $5F

; ---- KERNAL vectors (RAM, set during init) ----
ILOAD       = $0330     ; LOAD vector (lo/hi)
ISAVE       = $0332     ; SAVE vector (lo/hi)

; ---- File tables (RAM) ----
LAT         = $0259     ; Logical file number table
FAT         = $0263     ; Device number table
SAT         = $026D     ; Secondary address table
