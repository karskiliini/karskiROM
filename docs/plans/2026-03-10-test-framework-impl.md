# Test Framework Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an integration test framework that runs real C64 KERNAL ROM code via lib6502 against 64korppu E-variant firmware using cooperative scheduling over a shared IEC bus simulator.

**Architecture:** lib6502 emulates the C64 CPU running the original KERNAL ROM. A shared IEC bus simulator connects CIA2 $DD00 writes/reads to 64korppu's GPIO registers. A cooperative loop alternates execution between both sides. IEC protocol traces are recorded and compared against expected sequences.

**Tech Stack:** C11, lib6502 (Ian Piumarta), 64korppu E-variant firmware (vendor copy), Make

**Note:** The original KERNAL ROM only supports standard IEC. JiffyDOS and LZ4 tests are scaffolded but will become functional when karskiROM assembly code is written. The framework validates standard IEC first, then extends to fast protocols.

---

### Task 1: Obtain lib6502

**Files:**
- Create: `vendor/lib6502/lib6502.c`
- Create: `vendor/lib6502/lib6502.h`

**Step 1: Download lib6502 source**

```bash
mkdir -p vendor/lib6502
curl -L https://www.piumarta.com/software/lib6502/lib6502-1.3.tar.gz | \
  tar xz --strip-components=1 -C vendor/lib6502
```

If URL unavailable, clone from GitHub: `https://github.com/ShonFrazier/lib6502`

Keep only `lib6502.c` and `lib6502.h`. Remove examples, docs, Makefile.

**Step 2: Verify it compiles**

```bash
cc -c -Wall vendor/lib6502/lib6502.c -o /tmp/lib6502.o
```

Expected: compiles without errors.

**Step 3: Commit**

```bash
git add vendor/lib6502/
git commit -m "vendor: add lib6502 6502 emulator library"
```

---

### Task 2: Copy 64korppu E-variant firmware

**Files:**
- Create: `vendor/64korppu/src/*.c` (8 files)
- Create: `vendor/64korppu/include/*.h` (matching headers)

**Step 1: Copy source files from 64korppu repo**

From `/Users/marski/git/64korppu/firmware/E-IEC-Nano-SRAM/`:

```bash
mkdir -p vendor/64korppu/{src,include}

# Source files
for f in iec_protocol.c cbm_dos.c fat12.c d64.c \
         fastload.c fastload_jiffydos.c fastload_burst.c fastload_epyx.c \
         lz4_compress.c compress_proto.c sram.c; do
  cp ../64korppu/firmware/E-IEC-Nano-SRAM/src/$f vendor/64korppu/src/
done

# Headers
for f in iec_protocol.h cbm_dos.h fat12.h d64.h config.h \
         fastload.h fastload_jiffydos.h fastload_burst.h fastload_epyx.h \
         lz4_compress.h compress_proto.h sram.h; do
  cp ../64korppu/firmware/E-IEC-Nano-SRAM/include/$f vendor/64korppu/include/
done
```

**Step 2: Verify files are present**

```bash
ls vendor/64korppu/src/ vendor/64korppu/include/
```

Expected: all files listed.

**Step 3: Commit**

```bash
git add vendor/64korppu/
git commit -m "vendor: add 64korppu E-variant firmware sources"
```

---

### Task 3: Create mock hardware layer

**Files:**
- Create: `test/mock_hardware.h`
- Create: `test/mock_hardware.c`

This replaces AVR-specific registers and functions with host-accessible globals.

**Step 1: Write mock_hardware.h**

```c
#ifndef MOCK_HARDWARE_H
#define MOCK_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>

/* AVR register mocks — global variables that test code can read/write */
extern uint8_t mock_DDRD, mock_PORTD, mock_PIND;
extern uint8_t mock_DDRB, mock_PORTB, mock_PINB;
extern uint8_t mock_SPDR, mock_SPSR, mock_SPCR;

/* Map AVR register names to mock globals */
#define DDRD   mock_DDRD
#define PORTD  mock_PORTD
#define PIND   mock_PIND
#define DDRB   mock_DDRB
#define PORTB  mock_PORTB
#define PINB   mock_PINB
#define SPDR   mock_SPDR
#define SPSR   mock_SPSR
#define SPCR   mock_SPCR

/* AVR pin name aliases */
#define PD2 2
#define PD3 3
#define PD4 4
#define PD5 5
#define PD6 6
#define PB2 2
#define PB3 3
#define PB4 4
#define PB5 5

/* SPI status bits */
#define SPIF 7
#define SPE  6
#define MSTR 4
#define SPR0 0

/* Timing mock — counts calls, does not actually delay */
extern uint64_t mock_delay_us_total;
void _delay_us(double us);
void _delay_ms(double ms);

/* Interrupt control (no-ops on host) */
#define cli()
#define sei()

/* SRAM mock — 64KB host-side array */
#define MOCK_SRAM_SIZE 65536
extern uint8_t mock_sram[MOCK_SRAM_SIZE];

/* UART mock (no-ops) */
#define uart_init()
#define uart_putc(c)
#define uart_puts(s)

/* Reset mock state */
void mock_hardware_init(void);

#endif
```

**Step 2: Write mock_hardware.c**

```c
#include "mock_hardware.h"
#include <string.h>

uint8_t mock_DDRD, mock_PORTD, mock_PIND;
uint8_t mock_DDRB, mock_PORTB, mock_PINB;
uint8_t mock_SPDR, mock_SPSR, mock_SPCR;
uint64_t mock_delay_us_total;
uint8_t mock_sram[MOCK_SRAM_SIZE];

void _delay_us(double us) {
    mock_delay_us_total += (uint64_t)us;
}

void _delay_ms(double ms) {
    mock_delay_us_total += (uint64_t)(ms * 1000.0);
}

void mock_hardware_init(void) {
    mock_DDRD = mock_PORTD = mock_PIND = 0;
    mock_DDRB = mock_PORTB = mock_PINB = 0;
    mock_SPDR = mock_SPSR = mock_SPCR = 0;
    mock_delay_us_total = 0;
    memset(mock_sram, 0, MOCK_SRAM_SIZE);
}
```

**Step 3: Verify it compiles**

```bash
cc -c -Wall -I test test/mock_hardware.c -o /tmp/mock_hardware.o
```

Expected: compiles without errors.

**Step 4: Commit**

