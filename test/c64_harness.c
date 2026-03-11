/*
 * c64_harness.c — C64 emulation harness for IEC protocol testing.
 *
 * Wraps lib6502 with:
 *   - KERNAL ROM loaded at $E000-$FFFF
 *   - CIA2 $DD00 read/write callbacks connected to bus_sim
 *   - Sentinel illegal opcode at $FFF0 for detecting routine return
 *   - IEC-related zero-page locations initialized
 *
 * lib6502 API notes:
 *   - M6502_run() runs forever; it returns only when an illegal
 *     opcode is encountered (the 'ill' macro does 'return').
 *   - RTS does NOT check call callbacks (noted as a lib6502 bug).
 *   - Read/write callbacks fire on every memory access to the
 *     registered address.
 *   - Registers are in mpu->registers when not inside M6502_run().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c64_harness.h"
#include "firmware_step.h"
#include "mock_hardware.h"

/* ---- Static state for callbacks (lib6502 callbacks lack user data) ---- */

static bus_state_t      *s_bus   = NULL;
static trace_log_t      *s_trace = NULL;
static M6502            *s_cpu   = NULL;
static firmware_state_t *s_fw    = NULL;

/*
 * Watchdog: count $DD00 accesses. If the count exceeds a large threshold,
 * the KERNAL is likely stuck polling the bus. We force-exit by writing
 * the sentinel at $BFFE and redirecting there. This prevents infinite
 * hangs in tests. The $BFFE area is unused RAM.
 */
#define DD00_WATCHDOG_LIMIT  50000
static uint32_t s_dd00_access_count = 0;
static bool s_watchdog_triggered = false;
int c64_dd00_debug = 0;  /* Set to 1 for $DD00 access tracing */

/*
 * Minimal CIA1 Timer B emulation for ACPTR EOI detection.
 * ACPTR starts Timer B ($DC0F) and reads $DC0D to check if
 * the timer expired (bit 1 = Timer B underflow). If the talker
 * holds CLK released for >timeout, ACPTR detects EOI.
 * We count $DD00 accesses as a time proxy.
 */
#define CIA1_TIMER_B_THRESHOLD  8  /* $DD00 accesses until timer fires */
static bool s_timer_b_active = false;
static uint32_t s_timer_b_start = 0;

/* Address in RAM where we plant the watchdog exit trampoline.
 * Must be in writable RAM that the KERNAL won't use. */
#define WATCHDOG_TRAP_ADDR  0xBFF0

/*
 * Number of firmware_step() iterations per $DD00 access.
 * The device needs multiple iec_service() calls to process multi-step
 * protocol states (e.g., after seeing ATN, it needs several calls to
 * progress through the state machine). More iterations allow the device
 * to settle its bus outputs before the C64 reads back.
 */
#define FW_STEPS_PER_DD00_WRITE  8
#define FW_STEPS_PER_DD00_READ   8

/* ---- CIA2 $DD00 callbacks ---- */

static int dd00_write_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)addr;

    s_dd00_access_count++;

    bus_sim_c64_write(s_bus, data);

    if (c64_dd00_debug && s_dd00_access_count < 3000) {
        fprintf(stderr, "DD00 W #%u PC=$%04X val=$%02X  ATN=%d CLK=%d DATA=%d\n",
                s_dd00_access_count, mpu->registers->pc, data,
                (data >> 3) & 1, (data >> 4) & 1, (data >> 5) & 1);
    }

    if (s_trace) {
        trace_record_bus_change(s_trace, s_bus->cycles,
                                TRACE_C64,
                                bus_sim_atn_low(s_bus),
                                bus_sim_clk_low(s_bus),
                                bus_sim_data_low(s_bus));
    }

    /* Let firmware respond to the bus change */
    if (s_fw && s_fw->initialized) {
        for (int i = 0; i < FW_STEPS_PER_DD00_WRITE; i++) {
            firmware_step(s_fw, 1);
        }
    }

    /* Store the value in memory so KERNAL reads back what it wrote */
    mpu->memory[0xDD00] = data;

    return 0;  /* 0 = value already handled, don't store again */
}

