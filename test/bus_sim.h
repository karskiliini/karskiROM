#ifndef BUS_SIM_H
#define BUS_SIM_H

#include <stdint.h>
#include <stdbool.h>

/* IEC bus lines — active-low open-collector semantics.
 * true = asserted (LOW on wire), false = released (HIGH on wire).
 * Either side can assert; line is LOW if ANY side asserts. */
typedef struct {
    /* C64 outputs (directly mapped from CIA2 $DD00) */
    bool c64_atn;       /* $DD00 bit 3 */
    bool c64_clk;       /* $DD00 bit 4 */
    bool c64_data;      /* $DD00 bit 5 */

    /* Device outputs (mapped from 64korppu GPIO) */
    bool dev_clk;       /* IEC_PIN_CLK (PD3) */
    bool dev_data;      /* IEC_PIN_DATA (PD4) */
    bool dev_atn;       /* device reads only, included for completeness */
    bool dev_reset;     /* IEC_PIN_RESET (PD5) */

    /* Cycle counter */
    uint64_t cycles;
} bus_state_t;

void bus_sim_init(bus_state_t *bus);
void bus_sim_c64_write(bus_state_t *bus, uint8_t dd00_value);
uint8_t bus_sim_c64_read(bus_state_t *bus);
void bus_sim_sync_device(bus_state_t *bus);
void bus_sim_advance(bus_state_t *bus, int cycles);
bool bus_sim_clk_low(bus_state_t *bus);
bool bus_sim_data_low(bus_state_t *bus);
bool bus_sim_atn_low(bus_state_t *bus);

#endif