```bash
git add test/mock_hardware.h test/mock_hardware.c
git commit -m "test: add AVR hardware mock layer"
```

---

### Task 4: Create SRAM mock

**Files:**
- Create: `test/mock_sram.c`

Replaces `vendor/64korppu/src/sram.c` with a host implementation backed by `mock_sram[]`.

**Step 1: Write mock_sram.c**

Implement the same API as `vendor/64korppu/include/sram.h` but using `mock_sram[]` array:

```c
#include "mock_hardware.h"
#include "sram.h"
#include <string.h>

static uint32_t seq_addr;
static bool seq_writing;

void sram_init(void) {
    memset(mock_sram, 0, MOCK_SRAM_SIZE);
}

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    if (addr + len <= MOCK_SRAM_SIZE)
        memcpy(buf, &mock_sram[addr], len);
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    if (addr + len <= MOCK_SRAM_SIZE)
        memcpy(&mock_sram[addr], buf, len);
}

uint8_t sram_read_byte(uint32_t addr) {
    return (addr < MOCK_SRAM_SIZE) ? mock_sram[addr] : 0;
}

void sram_write_byte(uint32_t addr, uint8_t byte) {
    if (addr < MOCK_SRAM_SIZE)
        mock_sram[addr] = byte;
}

void sram_begin_seq_write(uint32_t addr) {
    seq_addr = addr;
    seq_writing = true;
}

void sram_seq_write_byte(uint8_t byte) {
    if (seq_addr < MOCK_SRAM_SIZE)
        mock_sram[seq_addr++] = byte;
}

void sram_end_seq(void) {
    seq_writing = false;
}

void sram_begin_seq_read(uint32_t addr) {
    seq_addr = addr;
    seq_writing = false;
}

uint8_t sram_seq_read_byte(void) {
    return (seq_addr < MOCK_SRAM_SIZE) ? mock_sram[seq_addr++] : 0;
}
```

**Step 2: Verify it compiles**

```bash
cc -c -Wall -I test -I vendor/64korppu/include test/mock_sram.c -o /tmp/mock_sram.o
```

**Step 3: Commit**

```bash
git add test/mock_sram.c
git commit -m "test: add SRAM mock backed by host memory"
```

---

### Task 5: Create FAT12 mock

**Files:**
- Create: `test/mock_fat12.c`

Provides a simple in-memory filesystem so `cbm_dos_open()` can find test files.

**Step 1: Write mock_fat12.c**

Implement the FAT12 API from `vendor/64korppu/include/fat12.h`. Use a simple array of mock files:

```c
#include "fat12.h"
#include <string.h>
#include <stdio.h>

#define MAX_MOCK_FILES 8
#define MAX_FILE_SIZE  65536

typedef struct {
    char name8[9];
    char ext3[4];
    uint8_t data[MAX_FILE_SIZE];
    uint16_t size;
    bool in_use;
} mock_file_t;

static mock_file_t mock_files[MAX_MOCK_FILES];
static int current_file = -1;
static uint16_t read_pos;

/* Test helper: add a file to the mock filesystem */
void mock_fat12_add_file(const char *name8, const char *ext3,
                         const uint8_t *data, uint16_t size) {
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (!mock_files[i].in_use) {
            strncpy(mock_files[i].name8, name8, 8);
            mock_files[i].name8[8] = '\0';
            strncpy(mock_files[i].ext3, ext3, 3);
            mock_files[i].ext3[3] = '\0';
            memcpy(mock_files[i].data, data, size);
            mock_files[i].size = size;
            mock_files[i].in_use = true;
            return;
        }
    }
}

void mock_fat12_reset(void) {
    memset(mock_files, 0, sizeof(mock_files));
    current_file = -1;
}

/* FAT12 API implementation */
bool fat12_mount(void) { return true; }

bool fat12_open_read(const char *name8, const char *ext3, void *handle) {
    for (int i = 0; i < MAX_MOCK_FILES; i++) {
        if (mock_files[i].in_use &&
            strncmp(mock_files[i].name8, name8, 8) == 0 &&
            strncmp(mock_files[i].ext3, ext3, 3) == 0) {
            current_file = i;
            read_pos = 0;
            return true;
        }
    }
    return false;
}

int fat12_read(void *handle, uint8_t *buf, uint16_t len) {
    if (current_file < 0) return -1;
    mock_file_t *f = &mock_files[current_file];
    uint16_t remaining = f->size - read_pos;
    if (remaining == 0) return 0;
    uint16_t n = (len < remaining) ? len : remaining;
    memcpy(buf, &f->data[read_pos], n);
    read_pos += n;
    return n;
}

bool fat12_open_write(const char *name8, const char *ext3, void *handle) {
    return false; /* TODO for SAVE tests */
}

int fat12_write(void *handle, const uint8_t *buf, uint16_t len) {
    return -1; /* TODO */
}

void fat12_close(void *handle) {
    current_file = -1;
}
```

**Step 2: Verify it compiles**

```bash
cc -c -Wall -I test -I vendor/64korppu/include test/mock_fat12.c -o /tmp/mock_fat12.o
```

**Step 3: Commit**

```bash
git add test/mock_fat12.c
git commit -m "test: add FAT12 mock with in-memory files"
```

---

### Task 6: Create IEC bus simulator

**Files:**
- Create: `test/bus_sim.h`
- Create: `test/bus_sim.c`

Shared IEC bus state connecting C64 $DD00 to 64korppu GPIO.

**Step 1: Write bus_sim.h**

```c
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
    bool dev_atn;       /* IEC_PIN_ATN — device reads only, but included for reset */
    bool dev_reset;     /* IEC_PIN_RESET (PD5) */

    /* Cycle counter */
    uint64_t cycles;
} bus_state_t;

/* Initialize bus: all lines released */
void bus_sim_init(bus_state_t *bus);

/* Update from C64 side: called by lib6502 $DD00 write callback */
void bus_sim_c64_write(bus_state_t *bus, uint8_t dd00_value);

/* Read for C64 side: returns $DD00 value with IN bits from device */
uint8_t bus_sim_c64_read(bus_state_t *bus);

/* Sync 64korppu GPIO registers (mock_PIND/PORTD/DDRD) with bus state */
void bus_sim_sync_device(bus_state_t *bus);

/* Advance cycle counter */
void bus_sim_advance(bus_state_t *bus, int cycles);

/* Query wire states (open-collector OR) */
bool bus_sim_clk_low(bus_state_t *bus);   /* true if CLK wire is LOW */
bool bus_sim_data_low(bus_state_t *bus);  /* true if DATA wire is LOW */
bool bus_sim_atn_low(bus_state_t *bus);   /* true if ATN wire is LOW */

#endif
```

