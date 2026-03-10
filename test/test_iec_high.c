/*
 * test_iec_high.c -- High-level IEC protocol tests (LOAD/SAVE).
 *
 * Tests KERNAL LOAD ($FFD5) against 64korppu firmware over
 * the simulated IEC bus using callback-based cooperative scheduling.
 *
 * Architecture:
 *   - M6502_run() executes KERNAL code until sentinel
 *   - Every $DD00 access fires firmware_step() via callbacks
 *   - The firmware's non-blocking state machine handles IEC protocol
 *   - This enables full KERNAL<->firmware IEC handshaking
 *
 * Tests build on test_iec_low: if LISTEN/TALK/UNLSN/UNTLK work
 * (verified there), we can attempt full LOAD sequences here.
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
#include "fat12.h"

/* External: mock_fat12 test helpers */
extern void mock_fat12_add_file(const char *name8, const char *ext3,
                                const uint8_t *data, uint32_t size);
extern void mock_fat12_reset(void);

/* External: IEC debug flag */
extern int iec_debug_enabled;

static const char *kernal_path = NULL;

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do {                                \
    tests_run++;                                          \
    printf("  %-50s ", #fn);                              \
    fflush(stdout);                                       \
    fn();                                                 \
    tests_passed++;                                       \
    printf("PASS\n");                                     \
} while (0)

/* ------------------------------------------------------------------ */

/*
 * Helper: Set up both harness and firmware for cooperative IEC tests.
 */
static bool setup_iec(c64_harness_t *c64, firmware_state_t *fw,
                      bus_state_t *bus, trace_log_t *trace)
{
    bus_sim_init(bus);
    trace_init(trace);

    if (!c64_harness_init(c64, kernal_path, bus, trace))
        return false;

    firmware_init(fw, bus, trace);
    c64_harness_attach_firmware(c64, fw);

    /* Mount the mock filesystem */
    fat12_mount();

    return true;
}

/* ------------------------------------------------------------------ */

/*
 * Test: Framework integration -- both sides initialize and
 * cooperative scheduling works without crashing.
 */
static void test_cooperative_loop_does_not_crash(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    bool ok = setup_iec(&c64, &fw, &bus, &trace);
    assert(ok && "setup_iec");

    /* Place a trivial program: just RTS */
    c64.cpu->memory[0xC000] = 0x60;

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 0);

    assert(c64_harness_returned(&c64));

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

    bool ok = setup_iec(&c64, &fw, &bus, &trace);
    assert(ok);

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

/*
 * Test: LISTEN + send secondary address (OPEN command sequence).
 *
 * The KERNAL OPEN for serial devices does:
 *   LISTEN device -> TKSA (send SA under ATN) -> CIOUT filename bytes -> UNLISTEN
 *
 * This test verifies LISTEN + TKSA (the first two steps).
 */
static void test_listen_and_secondary_address(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    bool ok = setup_iec(&c64, &fw, &bus, &trace);
    assert(ok);

    M6502 *mpu = c64.cpu;
    mpu->memory[0x90] = 0;  /* Clear status */
    mpu->memory[0x94] = 0;  /* Clear flag */
    mpu->memory[0xA3] = 0;

    /* LISTEN device 8 */
    c64_harness_call(&c64, 0xFFB1, 8, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64));
    assert((mpu->memory[0x90] & 0x80) == 0 && "LISTEN OK");

    /* Send secondary address: TKSA at $EDB9
     * A = $60 (OPEN + SA 0) */
    c64_harness_call(&c64, 0xEDB9, 0x60, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64));

    /* After TKSA, ATN should be released */
    assert(!bus_sim_atn_low(&bus) && "ATN released after TKSA");

    /* UNLISTEN */
    c64_harness_call(&c64, 0xFFAE, 0, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64));

    c64_harness_free(&c64);
}

/*
 * Test: Full LOAD sequence.
 *
 * Add a test file to mock_fat12, then call KERNAL LOAD.
 * Verify that the file contents appear in C64 memory.
 *
 * The test file format for LOAD:
 *   - First 2 bytes: load address (low, high)
 *   - Remaining bytes: data to load at that address
 *
 * KERNAL LOAD reads the load address from the first two bytes sent
 * by the device (via ACPTR), then stores subsequent bytes at that
 * address (incrementing).
 */
