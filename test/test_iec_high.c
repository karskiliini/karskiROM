/*
 * test_iec_high.c -- High-level IEC protocol tests (LOAD/SAVE).
 *
 * Tests KERNAL LOAD ($FFD5) against 64korppu firmware over
 * the simulated IEC bus.
 *
 * NOTE: These tests require the cooperative loop (C64 harness +
 * firmware step + bus sim) to work correctly.  If test_iec_low
 * fails, these tests are unlikely to pass.
 *
 * Current status: scaffold with basic framework test.
 * Full LOAD tests require the cooperative loop to handle the
 * complex handshaking of the IEC protocol, which involves
 * interleaving C64 execution and firmware service calls.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "c64_harness.h"
#include "firmware_step.h"
#include "bus_sim.h"
#include "trace.h"
#include "mock_hardware.h"

static const char *kernal_path = NULL;

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do {                                \
    tests_run++;                                          \
    printf("  %-50s ", #fn);                              \
    fn();                                                 \
    tests_passed++;                                       \
    printf("PASS\n");                                     \
} while (0)

/* ------------------------------------------------------------------ */

/*
 * Test: Framework integration -- both sides initialize and the
 * cooperative loop can run without crashing.
 *
 * This is a basic sanity test that proves the C64 harness and
 * firmware can coexist and step together.
 */
static void test_cooperative_loop_does_not_crash(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok && "c64_harness_init");

    firmware_init(&fw, &bus, &trace);

    /* Place a trivial program: just RTS */
    c64.cpu->memory[0xC000] = 0x60;  /* RTS */

    c64_harness_call(&c64, 0xC000, 0, 0, 0);

    /* Run cooperative loop for a few iterations */
    int max_iters = 10;
    for (int i = 0; i < max_iters; i++) {
        if (c64_harness_returned(&c64)) break;
        c64_harness_run(&c64, 1);
        bus_sim_advance(&bus, 1);
        firmware_step(&fw, 1);
    }

    assert(c64_harness_returned(&c64) && "Trivial RTS should return");

    c64_harness_free(&c64);
}

/*
 * Test: Both sides see consistent bus state after initialization.
 */
static void test_bus_consistency_after_init(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok);

    firmware_init(&fw, &bus, &trace);

    /* All lines should be released after init */
    assert(!bus_sim_atn_low(&bus));
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));

    /* C64 should read all input bits HIGH */
    uint8_t dd00 = bus_sim_c64_read(&bus);
    assert((dd00 & (1 << 6)) != 0);  /* CLK IN = HIGH */
    assert((dd00 & (1 << 7)) != 0);  /* DATA IN = HIGH */

    c64_harness_free(&c64);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    kernal_path = (argc > 1) ? argv[1] : "rom/original/kernal-901227-03.bin";

    printf("test_iec_high (KERNAL: %s):\n", kernal_path);

    RUN_TEST(test_cooperative_loop_does_not_crash);
    RUN_TEST(test_bus_consistency_after_init);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