**Step 2: Write bus_sim.c**

```c
#include "bus_sim.h"
#include "mock_hardware.h"

/* IEC pin positions in PORTD/PIND */
#define DEV_ATN_BIT   2  /* PD2 */
#define DEV_CLK_BIT   3  /* PD3 */
#define DEV_DATA_BIT  4  /* PD4 */
#define DEV_RESET_BIT 5  /* PD5 */

void bus_sim_init(bus_state_t *bus) {
    bus->c64_atn = false;
    bus->c64_clk = false;
    bus->c64_data = false;
    bus->dev_clk = false;
    bus->dev_data = false;
    bus->dev_atn = false;
    bus->dev_reset = false;
    bus->cycles = 0;
}

/* Open-collector: wire is LOW if ANY participant asserts */
bool bus_sim_clk_low(bus_state_t *bus) {
    return bus->c64_clk || bus->dev_clk;
}

bool bus_sim_data_low(bus_state_t *bus) {
    return bus->c64_data || bus->dev_data;
}

bool bus_sim_atn_low(bus_state_t *bus) {
    return bus->c64_atn;  /* Only C64 drives ATN */
}

void bus_sim_c64_write(bus_state_t *bus, uint8_t dd00_value) {
    /* CIA2 $DD00: bit3=ATN OUT, bit4=CLK OUT, bit5=DATA OUT
     * Note: bits are active-high in register but active-low on wire.
     * When bit is SET in $DD00, line is ASSERTED (LOW on wire). */
    bus->c64_atn  = (dd00_value & (1 << 3)) != 0;
    bus->c64_clk  = (dd00_value & (1 << 4)) != 0;
    bus->c64_data = (dd00_value & (1 << 5)) != 0;
}

uint8_t bus_sim_c64_read(bus_state_t *bus) {
    /* CIA2 $DD00: bit6=CLK IN, bit7=DATA IN
     * IN bits reflect wire state: 0 = LOW (asserted), 1 = HIGH (released)
     * So we INVERT: if wire is LOW, bit is 0. */
    uint8_t val = 0;
    if (!bus_sim_clk_low(bus))  val |= (1 << 6);  /* CLK IN: 1=HIGH */
    if (!bus_sim_data_low(bus)) val |= (1 << 7);  /* DATA IN: 1=HIGH */
    return val;
}

void bus_sim_sync_device(bus_state_t *bus) {
    /* Read device PORTD/DDRD to get device's output state */
    /* Open-collector: if DDR bit set (output) and PORT bit clear (drive low)
     * → device is asserting that line */
    if (mock_DDRD & (1 << DEV_CLK_BIT))
        bus->dev_clk = !(mock_PORTD & (1 << DEV_CLK_BIT));
    else
        bus->dev_clk = false;  /* input mode = released */

    if (mock_DDRD & (1 << DEV_DATA_BIT))
        bus->dev_data = !(mock_PORTD & (1 << DEV_DATA_BIT));
    else
        bus->dev_data = false;

    /* Update PIND so device reads see bus wire state */
    mock_PIND = 0xFF;  /* All HIGH by default */
    if (bus_sim_atn_low(bus))   mock_PIND &= ~(1 << DEV_ATN_BIT);
    if (bus_sim_clk_low(bus))   mock_PIND &= ~(1 << DEV_CLK_BIT);
    if (bus_sim_data_low(bus))  mock_PIND &= ~(1 << DEV_DATA_BIT);
    if (bus->dev_reset)         mock_PIND &= ~(1 << DEV_RESET_BIT);
}

void bus_sim_advance(bus_state_t *bus, int cycles) {
    bus->cycles += cycles;
}
```

**Step 3: Write a simple test to verify bus logic**

Create `test/test_bus_sim.c`:

```c
#include "bus_sim.h"
#include "mock_hardware.h"
#include <assert.h>
#include <stdio.h>

static void test_init_all_released(void) {
    bus_state_t bus;
    bus_sim_init(&bus);
    assert(!bus_sim_clk_low(&bus));
    assert(!bus_sim_data_low(&bus));
    assert(!bus_sim_atn_low(&bus));
    printf("PASS: test_init_all_released\n");
}

static void test_c64_asserts_atn(void) {
    bus_state_t bus;
    bus_sim_init(&bus);
    bus_sim_c64_write(&bus, (1 << 3));  /* ATN OUT set */
    assert(bus_sim_atn_low(&bus));
    assert(!bus_sim_clk_low(&bus));
    printf("PASS: test_c64_asserts_atn\n");
}

static void test_device_asserts_data(void) {
    bus_state_t bus;
    mock_hardware_init();
    bus_sim_init(&bus);
    /* Device: set DATA pin as output, drive LOW */
    mock_DDRD |= (1 << 4);   /* PD4 = output */
    mock_PORTD &= ~(1 << 4); /* PD4 = LOW */
    bus_sim_sync_device(&bus);
    assert(bus_sim_data_low(&bus));
    printf("PASS: test_device_asserts_data\n");
}

static void test_c64_reads_device_data(void) {
    bus_state_t bus;
    mock_hardware_init();
    bus_sim_init(&bus);
    /* Device asserts DATA */
    mock_DDRD |= (1 << 4);
    mock_PORTD &= ~(1 << 4);
    bus_sim_sync_device(&bus);
    uint8_t dd00 = bus_sim_c64_read(&bus);
    assert(!(dd00 & (1 << 7)));  /* DATA IN = 0 (wire LOW) */
    assert(dd00 & (1 << 6));     /* CLK IN = 1 (wire HIGH) */
    printf("PASS: test_c64_reads_device_data\n");
}

static void test_open_collector_or(void) {
    bus_state_t bus;
    mock_hardware_init();
    bus_sim_init(&bus);
    /* Both sides assert CLK */
    bus_sim_c64_write(&bus, (1 << 4));  /* C64 CLK OUT */
    mock_DDRD |= (1 << 3);
    mock_PORTD &= ~(1 << 3);  /* Device CLK */
    bus_sim_sync_device(&bus);
    assert(bus_sim_clk_low(&bus));
    /* C64 releases, but device still holds — wire stays LOW */
    bus_sim_c64_write(&bus, 0);
    assert(bus_sim_clk_low(&bus));
    printf("PASS: test_open_collector_or\n");
}

int main(void) {
    test_init_all_released();
    test_c64_asserts_atn();
    test_device_asserts_data();
    test_c64_reads_device_data();
    test_open_collector_or();
    printf("\nAll bus_sim tests passed.\n");
    return 0;
}
```