static void test_load_file(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    iec_debug_enabled = 1;

    bool ok = setup_iec(&c64, &fw, &bus, &trace);
    assert(ok);

    /* Create a test PRG file: load address $C000, data = "HELLO" */
    uint8_t prg_data[] = {
        0x00, 0xC0,             /* Load address: $C000 */
        'H', 'E', 'L', 'L', 'O'
    };
    mock_fat12_reset();
    fat12_mount();
    mock_fat12_add_file("TEST    ", "PRG", prg_data, sizeof(prg_data));

    M6502 *mpu = c64.cpu;

    /* Place filename "TEST" in C64 memory at $C100 */
    const char *filename = "TEST";
    size_t flen = strlen(filename);
    memcpy(mpu->memory + 0xC100, filename, flen);

    /* Set up KERNAL state for LOAD:
     * SETNAM: $B7=length, $BB/$BC=filename pointer
     * SETLFS: $B8=logical file, $BA=device, $B9=secondary address */
    mpu->memory[0xB7] = (uint8_t)flen;   /* Filename length */
    mpu->memory[0xBB] = 0x00;             /* Filename ptr low */
    mpu->memory[0xBC] = 0xC1;             /* Filename ptr high ($C100) */
    mpu->memory[0xB8] = 1;                /* Logical file number */
    mpu->memory[0xBA] = 8;                /* Device number */
    mpu->memory[0xB9] = 0;                /* Secondary address (0 = use header address) */
    mpu->memory[0x90] = 0;                /* Clear status */
    mpu->memory[0x94] = 0;                /* Clear flag byte */
    mpu->memory[0xA3] = 0;                /* Clear bit counter */
    mpu->memory[0x93] = 0;                /* LOAD (not VERIFY) */

    /* Clear the target area */
    memset(mpu->memory + 0xC000, 0, 16);

    /* Call LOAD at $FFD5 with A=0 (LOAD), X/Y = alt load address $C000 */
    c64_harness_call(&c64, 0xFFD5, 0, 0x00, 0xC0);
    c64_harness_run(&c64, 0);

    bool returned = c64_harness_returned(&c64);
    uint8_t st = mpu->memory[0x90];

    if (!returned) {
        fprintf(stderr, "\n    LOAD did not return (hung). PC=$%04X\n",
                mpu->registers->pc);
        fprintf(stderr, "    ST=$%02X\n", st);
        fprintf(stderr, "    Trace (%d entries):\n", trace.count);
        if (trace.count > 20) {
            fprintf(stderr, "    (showing last 20)\n");
            for (int i = trace.count - 20; i < trace.count; i++) {
                trace_entry_t *e = &trace.entries[i];
                fprintf(stderr, "    %8llu %-4s %-6s\n",
                        (unsigned long long)e->cycle,
                        e->dir == TRACE_C64 ? "C64" : "DEV",
                        e->event == TRACE_ATN_ASSERT ? "ATN+" :
                        e->event == TRACE_ATN_RELEASE ? "ATN-" :
                        e->event == TRACE_CLK_ASSERT ? "CLK+" :
                        e->event == TRACE_CLK_RELEASE ? "CLK-" :
                        e->event == TRACE_DATA_ASSERT ? "DATA+" :
                        e->event == TRACE_DATA_RELEASE ? "DATA-" : "???");
            }
        }
    }
    assert(returned && "LOAD should return");

    /* Check carry flag: clear = success, set = error */
    bool carry = c64_harness_carry(&c64);
    if (carry) {
        fprintf(stderr, "\n    LOAD returned with carry set (error). ST=$%02X\n", st);
        fprintf(stderr, "    A=$%02X (error code)\n", c64_harness_a(&c64));
    }
    assert(!carry && "LOAD should succeed (carry clear)");

    /* Verify: data should be at $C000 */
    assert(mpu->memory[0xC000] == 'H');
    assert(mpu->memory[0xC001] == 'E');
    assert(mpu->memory[0xC002] == 'L');
    assert(mpu->memory[0xC003] == 'L');
    assert(mpu->memory[0xC004] == 'O');

    /* Verify: end address should be in X/Y (low/high) */
    uint16_t end_addr = c64_harness_x(&c64) | (c64_harness_y(&c64) << 8);
    assert(end_addr == 0xC005 && "End address should be $C005");

    c64_harness_free(&c64);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    kernal_path = (argc > 1) ? argv[1] : "rom/original/kernal-901227-03.bin";

    printf("test_iec_high (KERNAL: %s):\n", kernal_path);

    RUN_TEST(test_cooperative_loop_does_not_crash);
    RUN_TEST(test_bus_consistency_after_init);
    RUN_TEST(test_listen_and_secondary_address);
    RUN_TEST(test_load_file);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
