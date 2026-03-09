#ifndef C64_HARNESS_H
#define C64_HARNESS_H

#include <stdint.h>
#include <stdbool.h>
#include "lib6502.h"
#include "bus_sim.h"
#include "trace.h"

/*
 * c64_harness — Wraps lib6502 with C64 KERNAL ROM loaded and
 *               CIA2 $DD00 I/O connected to the bus simulator.
 *
 * Usage:
 *   1. c64_harness_init()  — load KERNAL, set up callbacks
 *   2. c64_harness_call()  — invoke a KERNAL routine by address
 *   3. c64_harness_run()   — execute until routine returns (RTS)
 *   4. Inspect results via c64_harness_a/x/y/status/carry()
 *   5. c64_harness_free()  — clean up
 *
 * Sentinel mechanism:
 *   An illegal opcode ($02) is placed at $FFF0.  c64_harness_call()
 *   pushes a return address that makes RTS land on $FFF0, causing
 *   M6502_run() to exit.  c64_harness_returned() checks for this.
 */

#define C64_SENTINEL_ADDR 0xFFF0

typedef struct {
    M6502 *cpu;
    bus_state_t *bus;
    trace_log_t *trace;
} c64_harness_t;

bool c64_harness_init(c64_harness_t *c64, const char *kernal_path,
                      bus_state_t *bus, trace_log_t *trace);
void c64_harness_run(c64_harness_t *c64, int cycles);
void c64_harness_call(c64_harness_t *c64, uint16_t addr,
                      uint8_t a, uint8_t x, uint8_t y);
bool c64_harness_returned(c64_harness_t *c64);
uint8_t c64_harness_a(c64_harness_t *c64);
uint8_t c64_harness_x(c64_harness_t *c64);
uint8_t c64_harness_y(c64_harness_t *c64);
uint8_t c64_harness_status(c64_harness_t *c64);
bool c64_harness_carry(c64_harness_t *c64);
void c64_harness_free(c64_harness_t *c64);

#endif /* C64_HARNESS_H */
