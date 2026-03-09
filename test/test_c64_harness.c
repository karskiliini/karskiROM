/*
 * test_c64_harness.c — Tests for C64 emulation harness.
 *
 * Build & run:
 *   cc -Wall -I test -I vendor/lib6502 \
 *      test/test_c64_harness.c test/c64_harness.c test/bus_sim.c \
 *      test/mock_hardware.c test/trace.c vendor/lib6502/lib6502.c \
 *      -o test/test_c64_harness && ./test/test_c64_harness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "c64_harness.h"

static const char *kernal_rom_path = "rom/original/kernal-901227-03.bin";
#define KERNAL_PATH kernal_rom_path

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
 * Test: KERNAL ROM loads correctly.
 *
 * The standard C64 KERNAL (901227-03) has its RESET vector at
 * $FFFC/$FFFD pointing to $FCE2 (cold start).
 */
static void test_kernal_loads_and_reset_vector(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok && "c64_harness_init must succeed");

    /* Check RESET vector at $FFFC/$FFFD = $FCE2 */
    uint8_t lo = c64.cpu->memory[0xFFFC];
    uint8_t hi = c64.cpu->memory[0xFFFD];
    uint16_t reset_vec = lo | (hi << 8);

    if (reset_vec != 0xFCE2) {
        fprintf(stderr, "FAIL: RESET vector = $%04X, expected $FCE2\n",
                reset_vec);
        abort();
    }
    assert(reset_vec == 0xFCE2);

    c64_harness_free(&c64);
}

/*
 * Test: NMI vector is correct ($FE43 for KERNAL 901227-03).
 */
static void test_kernal_nmi_vector(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    uint16_t nmi_vec = c64.cpu->memory[0xFFFA]
                     | (c64.cpu->memory[0xFFFB] << 8);
    if (nmi_vec != 0xFE43) {
        fprintf(stderr, "FAIL: NMI vector = $%04X, expected $FE43\n", nmi_vec);
        abort();
    }

    c64_harness_free(&c64);
}

/*
 * Test: Sentinel opcode is in place at $FFF0.
 */
static void test_sentinel_opcode(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    assert(c64.cpu->memory[C64_SENTINEL_ADDR] == 0x02);

    c64_harness_free(&c64);
}

/*
 * Test: IEC zero-page locations are initialized correctly.
 */
static void test_iec_zeropage_init(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    assert(c64.cpu->memory[0xBA] == 8);   /* device number */
    assert(c64.cpu->memory[0xB9] == 0);   /* secondary address */
    assert(c64.cpu->memory[0xB8] == 0);   /* logical file number */
    assert(c64.cpu->memory[0x98] == 0);   /* number of open files */
    assert(c64.cpu->memory[0x9A] == 3);   /* output device: screen */
    assert(c64.cpu->memory[0x99] == 0);   /* input device: keyboard */

    c64_harness_free(&c64);
}

/*
 * Test: Call a trivial subroutine that just does RTS.
 *
 * We place an RTS ($60) at $C000 and call it.  The RTS should
 * return to the sentinel at $FFF0, hitting the illegal opcode
 * and stopping M6502_run().
 */
static void test_call_rts_returns_to_sentinel(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    /* Place RTS at $C000 */
    c64.cpu->memory[0xC000] = 0x60;  /* RTS */

    /* Call $C000 with A=0x42, X=0x13, Y=0x37 */
    c64_harness_call(&c64, 0xC000, 0x42, 0x13, 0x37);

    /* Verify registers were set */
    assert(c64.cpu->registers->a == 0x42);
    assert(c64.cpu->registers->x == 0x13);
    assert(c64.cpu->registers->y == 0x37);
    assert(c64.cpu->registers->pc == 0xC000);

    /* Run — RTS should pop sentinel address and hit ill opcode */
    c64_harness_run(&c64, 100);

    /* Should have returned to sentinel */
    assert(c64_harness_returned(&c64));

    /* Registers should be preserved through the RTS */
    assert(c64_harness_a(&c64) == 0x42);
    assert(c64_harness_x(&c64) == 0x13);
    assert(c64_harness_y(&c64) == 0x37);

    c64_harness_free(&c64);
}

/*
 * Test: A subroutine that modifies registers before RTS.
 *
 * Place a small routine at $C000 that does:
 *   LDA #$AA
 *   LDX #$BB
 *   LDY #$CC
 *   SEC        (set carry)
 *   RTS
 */
static void test_call_routine_modifies_registers(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    /* LDA #$AA; LDX #$BB; LDY #$CC; SEC; RTS */
    uint8_t code[] = {
        0xA9, 0xAA,   /* LDA #$AA */
        0xA2, 0xBB,   /* LDX #$BB */
        0xA0, 0xCC,   /* LDY #$CC */
        0x38,          /* SEC */
        0x60           /* RTS */
    };
    memcpy(c64.cpu->memory + 0xC000, code, sizeof(code));

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 100);

    assert(c64_harness_returned(&c64));
    assert(c64_harness_a(&c64) == 0xAA);
    assert(c64_harness_x(&c64) == 0xBB);
    assert(c64_harness_y(&c64) == 0xCC);
    assert(c64_harness_carry(&c64));

    c64_harness_free(&c64);
}

/*
 * Test: $DD00 write callback triggers bus_sim_c64_write.
 *
 * Place a routine that writes to $DD00 and verify the bus state.
 */
static void test_dd00_write_updates_bus(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, KERNAL_PATH, &bus, &trace);
    assert(ok);

    /* LDA #$08; STA $DD00; RTS
     * $08 = bit3 = ATN assert */
    uint8_t code[] = {
        0xA9, 0x08,           /* LDA #$08 */
        0x8D, 0x00, 0xDD,    /* STA $DD00 */
        0x60                  /* RTS */
    };
    memcpy(c64.cpu->memory + 0xC000, code, sizeof(code));

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 100);

    assert(c64_harness_returned(&c64));
    assert(bus_sim_atn_low(&bus));
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));

    c64_harness_free(&c64);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc > 1) kernal_rom_path = argv[1];

    printf("c64_harness tests (KERNAL: %s):\n", kernal_rom_path);

    RUN_TEST(test_kernal_loads_and_reset_vector);
    RUN_TEST(test_kernal_nmi_vector);
    RUN_TEST(test_sentinel_opcode);
    RUN_TEST(test_iec_zeropage_init);
    RUN_TEST(test_call_rts_returns_to_sentinel);
    RUN_TEST(test_call_routine_modifies_registers);
    RUN_TEST(test_dd00_write_updates_bus);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
