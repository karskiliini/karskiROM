/*
 * trace.h — IEC protocol trace recording and comparison.
 *
 * Records bus-level events (ATN/CLK/DATA edges, byte transfers)
 * with cycle timestamps.  Two traces can be compared to verify
 * that an implementation matches expected protocol behaviour.
 */

#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    TRACE_C64,   /* C64 changed bus */
    TRACE_DEV    /* Device changed bus */
} trace_dir_t;

typedef enum {
    TRACE_ATN_ASSERT,
    TRACE_ATN_RELEASE,
    TRACE_CLK_ASSERT,
    TRACE_CLK_RELEASE,
    TRACE_DATA_ASSERT,
    TRACE_DATA_RELEASE,
    TRACE_BYTE_OUT,
    TRACE_BYTE_IN,
    TRACE_EOI,
    TRACE_BUS_CHANGE
} trace_event_t;

typedef struct {
    uint64_t cycle;
    trace_dir_t dir;
    trace_event_t event;
    uint8_t data;
    uint8_t bus_atn;
    uint8_t bus_clk;
    uint8_t bus_data;
} trace_entry_t;

#define TRACE_MAX_ENTRIES 4096

typedef struct {
    trace_entry_t entries[TRACE_MAX_ENTRIES];
    int count;
    bool prev_atn, prev_clk, prev_data;
} trace_log_t;

void trace_init(trace_log_t *log);
void trace_record_bus_change(trace_log_t *log, uint64_t cycle,
                             trace_dir_t dir,
                             bool atn, bool clk, bool data);
void trace_add(trace_log_t *log, uint64_t cycle,
               trace_dir_t dir, trace_event_t event, uint8_t data,
               bool atn, bool clk, bool data_line);
void trace_print(const trace_log_t *log, FILE *out);
int trace_compare(const trace_log_t *expected, const trace_log_t *actual,
                  int cycle_tolerance);

#endif /* TRACE_H */