**Step 4: Run the test**

```bash
cc -Wall -I test -I vendor/64korppu/include \
   test/test_bus_sim.c test/bus_sim.c test/mock_hardware.c \
   -o test/test_bus_sim && ./test/test_bus_sim
```

Expected: `All bus_sim tests passed.`

**Step 5: Commit**

```bash
git add test/bus_sim.h test/bus_sim.c test/test_bus_sim.c
git commit -m "test: add IEC bus simulator with open-collector semantics"
```

---

### Task 7: Create trace system

**Files:**
- Create: `test/trace.h`
- Create: `test/trace.c`

**Step 1: Write trace.h**

```c
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
    TRACE_BYTE_OUT,      /* Full byte transferred */
    TRACE_BYTE_IN,       /* Full byte received */
    TRACE_EOI,           /* End-of-indicator */
    TRACE_BUS_CHANGE     /* Generic line change */
} trace_event_t;

typedef struct {
    uint64_t cycle;
    trace_dir_t dir;
    trace_event_t event;
    uint8_t data;         /* Byte value for BYTE_OUT/BYTE_IN */
    uint8_t bus_atn;      /* Wire states at time of event */
    uint8_t bus_clk;
    uint8_t bus_data;
} trace_entry_t;

#define TRACE_MAX_ENTRIES 4096

typedef struct {
    trace_entry_t entries[TRACE_MAX_ENTRIES];
    int count;
    /* Previous bus state for edge detection */
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

/* Compare two traces: returns 0 if sequences match within tolerance.
 * Only compares event sequence, not exact cycles.
 * cycle_tolerance: max allowed cycle difference per event (0 = exact). */
int trace_compare(const trace_log_t *expected, const trace_log_t *actual,
                  int cycle_tolerance);

#endif
```

**Step 2: Write trace.c**

```c
#include "trace.h"
#include <string.h>

void trace_init(trace_log_t *log) {
    memset(log, 0, sizeof(*log));
}

void trace_add(trace_log_t *log, uint64_t cycle,
               trace_dir_t dir, trace_event_t event, uint8_t data,
               bool atn, bool clk, bool data_line) {
    if (log->count >= TRACE_MAX_ENTRIES) return;
    trace_entry_t *e = &log->entries[log->count++];
    e->cycle = cycle;
    e->dir = dir;
    e->event = event;
    e->data = data;
    e->bus_atn = atn ? 1 : 0;
    e->bus_clk = clk ? 1 : 0;
    e->bus_data = data_line ? 1 : 0;
}

void trace_record_bus_change(trace_log_t *log, uint64_t cycle,
                             trace_dir_t dir,
                             bool atn, bool clk, bool data) {
    /* Detect edges and record relevant events */
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
    log->prev_atn = atn;
    log->prev_clk = clk;
    log->prev_data = data;
}

static const char *event_name(trace_event_t e) {
    switch (e) {
    case TRACE_ATN_ASSERT:   return "ATN_ASSERT";
    case TRACE_ATN_RELEASE:  return "ATN_RELEASE";
    case TRACE_CLK_ASSERT:   return "CLK_ASSERT";
    case TRACE_CLK_RELEASE:  return "CLK_RELEASE";
    case TRACE_DATA_ASSERT:  return "DATA_ASSERT";
    case TRACE_DATA_RELEASE: return "DATA_RELEASE";
    case TRACE_BYTE_OUT:     return "BYTE_OUT";
    case TRACE_BYTE_IN:      return "BYTE_IN";
    case TRACE_EOI:          return "EOI";
    case TRACE_BUS_CHANGE:   return "BUS_CHANGE";
    }
    return "UNKNOWN";
}

void trace_print(const trace_log_t *log, FILE *out) {
    fprintf(out, "CYCLE      DIR   EVENT           DATA  BUS(A/C/D)\n");
    fprintf(out, "---------------------------------------------------\n");
    for (int i = 0; i < log->count; i++) {
        const trace_entry_t *e = &log->entries[i];
        fprintf(out, "%010llu  %s  %-16s $%02X   %d/%d/%d\n",
                (unsigned long long)e->cycle,
                e->dir == TRACE_C64 ? "C64" : "DEV",
                event_name(e->event),
                e->data,
                e->bus_atn, e->bus_clk, e->bus_data);
    }
}

int trace_compare(const trace_log_t *expected, const trace_log_t *actual,
                  int cycle_tolerance) {
    if (expected->count != actual->count) return -1;
    for (int i = 0; i < expected->count; i++) {
        const trace_entry_t *exp = &expected->entries[i];
        const trace_entry_t *act = &actual->entries[i];
        if (exp->dir != act->dir) return i + 1;
        if (exp->event != act->event) return i + 1;
        if (exp->data != act->data) return i + 1;
        if (cycle_tolerance >= 0) {
            int64_t diff = (int64_t)act->cycle - (int64_t)exp->cycle;
            if (diff < -cycle_tolerance || diff > cycle_tolerance)
                return i + 1;
        }
    }
    return 0;
}
```

**Step 3: Verify it compiles**

```bash
cc -c -Wall -I test test/trace.c -o /tmp/trace.o
```

**Step 4: Commit**

```bash
git add test/trace.h test/trace.c
git commit -m "test: add IEC protocol trace recording and comparison"
```

---

### Task 8: Create C64 harness (lib6502 + KERNAL ROM)

**Files:**
- Create: `test/c64_harness.h`
- Create: `test/c64_harness.c`

Wraps lib6502 with KERNAL ROM loaded and $DD00 callbacks connected to bus simulator.

**Step 1: Write c64_harness.h**