static int dd00_read_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)addr;
    (void)data;

    s_dd00_access_count++;

    /* Watchdog: force exit if KERNAL appears stuck polling $DD00 */
    if (s_dd00_access_count >= DD00_WATCHDOG_LIMIT && !s_watchdog_triggered) {
        s_watchdog_triggered = true;

        uint8_t stored_dd00 = mpu->memory[0xDD00];
        bus_sim_sync_device(s_bus);
        uint8_t bus_in = bus_sim_c64_read(s_bus);
        uint8_t dd00_val = (stored_dd00 & 0x3F) | (bus_in & 0xC0);

        fprintf(stderr, "\nWATCHDOG: %u $DD00 accesses — KERNAL stuck\n",
                s_dd00_access_count);
        fprintf(stderr, "  PC=$%04X  A=$%02X  X=$%02X  Y=$%02X  SP=$%02X  P=$%02X\n",
                mpu->registers->pc, mpu->registers->a,
                mpu->registers->x, mpu->registers->y,
                mpu->registers->s, mpu->registers->p);
        fprintf(stderr, "  $DD00 stored=$%02X  bus_in=$%02X  combined=$%02X\n",
                stored_dd00, bus_in, dd00_val);
        fprintf(stderr, "  Bus: ATN=%d CLK=%d DATA=%d\n",
                bus_sim_atn_low(s_bus),
                bus_sim_clk_low(s_bus),
                bus_sim_data_low(s_bus));
        fprintf(stderr, "  Dev: CLK=%d DATA=%d\n",
                s_bus->dev_clk, s_bus->dev_data);
        fprintf(stderr, "  C64: ATN=%d CLK=%d DATA=%d\n",
                s_bus->c64_atn, s_bus->c64_clk, s_bus->c64_data);
        fprintf(stderr, "  ST=$%02X  mock_DDRD=$%02X  mock_PORTD=$%02X  mock_PIND=$%02X\n",
                mpu->memory[0x90], mock_DDRD, mock_PORTD, mock_PIND);

        /* Print nearby code */
        uint16_t pc = mpu->registers->pc;
        fprintf(stderr, "  Code at PC-4:");
        for (int i = -4; i < 8; i++) {
            fprintf(stderr, " %02X", mpu->memory[(pc + i) & 0xFFFF]);
        }
        fprintf(stderr, "\n");

        /* Plant a JMP to sentinel in unused RAM and redirect */
        mpu->memory[WATCHDOG_TRAP_ADDR]     = 0x4C;  /* JMP */
        mpu->memory[WATCHDOG_TRAP_ADDR + 1] = C64_SENTINEL_ADDR & 0xFF;
        mpu->memory[WATCHDOG_TRAP_ADDR + 2] = (C64_SENTINEL_ADDR >> 8) & 0xFF;
    }

    if (s_watchdog_triggered) {
        /* Force the CPU to jump to our trap on the next instruction fetch.
         * We can't safely modify PC from inside a read callback (the CPU
         * may overwrite it). Instead, we return a value that makes the
         * KERNAL's bus-polling loop exit, and also ensure the sentinel
         * is planted. The next instruction fetch will hit 0x02. */
        mpu->memory[C64_SENTINEL_ADDR] = 0x02;
        /* Hack: return a $DD00 value that should satisfy any wait condition
         * the KERNAL might have: DATA IN high and CLK IN high (both released).
         * This makes the KERNAL think the device responded, so it proceeds
         * to the next instruction, which we've patched. */
        return 0xFF;
    }

    /* Let firmware process before we read the bus state,
     * so the device has had time to react. */
    if (s_fw && s_fw->initialized) {
        for (int i = 0; i < FW_STEPS_PER_DD00_READ; i++) {
            firmware_step(s_fw, 1);
        }
    }

    bus_sim_sync_device(s_bus);

    /* Combine stored output bits (3-5) with bus input bits (6-7).
     * The KERNAL does read-modify-write on $DD00 (e.g., LDA $DD00;
     * ORA #$10; STA $DD00), so reads must return the output bits
     * that were previously written, plus the current input state.
     * Bits 0-2 are VIC bank select (preserved from memory). */
    uint8_t stored = mpu->memory[0xDD00];  /* output bits */
    uint8_t bus_in = bus_sim_c64_read(s_bus);  /* input bits (6,7) */

    uint8_t result = (stored & 0x3F) | (bus_in & 0xC0);
    if (c64_dd00_debug && s_dd00_access_count < 3000) {
        fprintf(stderr, "DD00 R #%u PC=$%04X result=$%02X  CLK_IN=%d DATA_IN=%d\n",
                s_dd00_access_count, mpu->registers->pc, result,
                (result >> 6) & 1, (result >> 7) & 1);
    }
    return result;
}

