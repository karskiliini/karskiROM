/*
 * test_iec_low.c -- Low-level IEC protocol tests.
 *
 * Verifies that the C64 KERNAL and 64korppu firmware can communicate
 * over the simulated IEC bus at the byte level using callback-based
 * cooperative scheduling.
 *
 * Architecture:
 *   - M6502_run() executes KERNAL code until it hits the sentinel
 *   - Every $DD00 read/write triggers firmware_step() via callbacks
 *   - This gives the firmware state machine a chance to respond to
 *     each bus state change, enabling full IEC protocol handshaking
 *
 * Tests progress from basic bus verification to full KERNAL routine
 * execution (LISTEN, TALK, UNLSN, UNTLK, ACPTR).
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
    fflush(stdout);                                       \
    fn();                                                 \
    tests_passed++;                                       \
    printf("PASS\n");                                     \
} while (0)

/* ------------------------------------------------------------------ */

/*
 * Test 1: Write to $DD00 and verify bus state from device side.
 */
static void test_dd00_write_visible_to_device(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok && "c64_harness_init");

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
 */
static void test_dd00_all_lines_asserted(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok);

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
 */
static void test_device_data_readable_by_c64(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok);

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
 * Test 5: State machine responds to ATN without blocking.
 *
 * When C64 asserts ATN, the firmware state machine should enter
 * ATN handling and assert DATA as acknowledgment.
 */
static void test_firmware_responds_to_atn(void)
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

    /* Step the firmware -- it should see ATN and assert DATA */
    firmware_step(&fw, 1);

    /* After one step, the firmware state machine should have asserted DATA */
    bus_sim_sync_device(&bus);
    assert(bus_sim_data_low(&bus) && "Device should assert DATA after seeing ATN");
}

/*
 * Test 6: State machine receives a command byte under ATN.
 *
 * Simulate the C64 sending a LISTEN command byte (0x28 = LISTEN device 8)
 * bit by bit under ATN. The firmware state machine should receive it.
 */
static void test_firmware_receives_atn_byte(void)
{
    bus_state_t bus;
    trace_log_t trace;
    firmware_state_t fw;

    bus_sim_init(&bus);
    trace_init(&trace);
    mock_hardware_init();

    firmware_init(&fw, &bus, &trace);

    /* Phase 1: Assert ATN + CLK (C64 initial state for sending under ATN) */
    bus_sim_c64_write(&bus, (1 << 3) | (1 << 4));  /* ATN + CLK */
    bus_sim_sync_device(&bus);

    /* Step firmware until it asserts DATA (ATN ack) */
    for (int i = 0; i < 10; i++) {
        firmware_step(&fw, 1);
    }
    bus_sim_sync_device(&bus);
    assert(bus_sim_data_low(&bus) && "Device should DATA-ack ATN");

    /* Phase 2: Release CLK (signal ready to send) */
    bus_sim_c64_write(&bus, (1 << 3));  /* ATN only, CLK released */
    bus_sim_sync_device(&bus);

    /* Step firmware until it releases DATA (ready for bits) */
    for (int i = 0; i < 10; i++) {
        firmware_step(&fw, 1);
    }

    /* Phase 3: Send byte 0x28 (LISTEN device 8) bit by bit, LSB first
     * 0x28 = 00101000 binary, LSB first: 0,0,0,1,0,1,0,0 */
    uint8_t cmd = 0x28;  /* LISTEN + device 8 */
    for (int bit = 0; bit < 8; bit++) {
        /* Set DATA line for this bit and assert CLK */
        uint8_t dd00 = (1 << 3) | (1 << 4);  /* ATN + CLK */
        if (!(cmd & (1 << bit))) {
            dd00 |= (1 << 5);  /* DATA asserted = bit 0 */
        }
        /* else DATA released = bit 1 */

        bus_sim_c64_write(&bus, dd00);
        bus_sim_sync_device(&bus);

        /* Step firmware to sample the bit */
        for (int i = 0; i < 4; i++) {
            firmware_step(&fw, 1);
        }

        /* Release CLK (bit end) */
        dd00 &= ~(1 << 4);  /* CLK released */
        bus_sim_c64_write(&bus, dd00);
        bus_sim_sync_device(&bus);

        for (int i = 0; i < 4; i++) {
            firmware_step(&fw, 1);
        }
    }

    /* After all 8 bits, the firmware should ACK by asserting DATA */
    bus_sim_sync_device(&bus);

    /* Give firmware time to process the complete byte */
    for (int i = 0; i < 10; i++) {
        firmware_step(&fw, 1);
    }
    bus_sim_sync_device(&bus);

    assert(bus_sim_data_low(&bus) && "Device should DATA-ack after receiving byte");
}

/*
 * Test 7: KERNAL LISTEN routine works with firmware attached.
 *
 * Call the KERNAL's LISTEN routine at $FFB1 with device number 8.
 * With the firmware attached via cooperative scheduling, the KERNAL
 * should be able to complete the LISTEN handshake.
 *
 * KERNAL LISTEN ($FFB1 -> $ED0C) expects A = device number.
 * It internally ORs 0x20 to make the LISTEN command byte.
 */
