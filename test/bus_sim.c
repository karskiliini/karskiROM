/*
 * bus_sim.c — IEC bus simulator with open-collector semantics.
 *
 * Models the shared IEC bus connecting a C64 (CIA2 $DD00) to
 * a 64korppu device (ATmega328P GPIO port D).
 *
 * Open-collector rule: wire is LOW if ANY participant asserts.
 */

#include "bus_sim.h"
#include "mock_hardware.h"

/* Device GPIO pin assignments (from config.h) */
#define DEV_PIN_ATN    2   /* PD2 */
#define DEV_PIN_CLK    3   /* PD3 */
#define DEV_PIN_DATA   4   /* PD4 */
#define DEV_PIN_RESET  5   /* PD5 */

void bus_sim_init(bus_state_t *bus)
{
    bus->c64_atn  = false;
    bus->c64_clk  = false;
    bus->c64_data = false;

    bus->dev_clk   = false;
    bus->dev_data  = false;
    bus->dev_atn   = false;
    bus->dev_reset = false;

    bus->cycles = 0;

    /* Ensure mock hardware is in a clean state */
    mock_DDRD  = 0;
    mock_PORTD = 0xFF;  /* Pull-ups enabled (released) */
    mock_PIND  = 0xFF;  /* All lines HIGH initially */
}

void bus_sim_c64_write(bus_state_t *bus, uint8_t dd00_value)
{
    /* CIA2 $DD00: bit3=ATN, bit4=CLK, bit5=DATA.
     * When bit is SET, line is ASSERTED (LOW on wire). */
    bus->c64_atn  = (dd00_value & (1 << 3)) != 0;
    bus->c64_clk  = (dd00_value & (1 << 4)) != 0;
    bus->c64_data = (dd00_value & (1 << 5)) != 0;
}

uint8_t bus_sim_c64_read(bus_state_t *bus)
{
    /* $DD00 read: bit6=CLK IN, bit7=DATA IN.
     * IN bits: 0 = wire LOW (asserted), 1 = wire HIGH (released).
     * Wire is LOW if ANY side asserts. */
    bool wire_clk  = bus->c64_clk  || bus->dev_clk;
    bool wire_data = bus->c64_data || bus->dev_data;

    uint8_t val = 0;

    /* bit6: CLK input — 0 if wire LOW, 1 if wire HIGH */
    if (!wire_clk)
        val |= (1 << 6);

    /* bit7: DATA input — 0 if wire LOW, 1 if wire HIGH */
    if (!wire_data)
        val |= (1 << 7);

    return val;
}

void bus_sim_sync_device(bus_state_t *bus)
{
    /* Read device GPIO to determine what the device is asserting.
     * Open-collector: pin is asserting if DDR=output(1) AND PORT=low(0). */
    uint8_t ddr  = mock_DDRD;
    uint8_t port = mock_PORTD;

    bus->dev_clk   = (ddr & (1 << DEV_PIN_CLK))   && !(port & (1 << DEV_PIN_CLK));
    bus->dev_data  = (ddr & (1 << DEV_PIN_DATA))   && !(port & (1 << DEV_PIN_DATA));
    bus->dev_atn   = (ddr & (1 << DEV_PIN_ATN))    && !(port & (1 << DEV_PIN_ATN));
    bus->dev_reset = (ddr & (1 << DEV_PIN_RESET))  && !(port & (1 << DEV_PIN_RESET));

    /* Update mock_PIND so device reads see the actual wire state.
     * Wire is LOW (bit=0 in PIND) if ANY side asserts. */
    uint8_t pind = 0xFF;  /* Start with all HIGH */

    /* ATN: wire LOW if C64 or device asserts */
    if (bus->c64_atn || bus->dev_atn)
        pind &= ~(1 << DEV_PIN_ATN);

    /* CLK: wire LOW if C64 or device asserts */
    if (bus->c64_clk || bus->dev_clk)
        pind &= ~(1 << DEV_PIN_CLK);

    /* DATA: wire LOW if C64 or device asserts */
    if (bus->c64_data || bus->dev_data)
        pind &= ~(1 << DEV_PIN_DATA);

    /* RESET: only device side (C64 reset is a separate signal) */
    if (bus->dev_reset)
        pind &= ~(1 << DEV_PIN_RESET);

    mock_PIND = pind;
}

void bus_sim_advance(bus_state_t *bus, int cycles)
{
    bus->cycles += (uint64_t)cycles;
}

bool bus_sim_clk_low(bus_state_t *bus)
{
    return bus->c64_clk || bus->dev_clk;
}

bool bus_sim_data_low(bus_state_t *bus)
{
    return bus->c64_data || bus->dev_data;
}

bool bus_sim_atn_low(bus_state_t *bus)
{
    return bus->c64_atn || bus->dev_atn;
}
