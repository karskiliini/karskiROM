/*
 * firmware_step.c — Cooperative stepping wrapper for 64korppu firmware.
 *
 * Adapts the vendor iec_service() loop for use in the test harness,
 * syncing IEC bus state before and after each service call and
 * recording any bus line changes to the trace log.
 *
 * NOTE: This file will NOT compile until vendor adaptation (Task 14)
 *       provides host-compatible wrappers for the AVR-specific
 *       firmware code.  It is included now to define the interface.
 */

#include "firmware_step.h"
#include "mock_hardware.h"
#include "iec_protocol.h"
#include "cbm_dos.h"
#include "fastload.h"
#include "fastload_jiffydos.h"
#include "compress_proto.h"

void firmware_init(firmware_state_t *fw, bus_state_t *bus, trace_log_t *trace)
{
    fw->bus   = bus;
    fw->trace = trace;

    mock_hardware_init();
    iec_init(8);
    cbm_dos_init();
    fastload_init();
    fastload_jiffydos_register();
    compress_proto_init();

    fw->initialized = true;
}

void firmware_step(firmware_state_t *fw, int cycles)
{
    (void)cycles;

    /* Sync device-side GPIO from bus state before service call */
    bus_sim_sync_device(fw->bus);

    bool prev_clk  = bus_sim_clk_low(fw->bus);
    bool prev_data = bus_sim_data_low(fw->bus);

    /* Run one iteration of the firmware's IEC service loop */
    iec_service();

    /* Sync again — iec_service() may have changed GPIO outputs */
    bus_sim_sync_device(fw->bus);

    bool new_clk  = bus_sim_clk_low(fw->bus);
    bool new_data = bus_sim_data_low(fw->bus);

    if (new_clk != prev_clk || new_data != prev_data) {
        if (fw->trace) {
            trace_record_bus_change(fw->trace, fw->bus->cycles,
                                    TRACE_DEV,
                                    bus_sim_atn_low(fw->bus),
                                    new_clk, new_data);
        }
    }
}