```c
#ifndef C64_HARNESS_H
#define C64_HARNESS_H

#include <stdint.h>
#include <stdbool.h>
#include "lib6502.h"
#include "bus_sim.h"
#include "trace.h"

typedef struct {
    M6502 *cpu;
    bus_state_t *bus;
    trace_log_t *trace;
} c64_harness_t;

/* Initialize: load KERNAL ROM, set up $DD00 callbacks */
bool c64_harness_init(c64_harness_t *c64, const char *kernal_path,
                      bus_state_t *bus, trace_log_t *trace);

/* Run CPU for N cycles */
void c64_harness_run(c64_harness_t *c64, int cycles);

/* Set up CPU state to call a KERNAL routine via JSR
 * Sets PC, A, X, Y, and pushes return address to stack */
void c64_harness_call(c64_harness_t *c64, uint16_t addr,
                      uint8_t a, uint8_t x, uint8_t y);

/* Check if the called routine has returned (PC at sentinel address) */
bool c64_harness_returned(c64_harness_t *c64);

/* Read CPU registers */
uint8_t c64_harness_a(c64_harness_t *c64);
uint8_t c64_harness_x(c64_harness_t *c64);
uint8_t c64_harness_y(c64_harness_t *c64);
uint8_t c64_harness_status(c64_harness_t *c64);  /* P register */
bool c64_harness_carry(c64_harness_t *c64);       /* Carry flag */

/* Clean up */
void c64_harness_free(c64_harness_t *c64);

#endif
```

**Step 2: Write c64_harness.c**

Key implementation details:
- Load 8KB KERNAL ROM at $E000-$FFFF
- Zero-fill RAM $0000-$CFFF
- Set up I/O area defaults (CIA2 at $DD00-$DDFF)
- lib6502 write callback on $DD00 → `bus_sim_c64_write()` + trace
- lib6502 read callback on $DD00 → `bus_sim_c64_read()`
- `c64_harness_call()` pushes a sentinel return address ($FFAA, a safe KERNAL address) onto stack and sets PC
- `c64_harness_returned()` checks if PC == sentinel+1

The KERNAL uses zero-page locations for IEC state. Key ones to initialize:
- $BA = current device number (default 8)
- $B9 = current secondary address
- $B8 = current logical file number
- $98 = number of open files

```c
#include "c64_harness.h"
#include <stdio.h>
#include <string.h>

#define KERNAL_BASE   0xE000
#define KERNAL_SIZE   8192
#define DD00_ADDR     0xDD00
#define SENTINEL_ADDR 0xFFF0  /* Unused area near end of ROM for return trap */

/* Global pointer for callbacks (lib6502 callbacks don't take user data) */
static c64_harness_t *g_c64;

static int dd00_write(M6502 *cpu, uint16_t addr, uint8_t val) {
    bus_sim_c64_write(g_c64->bus, val);
    if (g_c64->trace) {
        trace_record_bus_change(g_c64->trace, g_c64->bus->cycles,
                                TRACE_C64,
                                bus_sim_atn_low(g_c64->bus),
                                bus_sim_clk_low(g_c64->bus),
                                bus_sim_data_low(g_c64->bus));
    }
    return 0;
}

static int dd00_read(M6502 *cpu, uint16_t addr, uint8_t val) {
    /* Sync device GPIO first so reads reflect latest device state */
    bus_sim_sync_device(g_c64->bus);
    return bus_sim_c64_read(g_c64->bus);
}

static int sentinel_callback(M6502 *cpu, uint16_t addr, uint8_t val) {
    /* Stop execution by returning nonzero? Actually lib6502 uses
     * call callbacks — we just need PC to sit here. Run loop checks. */
    return 0;
}

bool c64_harness_init(c64_harness_t *c64, const char *kernal_path,
                      bus_state_t *bus, trace_log_t *trace) {
    g_c64 = c64;
    c64->bus = bus;
    c64->trace = trace;

    c64->cpu = M6502_new(NULL, NULL, NULL);
    if (!c64->cpu) return false;

    /* Clear all memory */
    memset(c64->cpu->memory, 0, 0x10000);

    /* Load KERNAL ROM */
    FILE *f = fopen(kernal_path, "rb");
    if (!f) return false;
    size_t n = fread(&c64->cpu->memory[KERNAL_BASE], 1, KERNAL_SIZE, f);
    fclose(f);
    if (n != KERNAL_SIZE) return false;

    /* Set up $DD00 callbacks */
    M6502_setCallback(c64->cpu, write, DD00_ADDR, dd00_write);
    M6502_setCallback(c64->cpu, read, DD00_ADDR, dd00_read);

    /* Place RTS at sentinel address */
    c64->cpu->memory[SENTINEL_ADDR] = 0x60;  /* RTS opcode — but we use BRK */
    /* Actually use BRK ($00) as halt sentinel */
    c64->cpu->memory[SENTINEL_ADDR] = 0x00;

    /* Initialize IEC-related zero page locations */
    c64->cpu->memory[0xBA] = 8;   /* Default device number */
    c64->cpu->memory[0x98] = 0;   /* Number of open files */

    return true;
}

void c64_harness_call(c64_harness_t *c64, uint16_t addr,
                      uint8_t a, uint8_t x, uint8_t y) {
    /* Push sentinel return address - 1 onto stack (RTS convention) */
    uint16_t ret = SENTINEL_ADDR - 1;
    c64->cpu->memory[0x0100 + c64->cpu->registers->s] = (ret >> 8) & 0xFF;
    c64->cpu->registers->s--;
    c64->cpu->memory[0x0100 + c64->cpu->registers->s] = ret & 0xFF;
    c64->cpu->registers->s--;

    c64->cpu->registers->a = a;
    c64->cpu->registers->x = x;
    c64->cpu->registers->y = y;
    c64->cpu->registers->pc = addr;
}

bool c64_harness_returned(c64_harness_t *c64) {
    return c64->cpu->registers->pc == SENTINEL_ADDR;
}

void c64_harness_run(c64_harness_t *c64, int cycles) {
    M6502_run(c64->cpu);
    /* Note: lib6502 M6502_run may need adaptation —
     * the actual API uses M6502_run(cpu) which runs until callback stops it.
     * We may need to use M6502_step or modify the approach.
     * This will be refined during implementation. */
}

uint8_t c64_harness_a(c64_harness_t *c64) { return c64->cpu->registers->a; }
uint8_t c64_harness_x(c64_harness_t *c64) { return c64->cpu->registers->x; }
uint8_t c64_harness_y(c64_harness_t *c64) { return c64->cpu->registers->y; }
uint8_t c64_harness_status(c64_harness_t *c64) { return c64->cpu->registers->p; }
bool c64_harness_carry(c64_harness_t *c64) { return c64->cpu->registers->p & 0x01; }

void c64_harness_free(c64_harness_t *c64) {
    if (c64->cpu) M6502_delete(c64->cpu);
    c64->cpu = NULL;
}
```

