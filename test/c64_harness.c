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

/* ---- Static state for callbacks (lib6502 callbacks lack user data) ---- */

static bus_state_t  *s_bus   = NULL;
static trace_log_t  *s_trace = NULL;
static M6502        *s_cpu   = NULL;

/* ---- CIA2 $DD00 callbacks ---- */

static int dd00_write_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)addr;

    bus_sim_c64_write(s_bus, data);

    if (s_trace) {
        trace_record_bus_change(s_trace, s_bus->cycles,
                                TRACE_C64,
                                bus_sim_atn_low(s_bus),
                                bus_sim_clk_low(s_bus),
                                bus_sim_data_low(s_bus));
    }

    /* Store the value in memory so KERNAL reads back what it wrote */
    mpu->memory[0xDD00] = data;

    return 0;  /* 0 = value already handled, don't store again */
}

static int dd00_read_cb(M6502 *mpu, uint16_t addr, uint8_t data)
{
    (void)mpu;
    (void)addr;
    (void)data;

    bus_sim_sync_device(s_bus);
    return bus_sim_c64_read(s_bus);
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

    /* Initialize IEC-related zero-page locations */
    mpu->memory[0xBA] = 8;   /* Current device number */
    mpu->memory[0xB9] = 0;   /* Current secondary address */
    mpu->memory[0xB8] = 0;   /* Current logical file number */
    mpu->memory[0x98] = 0;   /* Number of open files */
    mpu->memory[0x9A] = 3;   /* Current output device: screen */
    mpu->memory[0x99] = 0;   /* Current input device: keyboard */

    /* Set stack pointer to top of stack page */
    mpu->registers->s = 0xFF;

    return true;
}

/* ---- Execution ---- */

void c64_harness_call(c64_harness_t *c64, uint16_t addr,
                      uint8_t a, uint8_t x, uint8_t y)
{
    M6502 *mpu = c64->cpu;

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
    (void)cycles;  /* Currently unused — M6502_run returns when hitting
                    * the illegal opcode sentinel at $FFF0.
                    * Cycle-limited execution would require instrumenting
                    * lib6502 (it has no cycle-counting hooks). */

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
}
