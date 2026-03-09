/*
 * test_bus_sim.c — Tests for IEC bus simulator.
 *
 * Build & run:
 *   cc -Wall -I test test/test_bus_sim.c test/bus_sim.c test/mock_hardware.c \
 *      -o test/test_bus_sim && ./test/test_bus_sim
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "mock_hardware.h"
#include "bus_sim.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do {                                \
    tests_run++;                                          \
    printf("  %-40s ", #fn);                              \
    mock_hardware_init();                                 \
    fn();                                                 \
    tests_passed++;                                       \
    printf("PASS\n");                                     \
} while (0)

/* ------------------------------------------------------------------ */

static void test_init_all_released(void)
{
    bus_state_t bus;
    bus_sim_init(&bus);

    /* After init, no side asserts — all wires HIGH */
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));
    assert(!bus_sim_atn_low(&bus));

    /* C64 read should see all input bits HIGH (bit6=1, bit7=1) */
    uint8_t dd00 = bus_sim_c64_read(&bus);
    assert((dd00 & (1 << 6)) != 0);  /* CLK IN = HIGH */
    assert((dd00 & (1 << 7)) != 0);  /* DATA IN = HIGH */

    /* Device side: PIND should show all lines HIGH */
    bus_sim_sync_device(&bus);
    assert((mock_PIND & (1 << 2)) != 0);  /* ATN HIGH */
    assert((mock_PIND & (1 << 3)) != 0);  /* CLK HIGH */
    assert((mock_PIND & (1 << 4)) != 0);  /* DATA HIGH */
    assert((mock_PIND & (1 << 5)) != 0);  /* RESET HIGH */
}

static void test_c64_asserts_atn(void)
{
    bus_state_t bus;
    bus_sim_init(&bus);

    /* C64 writes $DD00 with bit3 set → ATN asserted (LOW on wire) */
    bus_sim_c64_write(&bus, (1 << 3));

    assert(bus_sim_atn_low(&bus));
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));

    /* Sync so device sees ATN LOW in PIND */
    bus_sim_sync_device(&bus);
    assert((mock_PIND & (1 << 2)) == 0);  /* ATN LOW in PIND */
    assert((mock_PIND & (1 << 3)) != 0);  /* CLK still HIGH */
    assert((mock_PIND & (1 << 4)) != 0);  /* DATA still HIGH */
}

static void test_device_asserts_data(void)
{
    bus_state_t bus;
    bus_sim_init(&bus);

    /* Device asserts DATA: DDR bit4=output(1), PORT bit4=LOW(0)
     * This is what IEC_ASSERT(IEC_PIN_DATA) does. */
    mock_DDRD  |= (1 << 4);   /* PD4 = output */
    mock_PORTD &= ~(1 << 4);  /* PD4 = LOW → asserting */

    bus_sim_sync_device(&bus);

    assert(bus_sim_data_low(&bus));
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_atn_low(&bus));

    /* PIND should reflect DATA LOW */
    assert((mock_PIND & (1 << 4)) == 0);  /* DATA LOW */
    assert((mock_PIND & (1 << 3)) != 0);  /* CLK HIGH */
}

static void test_c64_reads_device_data(void)
{
    bus_state_t bus;
    bus_sim_init(&bus);

    /* Device asserts DATA */
    mock_DDRD  |= (1 << 4);
    mock_PORTD &= ~(1 << 4);
    bus_sim_sync_device(&bus);

    /* C64 reads $DD00: bit7 (DATA IN) should be 0 (wire LOW) */
    uint8_t dd00 = bus_sim_c64_read(&bus);
    assert((dd00 & (1 << 7)) == 0);  /* DATA IN = LOW */
    assert((dd00 & (1 << 6)) != 0);  /* CLK IN = HIGH (nobody asserting CLK) */
}

static void test_open_collector_or(void)
{
    bus_state_t bus;
    bus_sim_init(&bus);

    /* Both sides assert CLK */
    bus_sim_c64_write(&bus, (1 << 4));  /* C64 asserts CLK */
    mock_DDRD  |= (1 << 3);            /* Device PD3 = output */
    mock_PORTD &= ~(1 << 3);           /* Device PD3 = LOW → asserting CLK */
    bus_sim_sync_device(&bus);

    assert(bus_sim_clk_low(&bus));      /* Wire LOW */

    /* C64 releases CLK, device still asserting → wire stays LOW */
    bus_sim_c64_write(&bus, 0);         /* C64 releases all */
    bus_sim_sync_device(&bus);

    assert(bus_sim_clk_low(&bus));      /* Wire still LOW (device holds it) */

    /* Now device also releases CLK → wire goes HIGH */
    mock_DDRD  &= ~(1 << 3);           /* PD3 = input (released) */
    mock_PORTD |= (1 << 3);            /* Pull-up */
    bus_sim_sync_device(&bus);

    assert(!bus_sim_clk_low(&bus));     /* Wire HIGH (nobody asserting) */
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("bus_sim tests:\n");

    RUN_TEST(test_init_all_released);
    RUN_TEST(test_c64_asserts_atn);
    RUN_TEST(test_device_asserts_data);
    RUN_TEST(test_c64_reads_device_data);
    RUN_TEST(test_open_collector_or);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