/* ---- CIA1 Timer B callbacks ($DC07, $DC0D, $DC0F) ---- */

static int dc0f_write_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)addr;
    /* Bit 0 = start timer, bit 3 = one-shot, bit 4 = force load.
     * ACPTR writes $19 to start one-shot timer for EOI detection. */
    if (data & 0x01) {
        s_timer_b_active = true;
        s_timer_b_start = s_dd00_access_count;
    }
    mpu->memory[0xDC0F] = data;
    return 0;
}

static int dc0d_read_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)addr;
    (void)data;
    uint8_t icr = 0;
    if (s_timer_b_active &&
        (s_dd00_access_count - s_timer_b_start) > CIA1_TIMER_B_THRESHOLD) {
        icr |= 0x02;  /* Timer B underflow */
        s_timer_b_active = false;  /* One-shot: auto-stop */
    }
    return icr;
}

/* ---- Initialization ---- */

bool c64_harness_init(c64_harness_t *c64, const char *kernal_path,
                      bus_state_t *bus, trace_log_t *trace)
{
    memset(c64, 0, sizeof(*c64));

    /* Create the 6502 with lib6502-allocated memory, registers, callbacks */
    c64->cpu = M6502_new(NULL, NULL, NULL);
    if (!c64->cpu) {
        fprintf(stderr, "c64_harness: M6502_new failed\n");
        return false;
    }

    c64->bus   = bus;
    c64->trace = trace;

    /* Set up static pointers for callbacks */
    s_bus   = bus;
    s_trace = trace;
    s_cpu   = c64->cpu;
    s_fw    = NULL;  /* No firmware attached by default */
    c64->fw = NULL;

    M6502 *mpu = c64->cpu;

    /* Zero-fill RAM $0000-$CFFF (already zeroed by calloc in M6502_new,
     * but be explicit) */
    memset(mpu->memory, 0x00, 0xD000);

    /* Load KERNAL ROM (8192 bytes) at $E000-$FFFF */
    FILE *f = fopen(kernal_path, "rb");
    if (!f) {
        fprintf(stderr, "c64_harness: cannot open KERNAL ROM: %s\n",
                kernal_path);
        return false;
    }

    size_t n = fread(mpu->memory + 0xE000, 1, 8192, f);
    fclose(f);

    if (n != 8192) {
        fprintf(stderr, "c64_harness: KERNAL ROM short read (%zu bytes)\n", n);
        return false;
    }

    /* Place illegal opcode at sentinel address $FFF0.
     * Opcode $02 is undefined on 6502; lib6502's 'ill' macro
     * prints a message to stderr and returns from M6502_run(). */
    mpu->memory[C64_SENTINEL_ADDR] = 0x02;

    /* Install CIA2 $DD00 callbacks */
    M6502_setCallback(mpu, write, 0xDD00, dd00_write_cb);
    M6502_setCallback(mpu, read,  0xDD00, dd00_read_cb);

    /* Install CIA1 Timer B callbacks for ACPTR EOI detection */
    M6502_setCallback(mpu, write, 0xDC0F, dc0f_write_cb);
    M6502_setCallback(mpu, read,  0xDC0D, dc0d_read_cb);

    /* Initialize IEC-related zero-page locations */
    mpu->memory[0xBA] = 8;   /* Current device number */
    mpu->memory[0xB9] = 0;   /* Current secondary address */
    mpu->memory[0xB8] = 0;   /* Current logical file number */
    mpu->memory[0x98] = 0;   /* Number of open files */
    mpu->memory[0x9A] = 3;   /* Current output device: screen */
    mpu->memory[0x99] = 0;   /* Current input device: keyboard */
    mpu->memory[0x91] = 0xFF; /* Keyboard row: no keys pressed (for STOP check) */
    mpu->memory[0x9D] = 0;   /* System message flag: suppress messages */

    /* Initialize KERNAL I/O vectors at $0314-$0333.
     * Normally these are set by KERNAL cold start. We copy the
     * default vector table from the KERNAL ROM at $FD30. */
    static const uint16_t kernal_vectors[] = {
        0xEA31, /* $0314 IRQ */
        0xFE66, /* $0316 BRK */
        0xFE47, /* $0318 NMI */
        0xF34A, /* $031A OPEN */
        0xF291, /* $031C CLOSE */
        0xF20E, /* $031E CHKIN */
        0xF250, /* $0320 CHKOUT */
        0xF333, /* $0322 CLRCHN */
        0xF157, /* $0324 CHRIN */
        0xF1CA, /* $0326 CHROUT */
        0xF6ED, /* $0328 STOP */
        0xF13E, /* $032A GETIN */
        0xF32F, /* $032C CLALL */
        0xFE66, /* $032E usr cmd */
        0xF4A5, /* $0330 LOAD */
        0xF5ED, /* $0332 SAVE */
    };
    for (int i = 0; i < 16; i++) {
        mpu->memory[0x0314 + i * 2]     = kernal_vectors[i] & 0xFF;
        mpu->memory[0x0314 + i * 2 + 1] = (kernal_vectors[i] >> 8) & 0xFF;
    }

    /* Set stack pointer to top of stack page */
    mpu->registers->s = 0xFF;

    return true;
}