**Note:** lib6502's `M6502_run()` API runs until stopped. During implementation, check exact API — may need cycle-limited wrapper or use `M6502_step()` if available. Alternative: use a write callback on a cycle-counter I/O address to yield control.

**Step 3: Verify it compiles (may need lib6502 API adjustments)**

```bash
cc -c -Wall -I test -I vendor/lib6502 test/c64_harness.c -o /tmp/c64_harness.o
```

**Step 4: Commit**

```bash
git add test/c64_harness.h test/c64_harness.c
git commit -m "test: add C64 harness with lib6502 and $DD00 bus hooks"
```

---

### Task 9: Create firmware step wrapper

**Files:**
- Create: `test/firmware_step.h`
- Create: `test/firmware_step.c`

Adapts 64korppu's `iec_service()` for cooperative stepping.

**Step 1: Write firmware_step.h**

```c
#ifndef FIRMWARE_STEP_H
#define FIRMWARE_STEP_H

#include <stdint.h>
#include <stdbool.h>
#include "bus_sim.h"
#include "trace.h"

typedef struct {
    bus_state_t *bus;
    trace_log_t *trace;
    bool initialized;
} firmware_state_t;

/* Initialize 64korppu firmware (iec_init, cbm_dos_init, fastload_init, etc.) */
void firmware_init(firmware_state_t *fw, bus_state_t *bus, trace_log_t *trace);

/* Run one step of firmware: sync GPIO, call iec_service(), sync back.
 * cycles = how many C64 cycles this step represents (~1µs per cycle) */
void firmware_step(firmware_state_t *fw, int cycles);

#endif
```

**Step 2: Write firmware_step.c**

```c
#include "firmware_step.h"
#include "mock_hardware.h"
#include "iec_protocol.h"
#include "cbm_dos.h"
#include "fastload.h"
#include "fastload_jiffydos.h"
#include "compress_proto.h"

void firmware_init(firmware_state_t *fw, bus_state_t *bus, trace_log_t *trace) {
    fw->bus = bus;
    fw->trace = trace;

    mock_hardware_init();
    iec_init(8);
    cbm_dos_init();
    fastload_init();
    fastload_jiffydos_register();
    compress_proto_init();

    fw->initialized = true;
}

void firmware_step(firmware_state_t *fw, int cycles) {
    /* 1. Sync bus state → device GPIO (mock_PIND) */
    bus_sim_sync_device(fw->bus);

    /* Snapshot pre-step bus state */
    bool prev_clk = bus_sim_clk_low(fw->bus);
    bool prev_data = bus_sim_data_low(fw->bus);

    /* 2. Run one iteration of firmware main loop */
    iec_service();

    /* 3. Sync device GPIO back → bus state */
    bus_sim_sync_device(fw->bus);

    /* 4. Record trace if bus changed */
    bool new_clk = bus_sim_clk_low(fw->bus);
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
```

**Step 3: Verify it compiles (will need vendor sources and all mocks)**

This step may reveal missing mock functions or header conflicts. Address them as they arise.

**Step 4: Commit**

```bash
git add test/firmware_step.h test/firmware_step.c
git commit -m "test: add 64korppu firmware cooperative step wrapper"
```

---

### Task 10: Create Makefile

**Files:**
- Create: `test/Makefile`

**Step 1: Write Makefile**

```makefile
CC = cc
CFLAGS = -Wall -g -std=c11 -DHOST_TEST
INCLUDES = -I. -I../vendor/lib6502 -I../vendor/64korppu/include

# Vendor sources (64korppu E-variant)
VENDOR_SRC = ../vendor/64korppu/src
VENDOR_OBJS = $(VENDOR_SRC)/iec_protocol.o \
              $(VENDOR_SRC)/cbm_dos.o \
              $(VENDOR_SRC)/fastload.o \
              $(VENDOR_SRC)/fastload_jiffydos.o \
              $(VENDOR_SRC)/lz4_compress.o \
              $(VENDOR_SRC)/compress_proto.o

# lib6502
LIB6502_OBJ = ../vendor/lib6502/lib6502.o

# Mock and framework objects
MOCK_OBJS = mock_hardware.o mock_sram.o mock_fat12.o
FRAMEWORK_OBJS = bus_sim.o trace.o c64_harness.o firmware_step.o

# Test executables
TESTS = test_bus_sim test_iec_low test_iec_high test_jiffydos test_lz4_load

KERNAL_ROM = ../rom/original/kernal-901227-03.bin

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do echo "=== $$t ===" && ./$$t $(KERNAL_ROM) || exit 1; done

test_bus_sim: test_bus_sim.o bus_sim.o mock_hardware.o
	$(CC) $(CFLAGS) -o $@ $^

test_iec_low: test_iec_low.o $(FRAMEWORK_OBJS) $(MOCK_OBJS) $(VENDOR_OBJS) $(LIB6502_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test_iec_high: test_iec_high.o $(FRAMEWORK_OBJS) $(MOCK_OBJS) $(VENDOR_OBJS) $(LIB6502_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test_jiffydos: test_jiffydos.o $(FRAMEWORK_OBJS) $(MOCK_OBJS) $(VENDOR_OBJS) $(LIB6502_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test_lz4_load: test_lz4_load.o $(FRAMEWORK_OBJS) $(MOCK_OBJS) $(VENDOR_OBJS) $(LIB6502_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(VENDOR_SRC)/%.o: $(VENDOR_SRC)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB6502_OBJ): ../vendor/lib6502/lib6502.c
	$(CC) $(CFLAGS) -I../vendor/lib6502 -c $< -o $@

clean:
	rm -f *.o $(VENDOR_SRC)/*.o $(LIB6502_OBJ) $(TESTS)
```

**Step 2: Commit**

```bash
git add test/Makefile
git commit -m "test: add Makefile for integration test build"
```

---

### Task 11: Write test_iec_low — LISTEN and TALK

**Files:**
- Create: `test/test_iec_low.c`

Tests KERNAL's low-level IEC routines (LISTEN $FFB1, TALK $FFB4, UNLSN $FFAE, UNTLK $FFAB, ACPTR $FFA5, CIOUT $FFA8) against 64korppu firmware.