static void test_listen_device8(void)
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
    c64_harness_attach_firmware(&c64, &fw);

    /* Set up zero-page for IEC operation */
    c64.cpu->memory[0xBA] = 8;   /* Device number */
    c64.cpu->memory[0x90] = 0;   /* ST (status) clear */
    c64.cpu->memory[0x94] = 0;   /* Flag byte */
    c64.cpu->memory[0xA3] = 0;   /* Bit counter */

    /* Call KERNAL LISTEN with A = device 8 */
    c64_harness_call(&c64, 0xFFB1, 8, 0, 0);
    c64_harness_run(&c64, 0);

    /* Verify: routine should have returned (hit sentinel) */
    assert(c64_harness_returned(&c64) && "LISTEN should return");

    /* Verify: trace should show ATN was asserted */
    bool found_atn = false;
    for (int i = 0; i < trace.count; i++) {
        if (trace.entries[i].event == TRACE_ATN_ASSERT &&
            trace.entries[i].dir == TRACE_C64) {
            found_atn = true;
            break;
        }
    }
    assert(found_atn && "LISTEN must assert ATN");

    /* Verify: status byte should not indicate an error.
     * Bit 7 = device not present. If the firmware responded properly,
     * this should be clear. Bit 1 = timeout on read. */
    uint8_t st = c64.cpu->memory[0x90];
    if (st & 0x80) {
        fprintf(stderr, "\n    WARNING: ST=$%02X (device not present flag set)\n", st);
        fprintf(stderr, "    Trace (%d entries):\n", trace.count);
        trace_print(&trace, stderr);
    }
    assert((st & 0x80) == 0 && "LISTEN: device-not-present flag must be clear");

    c64_harness_free(&c64);
}

/*
 * Test 8: KERNAL UNLSN works after LISTEN.
 *
 * Send LISTEN then UNLISTEN. The firmware should process both
 * commands and return to idle state.
 */
extern int iec_debug_enabled;
static void test_unlisten_after_listen(void)
{
    bus_state_t bus;
    trace_log_t trace;
    c64_harness_t c64;
    firmware_state_t fw;

    iec_debug_enabled = 1;

    bus_sim_init(&bus);
    trace_init(&trace);

    bool ok = c64_harness_init(&c64, kernal_path, &bus, &trace);
    assert(ok);

    firmware_init(&fw, &bus, &trace);
    c64_harness_attach_firmware(&c64, &fw);

    c64.cpu->memory[0x90] = 0;
    c64.cpu->memory[0x94] = 0;
    c64.cpu->memory[0xA3] = 0;

    /* LISTEN device 8 */
    c64_harness_call(&c64, 0xFFB1, 8, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64) && "LISTEN should return");

    uint8_t st = c64.cpu->memory[0x90];
    assert((st & 0x80) == 0 && "LISTEN: no error");

    /* UNLISTEN ($FFAE -> $EDFE) */
    c64_harness_call(&c64, 0xFFAE, 0, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64) && "UNLSN should return");

    /* After UNLISTEN, ATN should be released */
    assert(!bus_sim_atn_low(&bus) && "ATN should be released after UNLSN");

    c64_harness_free(&c64);
}

/*
 * Test 9: KERNAL TALK routine works with firmware attached.
 *
 * Call TALK at $FFB4 with device 8. Similar to LISTEN but sends
 * the TALK command (0x48).
 */
static void test_talk_device8(void)
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
    c64_harness_attach_firmware(&c64, &fw);

    c64.cpu->memory[0x90] = 0;
    c64.cpu->memory[0x94] = 0;
    c64.cpu->memory[0xA3] = 0;

    /* Call KERNAL TALK with A = 8 */
    c64_harness_call(&c64, 0xFFB4, 8, 0, 0);
    c64_harness_run(&c64, 0);

    assert(c64_harness_returned(&c64) && "TALK should return");

    /* Verify ATN was asserted during TALK */
    bool found_atn = false;
    for (int i = 0; i < trace.count; i++) {
        if (trace.entries[i].event == TRACE_ATN_ASSERT) {
            found_atn = true;
            break;
        }
    }
    assert(found_atn && "TALK must assert ATN");

    /* Check status */
    uint8_t st = c64.cpu->memory[0x90];
    if (st & 0x80) {
        fprintf(stderr, "\n    WARNING: ST=$%02X after TALK\n", st);
        trace_print(&trace, stderr);
    }
    assert((st & 0x80) == 0 && "TALK: device-not-present flag must be clear");

    c64_harness_free(&c64);
}

/*
 * Test 10: KERNAL UNTALK works after TALK.
 */
static void test_untalk_after_talk(void)
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
    c64_harness_attach_firmware(&c64, &fw);

    c64.cpu->memory[0x90] = 0;
    c64.cpu->memory[0x94] = 0;
    c64.cpu->memory[0xA3] = 0;

    /* TALK device 8 */
    c64_harness_call(&c64, 0xFFB4, 8, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64));

    uint8_t st = c64.cpu->memory[0x90];
    assert((st & 0x80) == 0);

    /* UNTALK ($FFAB -> $EDEF) */
    c64_harness_call(&c64, 0xFFAB, 0, 0, 0);
    c64_harness_run(&c64, 0);
    assert(c64_harness_returned(&c64) && "UNTLK should return");

    /* ATN should be released after UNTALK */
    assert(!bus_sim_atn_low(&bus));

    c64_harness_free(&c64);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    kernal_path = (argc > 1) ? argv[1] : "rom/original/kernal-901227-03.bin";

    printf("test_iec_low (KERNAL: %s):\n", kernal_path);

    /* Bus-level tests (no firmware needed) */
    RUN_TEST(test_dd00_write_visible_to_device);
    RUN_TEST(test_dd00_all_lines_asserted);
    RUN_TEST(test_device_data_readable_by_c64);
    RUN_TEST(test_firmware_init_idle);

    /* State machine tests (firmware only, no C64) */
    RUN_TEST(test_firmware_responds_to_atn);
    RUN_TEST(test_firmware_receives_atn_byte);

    /* Full KERNAL+firmware cooperative tests */
    RUN_TEST(test_listen_device8);
    RUN_TEST(test_unlisten_after_listen);
    RUN_TEST(test_talk_device8);
    RUN_TEST(test_untalk_after_talk);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