/* ---- Firmware attachment ---- */

void c64_harness_attach_firmware(c64_harness_t *c64, firmware_state_t *fw)
{
    c64->fw = fw;
    s_fw = fw;
}

/* ---- Execution ---- */

void c64_harness_call(c64_harness_t *c64, uint16_t addr,
                      uint8_t a, uint8_t x, uint8_t y)
{
    M6502 *mpu = c64->cpu;

    /* Reset watchdog and timer state for each new call */
    s_dd00_access_count = 0;
    s_watchdog_triggered = false;
    s_timer_b_active = false;

    /* Push sentinel return address onto 6502 stack.
     * RTS pops low byte, then high byte, then adds 1 to form the PC.
     * So we push (sentinel - 1) = $FFEF.
     * Push order: high byte first, then low byte (stack grows down). */
    uint16_t ret_addr = C64_SENTINEL_ADDR - 1;  /* $FFEF */
    mpu->memory[0x0100 + mpu->registers->s] = (uint8_t)(ret_addr >> 8);
    mpu->registers->s--;
    mpu->memory[0x0100 + mpu->registers->s] = (uint8_t)(ret_addr & 0xFF);
    mpu->registers->s--;

    /* Set registers */
    mpu->registers->a  = a;
    mpu->registers->x  = x;
    mpu->registers->y  = y;
    mpu->registers->pc = addr;

    /* Ensure sentinel opcode is in place */
    mpu->memory[C64_SENTINEL_ADDR] = 0x02;
}

void c64_harness_run(c64_harness_t *c64, int cycles)
{
    (void)cycles;
    M6502_run(c64->cpu);
}

/* ---- Result inspection ---- */

bool c64_harness_returned(c64_harness_t *c64)
{
    /* After hitting the illegal opcode $02 at $FFF0, lib6502 exits
     * M6502_run().  The exact PC depends on the compiler path:
     *   - GCC computed-goto path: fetch() increments PC twice -> $FFF2
     *   - ANSI switch path:      fetch() is a no-op           -> $FFF1
     * We check for either case. */
    uint16_t pc = c64->cpu->registers->pc;
    return (pc == C64_SENTINEL_ADDR + 1) || (pc == C64_SENTINEL_ADDR + 2);
}

uint8_t c64_harness_a(c64_harness_t *c64)
{
    return c64->cpu->registers->a;
}

uint8_t c64_harness_x(c64_harness_t *c64)
{
    return c64->cpu->registers->x;
}

uint8_t c64_harness_y(c64_harness_t *c64)
{
    return c64->cpu->registers->y;
}

uint8_t c64_harness_status(c64_harness_t *c64)
{
    return c64->cpu->registers->p;
}

bool c64_harness_carry(c64_harness_t *c64)
{
    return (c64->cpu->registers->p & 0x01) != 0;
}

/* ---- Cleanup ---- */

void c64_harness_free(c64_harness_t *c64)
{
    if (c64->cpu) {
        M6502_delete(c64->cpu);
        c64->cpu = NULL;
    }
    s_bus   = NULL;
    s_trace = NULL;
    s_cpu   = NULL;
    s_fw    = NULL;
}
