/*
 * trace.c — IEC protocol trace recording and comparison.
 */

#include "trace.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Initialisation                                                     */
/* ------------------------------------------------------------------ */

void trace_init(trace_log_t *log)
{
    memset(log, 0, sizeof(*log));
    log->count    = 0;
    log->prev_atn  = false;
    log->prev_clk  = false;
    log->prev_data = false;
}

/* ------------------------------------------------------------------ */
/*  Low-level append                                                   */
/* ------------------------------------------------------------------ */

void trace_add(trace_log_t *log, uint64_t cycle,
               trace_dir_t dir, trace_event_t event, uint8_t data,
               bool atn, bool clk, bool data_line)
{
    if (log->count >= TRACE_MAX_ENTRIES)
        return;   /* silently drop when full */

    trace_entry_t *e = &log->entries[log->count++];
    e->cycle    = cycle;
    e->dir      = dir;
    e->event    = event;
    e->data     = data;
    e->bus_atn  = atn  ? 1 : 0;
    e->bus_clk  = clk  ? 1 : 0;
    e->bus_data = data_line ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Edge-detecting bus-change recorder                                 */
/* ------------------------------------------------------------------ */

void trace_record_bus_change(trace_log_t *log, uint64_t cycle,
                             trace_dir_t dir,
                             bool atn, bool clk, bool data)
{
    if (atn != log->prev_atn) {
        trace_add(log, cycle, dir,
                  atn ? TRACE_ATN_ASSERT : TRACE_ATN_RELEASE,
                  0, atn, clk, data);
    }
    if (clk != log->prev_clk) {
        trace_add(log, cycle, dir,
                  clk ? TRACE_CLK_ASSERT : TRACE_CLK_RELEASE,
                  0, atn, clk, data);
    }
    if (data != log->prev_data) {
        trace_add(log, cycle, dir,
                  data ? TRACE_DATA_ASSERT : TRACE_DATA_RELEASE,
                  0, atn, clk, data);
    }

    log->prev_atn  = atn;
    log->prev_clk  = clk;
    log->prev_data = data;
}

/* ------------------------------------------------------------------ */
/*  Pretty-print                                                       */
/* ------------------------------------------------------------------ */

static const char *event_name(trace_event_t ev)
{
    switch (ev) {
    case TRACE_ATN_ASSERT:   return "ATN+";
    case TRACE_ATN_RELEASE:  return "ATN-";
    case TRACE_CLK_ASSERT:   return "CLK+";
    case TRACE_CLK_RELEASE:  return "CLK-";
    case TRACE_DATA_ASSERT:  return "DATA+";
    case TRACE_DATA_RELEASE: return "DATA-";
    case TRACE_BYTE_OUT:     return "BOUT";
    case TRACE_BYTE_IN:      return "BIN";
    case TRACE_EOI:          return "EOI";
    case TRACE_BUS_CHANGE:   return "BUS";
    }
    return "???";
}

void trace_print(const trace_log_t *log, FILE *out)
{
    fprintf(out, "%-8s %-4s %-6s %-4s  A C D\n",
            "CYCLE", "DIR", "EVENT", "DATA");
    fprintf(out, "-------- ---- ------ ----  -----\n");

    for (int i = 0; i < log->count; i++) {
        const trace_entry_t *e = &log->entries[i];
        fprintf(out, "%8llu %-4s %-6s 0x%02X  %d %d %d\n",
                (unsigned long long)e->cycle,
                e->dir == TRACE_C64 ? "C64" : "DEV",
                event_name(e->event),
                e->data,
                e->bus_atn, e->bus_clk, e->bus_data);
    }
}

/* ------------------------------------------------------------------ */
/*  Comparison                                                         */
/* ------------------------------------------------------------------ */

int trace_compare(const trace_log_t *expected, const trace_log_t *actual,
                  int cycle_tolerance)
{
    int n = expected->count < actual->count
          ? expected->count : actual->count;

    for (int i = 0; i < n; i++) {
        const trace_entry_t *ex = &expected->entries[i];
        const trace_entry_t *ac = &actual->entries[i];

        if (ex->event != ac->event)  return i + 1;
        if (ex->dir   != ac->dir)    return i + 1;
        if (ex->data  != ac->data)   return i + 1;

        if (cycle_tolerance >= 0) {
            int64_t diff = (int64_t)ac->cycle - (int64_t)ex->cycle;
            if (diff < 0) diff = -diff;
            if (diff > cycle_tolerance) return i + 1;
        }
    }

    /* Length mismatch counts as a difference at the first extra entry. */
    if (expected->count != actual->count)
        return n + 1;

    return 0;
}