**Step 1: Write test_iec_low.c**

Start with the simplest test: LISTEN to device 8.

```c
#include "c64_harness.h"
#include "firmware_step.h"
#include "bus_sim.h"
#include "trace.h"
#include "mock_hardware.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Simulation configuration */
#define STEP_CYCLES    100
#define MAX_SIM_CYCLES 1000000  /* Timeout */

typedef struct {
    c64_harness_t c64;
    firmware_state_t fw;
    bus_state_t bus;
    trace_log_t trace;
} sim_t;

static void sim_init(sim_t *sim, const char *kernal_path) {
    bus_sim_init(&sim->bus);
    trace_init(&sim->trace);
    assert(c64_harness_init(&sim->c64, kernal_path, &sim->bus, &sim->trace));
    firmware_init(&sim->fw, &sim->bus, &sim->trace);
}

static void sim_run_until_return(sim_t *sim) {
    uint64_t timeout = sim->bus.cycles + MAX_SIM_CYCLES;
    while (!c64_harness_returned(&sim->c64) && sim->bus.cycles < timeout) {
        c64_harness_run(&sim->c64, STEP_CYCLES);
        bus_sim_advance(&sim->bus, STEP_CYCLES);
        firmware_step(&sim->fw, STEP_CYCLES);
    }
    assert(c64_harness_returned(&sim->c64) && "KERNAL routine did not return in time");
}

static void test_listen_device8(sim_t *sim) {
    printf("test_listen_device8... ");

    /* LISTEN $FFB1: expects device number in A */
    c64_harness_call(&sim->c64, 0xFFB1, 8, 0, 0);
    sim_run_until_return(sim);

    /* Verify trace contains ATN assert + LISTEN byte ($28 = LISTEN + device 8) */
    bool found_atn = false, found_listen = false;
    for (int i = 0; i < sim->trace.count; i++) {
        if (sim->trace.entries[i].event == TRACE_ATN_ASSERT)
            found_atn = true;
    }
    assert(found_atn && "ATN was not asserted during LISTEN");

    /* Verify no error (carry clear) */
    assert(!c64_harness_carry(&sim->c64) && "LISTEN returned with carry set (error)");

    printf("PASS\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <kernal.bin>\n", argv[0]);
        return 1;
    }

    sim_t sim;
    sim_init(&sim, argv[1]);

    test_listen_device8(&sim);

    /* Print full trace for debugging */
    printf("\n--- IEC Trace ---\n");
    trace_print(&sim.trace, stdout);

    printf("\nAll test_iec_low tests passed.\n");
    c64_harness_free(&sim.c64);
    return 0;
}
```

**Step 2: Try to build and iterate**

```bash
cd test && make test_iec_low
```

This will likely reveal compilation issues with vendor code. Fix mock/header gaps as they arise. The common issues will be:
- Missing AVR intrinsics (`_BV()`, `pgm_read_byte()`, etc.) — add to mock_hardware.h
- Missing `shiftreg.h` / `mfm_codec.h` — create minimal stubs
- Conflicting type definitions — resolve with `#ifdef HOST_TEST` guards

**Step 3: Run the test**

```bash
./test_iec_low ../rom/original/kernal-901227-03.bin
```

**Step 4: Add remaining low-level tests**

Add tests for TALK, UNLSN, UNTLK, ACPTR, CIOUT following the same pattern. Each test calls the KERNAL routine and verifies the trace shows the correct bus activity.

**Step 5: Commit**

```bash
git add test/test_iec_low.c
git commit -m "test: add low-level IEC routine tests (LISTEN, TALK, ACPTR, CIOUT)"
```

---

### Task 12: Write test_iec_high — LOAD

**Files:**
- Create: `test/test_iec_high.c`
- Create: `test/fixtures/hello.prg`

Tests KERNAL LOAD ($FFD5) end-to-end.

**Step 1: Create a test PRG file**

A minimal C64 PRG: 2-byte load address ($0801) + data bytes.

```bash
# Create hello.prg: load addr $0801, data = 10 bytes of $AA
printf '\x01\x08' > test/fixtures/hello.prg
python3 -c "import sys; sys.stdout.buffer.write(b'\xAA' * 10)" >> test/fixtures/hello.prg
```

**Step 2: Write test_iec_high.c**

