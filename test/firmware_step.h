#ifndef FIRMWARE_STEP_H
#define FIRMWARE_STEP_H

#include <stdint.h>
#include <stdbool.h>
#include "bus_sim.h"
#include "trace.h"

/*
 * firmware_step — Adapts 64korppu's iec_service() for cooperative
 *                 stepping alongside the C64 harness.
 *
 * Each call to firmware_step() syncs bus state, calls iec_service()
 * once, then records any bus line changes to the trace log.
 *
 * NOTE: This file will not compile until vendor adaptation (Task 14)
 *       resolves AVR-specific dependencies in the firmware code.
 */

typedef struct {
    bus_state_t *bus;
    trace_log_t *trace;
    bool initialized;
} firmware_state_t;

void firmware_init(firmware_state_t *fw, bus_state_t *bus, trace_log_t *trace);
void firmware_step(firmware_state_t *fw, int cycles);

#endif /* FIRMWARE_STEP_H */
