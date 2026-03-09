/*
 * test_trace.c — Unit tests for IEC protocol trace system.
 *
 * Build & run:
 *   cc -Wall -I test test/test_trace.c test/trace.c -o test/test_trace \
 *       && ./test/test_trace
 */

#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            fprintf(stderr, "  FAIL: %s  (%s:%d)\n",           \
                    (msg), __FILE__, __LINE__);                 \
            return 1;                                           \
        }                                                       \
    } while (0)

#define RUN(fn)                                                 \
    do {                                                        \
        tests_run++;                                            \
        printf("  %-40s ", #fn);                                \
        if (fn() == 0) { tests_passed++; printf("OK\n"); }     \
    } while (0)

/* ------------------------------------------------------------------ */
/*  1. trace_init — empty trace after init                             */
/* ------------------------------------------------------------------ */

static int test_trace_init(void)
{
    trace_log_t log;
    trace_init(&log);

    ASSERT(log.count == 0,       "count must be 0 after init");
    ASSERT(log.prev_atn  == false, "prev_atn must be false");
    ASSERT(log.prev_clk  == false, "prev_clk must be false");
    ASSERT(log.prev_data == false, "prev_data must be false");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  2. trace_add — add entries, verify count                           */
/* ------------------------------------------------------------------ */

static int test_trace_add(void)
{
    trace_log_t log;
    trace_init(&log);

    trace_add(&log, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    ASSERT(log.count == 1, "count must be 1 after one add");

    trace_add(&log, 200, TRACE_DEV, TRACE_DATA_ASSERT, 0x42,
              true, false, true);
    ASSERT(log.count == 2, "count must be 2 after two adds");

    /* Verify first entry fields */
    ASSERT(log.entries[0].cycle == 100,                "entry 0 cycle");
    ASSERT(log.entries[0].dir   == TRACE_C64,          "entry 0 dir");
    ASSERT(log.entries[0].event == TRACE_ATN_ASSERT,   "entry 0 event");
    ASSERT(log.entries[0].data  == 0,                  "entry 0 data");
    ASSERT(log.entries[0].bus_atn  == 1,               "entry 0 bus_atn");
    ASSERT(log.entries[0].bus_clk  == 0,               "entry 0 bus_clk");
    ASSERT(log.entries[0].bus_data == 0,               "entry 0 bus_data");

    /* Verify second entry */
    ASSERT(log.entries[1].cycle == 200,                "entry 1 cycle");
    ASSERT(log.entries[1].dir   == TRACE_DEV,          "entry 1 dir");
    ASSERT(log.entries[1].event == TRACE_DATA_ASSERT,  "entry 1 event");
    ASSERT(log.entries[1].data  == 0x42,               "entry 1 data");
    ASSERT(log.entries[1].bus_data == 1,               "entry 1 bus_data");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  3. trace_record_edges — only edges produce events                  */
/* ------------------------------------------------------------------ */

static int test_trace_record_edges(void)
{
    trace_log_t log;
    trace_init(&log);

    /* All lines start false.  Assert ATN only. */
    trace_record_bus_change(&log, 10, TRACE_C64, true, false, false);
    ASSERT(log.count == 1, "one edge => one event");
    ASSERT(log.entries[0].event == TRACE_ATN_ASSERT, "ATN assert edge");

    /* Same state again — no new events. */
    trace_record_bus_change(&log, 20, TRACE_C64, true, false, false);
    ASSERT(log.count == 1, "no change => no new events");

    /* Assert CLK and DATA simultaneously. */
    trace_record_bus_change(&log, 30, TRACE_C64, true, true, true);
    ASSERT(log.count == 3, "two edges => two new events");
    ASSERT(log.entries[1].event == TRACE_CLK_ASSERT,  "CLK assert edge");
    ASSERT(log.entries[2].event == TRACE_DATA_ASSERT, "DATA assert edge");

    /* Release ATN only. */
    trace_record_bus_change(&log, 40, TRACE_C64, false, true, true);
    ASSERT(log.count == 4, "one release edge");
    ASSERT(log.entries[3].event == TRACE_ATN_RELEASE, "ATN release edge");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  4. trace_compare_match — identical traces compare equal            */
/* ------------------------------------------------------------------ */

static int test_trace_compare_match(void)
{
    trace_log_t a, b;
    trace_init(&a);
    trace_init(&b);

    /* Build identical traces */
    trace_add(&a, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&a, 200, TRACE_DEV, TRACE_CLK_ASSERT, 0,
              true, true, false);
    trace_add(&a, 300, TRACE_C64, TRACE_BYTE_OUT, 0x28,
              true, true, false);

    trace_add(&b, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&b, 200, TRACE_DEV, TRACE_CLK_ASSERT, 0,
              true, true, false);
    trace_add(&b, 300, TRACE_C64, TRACE_BYTE_OUT, 0x28,
              true, true, false);

    ASSERT(trace_compare(&a, &b, 0) == 0,
           "identical traces must match exactly");

    /* Timing tolerance: b's cycles shifted by 5, tolerance = 10 */
    trace_init(&b);
    trace_add(&b, 105, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&b, 195, TRACE_DEV, TRACE_CLK_ASSERT, 0,
              true, true, false);
    trace_add(&b, 305, TRACE_C64, TRACE_BYTE_OUT, 0x28,
              true, true, false);

    ASSERT(trace_compare(&a, &b, 10) == 0,
           "within tolerance must still match");

    /* Ignore timing when tolerance < 0 */
    trace_init(&b);
    trace_add(&b, 9999, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&b, 9999, TRACE_DEV, TRACE_CLK_ASSERT, 0,
              true, true, false);
    trace_add(&b, 9999, TRACE_C64, TRACE_BYTE_OUT, 0x28,
              true, true, false);

    ASSERT(trace_compare(&a, &b, -1) == 0,
           "negative tolerance ignores timing");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  5. trace_compare_mismatch — different traces return mismatch idx   */
/* ------------------------------------------------------------------ */

static int test_trace_compare_mismatch(void)
{
    trace_log_t a, b;
    trace_init(&a);
    trace_init(&b);

    /* Same first entry, different second */
    trace_add(&a, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&a, 200, TRACE_DEV, TRACE_CLK_ASSERT, 0,
              true, true, false);

    trace_add(&b, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&b, 200, TRACE_DEV, TRACE_DATA_ASSERT, 0,   /* different event */
              true, false, true);

    ASSERT(trace_compare(&a, &b, -1) == 2,
           "mismatch at entry 2 (1-based)");

    /* Length mismatch */
    trace_init(&b);
    trace_add(&b, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    /* b has only 1 entry, a has 2 */
    ASSERT(trace_compare(&a, &b, -1) == 2,
           "length mismatch returns n+1");

    /* Timing mismatch */
    trace_init(&a);
    trace_init(&b);
    trace_add(&a, 100, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);
    trace_add(&b, 200, TRACE_C64, TRACE_ATN_ASSERT, 0,
              true, false, false);

    ASSERT(trace_compare(&a, &b, 10) == 1,
           "timing beyond tolerance is a mismatch");

    /* Data byte mismatch */
    trace_init(&a);
    trace_init(&b);
    trace_add(&a, 100, TRACE_C64, TRACE_BYTE_OUT, 0xAA,
              true, false, false);
    trace_add(&b, 100, TRACE_C64, TRACE_BYTE_OUT, 0x55,
              true, false, false);

    ASSERT(trace_compare(&a, &b, -1) == 1,
           "data byte mismatch at entry 1");

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("test_trace\n");

    RUN(test_trace_init);
    RUN(test_trace_add);
    RUN(test_trace_record_edges);
    RUN(test_trace_compare_match);
    RUN(test_trace_compare_mismatch);

    printf("\n%d / %d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