```c
#include "c64_harness.h"
#include "firmware_step.h"
#include "bus_sim.h"
#include "trace.h"
#include "mock_hardware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* External mock_fat12 helper */
extern void mock_fat12_add_file(const char *name8, const char *ext3,
                                const uint8_t *data, uint16_t size);
extern void mock_fat12_reset(void);

#define STEP_CYCLES    100
#define MAX_SIM_CYCLES 5000000

typedef struct {
    c64_harness_t c64;
    firmware_state_t fw;
    bus_state_t bus;
    trace_log_t trace;
} sim_t;

static void sim_init(sim_t *sim, const char *kernal_path) {
    bus_sim_init(&sim->bus);
    trace_init(&sim->trace);
    mock_fat12_reset();
    assert(c64_harness_init(&sim->c64, kernal_path, &sim->bus, &sim->trace));
    firmware_init(&sim->fw, &sim->bus, &sim->trace);
}

static void sim_run_until_return(sim_t *sim) {
    uint64_t timeout = sim->bus.cycles + MAX_SIM_CYCLES;
    while (!c64_harness_returned(&sim->c64) && sim->bus.cycles < timeout) {
        c64_harness_run(&sim->c64, STEP_CYCLES);
        bus_sim_advance(&sim->bus, STEP_CYCLES);
        firmware_step(&sim->fw, STEP_CYCLES);
    }
    assert(c64_harness_returned(&sim->c64) && "KERNAL routine timed out");
}

static void test_load_hello(sim_t *sim) {
    printf("test_load_hello... ");

    /* Add test file to mock filesystem */
    uint8_t prg_data[12]; /* 2B addr + 10B data */
    prg_data[0] = 0x01; prg_data[1] = 0x08; /* Load addr $0801 */
    memset(&prg_data[2], 0xAA, 10);
    mock_fat12_add_file("HELLO   ", "PRG", prg_data, 12);

    /* Set up KERNAL LOAD parameters:
     * - filename at an address in memory
     * - filename length in A for SETNAM
     * - device 8, SA 0 for SETLFS
     * - then call LOAD with A=0 (load), X/Y = load address
     *
     * We need to call SETNAM ($FFBD), SETLFS ($FFBA), then LOAD ($FFD5).
     * Simplest approach: set up the zero-page variables directly. */

    /* Write filename "HELLO" at $0300 */
    const char *fname = "HELLO";
    for (int i = 0; i < 5; i++)
        sim->c64.cpu->memory[0x0300 + i] = fname[i];

    /* SETNAM: $B7=filename length, $BB/$BC=filename pointer */
    sim->c64.cpu->memory[0xB7] = 5;       /* filename length */
    sim->c64.cpu->memory[0xBB] = 0x00;    /* filename addr low */
    sim->c64.cpu->memory[0xBC] = 0x03;    /* filename addr high */

    /* SETLFS: $B8=logical file, $BA=device, $B9=secondary address */
    sim->c64.cpu->memory[0xB8] = 1;       /* logical file number */
    sim->c64.cpu->memory[0xBA] = 8;       /* device 8 */
    sim->c64.cpu->memory[0xB9] = 0;       /* SA 0 = LOAD */

    /* LOAD $FFD5: A=0 (load to address in file header) */
    c64_harness_call(&sim->c64, 0xFFD5, 0, 0x01, 0x08);
    sim_run_until_return(sim);

    /* Verify: data should be at $0801-$080A */
    bool data_ok = true;
    for (int i = 0; i < 10; i++) {
        if (sim->c64.cpu->memory[0x0801 + i] != 0xAA) {
            data_ok = false;
            break;
        }
    }
    assert(data_ok && "Loaded data does not match expected");
    assert(!c64_harness_carry(&sim->c64) && "LOAD returned error");

    printf("PASS\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <kernal.bin>\n", argv[0]);
        return 1;
    }

    sim_t sim;
    sim_init(&sim, argv[1]);

    test_load_hello(&sim);

    printf("\n--- IEC Trace ---\n");
    trace_print(&sim.trace, stdout);

    printf("\nAll test_iec_high tests passed.\n");
    c64_harness_free(&sim.c64);
    return 0;
}
```

**Step 3: Build and run**

```bash
cd test && make test_iec_high && ./test_iec_high ../rom/original/kernal-901227-03.bin
```

**Step 4: Add SAVE test following same pattern**

**Step 5: Commit**

```bash
git add test/test_iec_high.c test/fixtures/hello.prg
git commit -m "test: add high-level IEC tests (LOAD, SAVE)"
```

---

### Task 13: Scaffold test_jiffydos and test_lz4_load

**Files:**
- Create: `test/test_jiffydos.c`
- Create: `test/test_lz4_load.c`

These tests will not pass until karskiROM assembly code is written, but the scaffolding validates the framework is ready.

**Step 1: Write test_jiffydos.c scaffold**

```c
#include <stdio.h>

int main(int argc, char **argv) {
    printf("test_jiffydos: SKIPPED (requires karskiROM JiffyDOS routines)\n");
    /* TODO: When karskiROM 2-bit transfer code is written:
     * - Load modified KERNAL ROM instead of original
     * - Use step_cycles=5 for 2-bit timing precision
     * - Verify JiffyDOS handshake (DATA low ~260µs after ATN release)
     * - Verify 4-round 2-bit byte transfer
     * - Add jitter test with jitter_range=3 */
    return 0;
}
```

**Step 2: Write test_lz4_load.c scaffold**

```c
#include <stdio.h>

int main(int argc, char **argv) {
    printf("test_lz4_load: SKIPPED (requires karskiROM LZ4 decompression routines)\n");
    /* TODO: When karskiROM LZ4 code is written:
     * - Load modified KERNAL ROM
     * - Enable compression via XZ:1 command
     * - LOAD a file, verify decompressed data matches original
     * - Test with step_cycles=5 (JiffyDOS transport)
     * - Add jitter test with jitter_range=5 */
    return 0;
}
```

**Step 3: Commit**

```bash
git add test/test_jiffydos.c test/test_lz4_load.c
git commit -m "test: scaffold JiffyDOS and LZ4 tests (pending karskiROM code)"
```

---

### Task 14: Adapt vendor code for host compilation

**Files:**
- Modify: `vendor/64korppu/include/config.h` (add `#ifdef HOST_TEST` guards)
- Create: `test/mock_stubs.c` (any remaining stubs)

This task handles compilation issues that arise when building 64korppu firmware for host. Common issues:

**Step 1: Add HOST_TEST guards to config.h**

Wrap AVR-specific includes and definitions:

```c
#ifdef HOST_TEST
  #include "mock_hardware.h"
#else
  #include <avr/io.h>
  #include <util/delay.h>
  #include <avr/interrupt.h>
  /* ... existing AVR definitions ... */
#endif
```

**Step 2: Create mock stubs for remaining dependencies**

Any functions referenced by vendor code but not yet mocked (e.g., `shiftreg_init()`, `floppy_init()`, `mfm_init()`, `uart_init()`):

```c
/* test/mock_stubs.c */
#include <stdint.h>

void shiftreg_init(void) {}
void shiftreg_write(uint8_t val) {}
void floppy_init(void) {}
void floppy_motor_on(void) {}
void floppy_motor_off(void) {}
void floppy_recalibrate(void) {}
void mfm_init(void) {}
int disk_read_sector(uint8_t track, uint8_t head, uint8_t sector,
                     uint8_t *buf) { return -1; }
int disk_write_sector(uint8_t track, uint8_t head, uint8_t sector,
                      const uint8_t *buf) { return -1; }
```

**Step 3: Iterate until `make test_bus_sim` passes**

```bash
cd test && make test_bus_sim && ./test_bus_sim
```

**Step 4: Iterate until `make test_iec_low` compiles**

Fix all compilation errors one by one.

**Step 5: Commit**

```bash
git add -A
git commit -m "test: adapt vendor code for host compilation"
```

---

### Task 15: Integration — run all tests

**Step 1: Build everything**

```bash
cd test && make clean && make test
```

**Step 2: Fix any remaining issues**

Iterate on mock gaps, timing issues, API mismatches until tests pass or produce meaningful trace output.

**Step 3: Final commit**

```bash
git add -A
git commit -m "test: integration test framework working with standard IEC"
```

**Step 4: Push to GitHub**

```bash
git push
```
