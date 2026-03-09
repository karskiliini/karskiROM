/*
 * test_iec_low.c -- Low-level IEC protocol tests.
 *
 * Verifies that the C64 KERNAL and 64korppu firmware can communicate
 * over the simulated IEC bus at the byte level.
 *
 * Test approach:
 *   1. Simplest test: write to $DD00 from a tiny 6502 program, verify
 *      the firmware side sees the bus change.
 *   2. Call KERNAL LISTEN ($FFB1) and verify ATN assertion in trace.
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
 * Test 1: Write to $DD00 and verify bus state from device side.
 *
 * A tiny 6502 program:
 *   LDA #$08   ; ATN assert (bit 3)
 *   STA $DD00
 *   RTS
 *
 * After execution, ATN should be asserted on the bus, and the
 * device-side mock GPIO should see ATN LOW.
 */
static void test_dd00_write_visible_to_device(void)
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

    /* Place a small program at $C000 */
    uint8_t code[] = {
        0xA9, 0x08,           /* LDA #$08  (assert ATN) */
        0x8D, 0x00, 0xDD,    /* STA $DD00 */
        0x60                  /* RTS */
    };
    memcpy(c64.cpu->memory + 0xC000, code, sizeof(code));

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 100);

    assert(c64_harness_returned(&c64));

    /* Bus state: ATN should be asserted */
    assert(bus_sim_atn_low(&bus));

    /* Sync device side and verify PIND reflects ATN LOW */
    bus_sim_sync_device(&bus);
    assert((mock_PIND & (1 << 2)) == 0);  /* PD2 = ATN = LOW */

    /* Trace should contain ATN assert event */
    bool found_atn = false;
    for (int i = 0; i < trace.count; i++) {
        if (trace.entries[i].event == TRACE_ATN_ASSERT) {
            found_atn = true;
            break;
        }
    }
    assert(found_atn && "Trace must contain ATN_ASSERT event");

    c64_harness_free(&c64);
}

/*
 * Test 2: Write CLK and DATA to $DD00, verify combined bus state.
 *
 *   LDA #$38   ; ATN(bit3) + CLK(bit4) + DATA(bit5)
 *   STA $DD00
 *   RTS
 */
static void test_dd00_all_lines_asserted(void)
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

    /* Assert ATN + CLK + DATA */
    uint8_t code[] = {
        0xA9, 0x38,           /* LDA #$38  (bits 3,4,5) */
        0x8D, 0x00, 0xDD,    /* STA $DD00 */
        0x60                  /* RTS */
    };
    memcpy(c64.cpu->memory + 0xC000, code, sizeof(code));

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 100);

    assert(c64_harness_returned(&c64));
    assert(bus_sim_atn_low(&bus));
    assert(bus_sim_clk_low(&bus));
    assert(bus_sim_data_low(&bus));

    /* Sync and verify device sees all lines LOW */
    bus_sim_sync_device(&bus);
    assert((mock_PIND & (1 << 2)) == 0);  /* ATN LOW */
    assert((mock_PIND & (1 << 3)) == 0);  /* CLK LOW */
    assert((mock_PIND & (1 << 4)) == 0);  /* DATA LOW */

    c64_harness_free(&c64);
}

/*
 * Test 3: Device asserts DATA, C64 reads it back via $DD00.
 *
 * The 6502 program reads $DD00 and stores the result at $02.
 * We check that bit7 (DATA IN) is 0 when device asserts DATA.
 */
static void test_device_data_readable_by_c64(void)
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

    /* Device asserts DATA line */
    mock_DDRD  |= (1 << 4);   /* PD4 = output */
    mock_PORTD &= ~(1 << 4);  /* PD4 = LOW -> asserting DATA */
    bus_sim_sync_device(&bus);

    /* 6502 program reads $DD00, stores at $02 */
    uint8_t code[] = {
        0xAD, 0x00, 0xDD,    /* LDA $DD00 */
        0x85, 0x02,           /* STA $02   */
        0x60                  /* RTS */
    };
    memcpy(c64.cpu->memory + 0xC000, code, sizeof(code));

    c64_harness_call(&c64, 0xC000, 0, 0, 0);
    c64_harness_run(&c64, 100);

    assert(c64_harness_returned(&c64));

    /* Check stored value: bit7 (DATA IN) should be 0 */
    uint8_t stored = c64.cpu->memory[0x02];
    assert((stored & (1 << 7)) == 0);  /* DATA IN = LOW */
    /* bit6 (CLK IN) should be 1 (nobody asserting CLK) */
    assert((stored & (1 << 6)) != 0);

    c64_harness_free(&c64);
}

/*
 * Test 4: firmware_init sets up device correctly.
 *
 * After firmware_init, the device should be in IDLE state
 * with IEC lines released.
 */
static void test_firmware_init_idle(void)
{
    bus_state_t bus;
    trace_log_t trace;
    firmware_state_t fw;

    bus_sim_init(&bus);
    trace_init(&trace);
    mock_hardware_init();

    firmware_init(&fw, &bus, &trace);

    assert(fw.initialized);

    /* After init, device should not be asserting any IEC lines */
    bus_sim_sync_device(&bus);
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));
}

/*
 * Test 5: firmware_step with ATN asserted causes device to process ATN.
 *
 * When C64 asserts ATN, the device enters ATN handling in iec_service().
 * It asserts DATA as an initial acknowledgement, then tries to receive
 * a command byte. Since no CLK transitions happen (C64 isn't running),
 * iec_receive_byte_atn() will time out. But the call should not crash,
 * and the trace should show that the mock delay accumulator was used
 * (indicating the firmware did enter the ATN processing path).
 */
static void test_firmware_processes_atn_without_crash(void)
{
    bus_state_t bus;
    trace_log_t trace;
    firmware_state_t fw;

    bus_sim_init(&bus);
    trace_init(&trace);
    mock_hardware_init();

    firmware_init(&fw, &bus, &trace);

    /* C64 asserts ATN */
    bus_sim_c64_write(&bus, (1 << 3));  /* bit3 = ATN */
    bus_sim_sync_device(&bus);

    /* Verify device can see ATN */
    assert((mock_PIND & (1 << 2)) == 0);  /* ATN LOW */

    /* Record delay counter before stepping */
    uint64_t delay_before = mock_delay_us_total;

    /* Step the firmware -- it should see ATN and try to receive bytes */
    firmware_step(&fw, 1);

    /* The firmware should have called _delay_us() during ATN handling.
     * iec_receive_byte_atn() enters busy-wait loops that accumulate delay. */
    assert(mock_delay_us_total > delay_before &&
           "Firmware should have entered ATN processing (delay accumulated)");

    /* The firmware should not crash and should return normally */
    /* (If we got here, the firmware handled ATN without crashing) */
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    kernal_path = (argc > 1) ? argv[1] : "rom/original/kernal-901227-03.bin";

    printf("test_iec_low (KERNAL: %s):\n", kernal_path);

    RUN_TEST(test_dd00_write_visible_to_device);
    RUN_TEST(test_dd00_all_lines_asserted);
    RUN_TEST(test_device_data_readable_by_c64);
    RUN_TEST(test_firmware_init_idle);
    RUN_TEST(test_firmware_processes_atn_without_crash);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
