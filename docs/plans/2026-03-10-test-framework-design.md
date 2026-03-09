# Test Framework Design: karskiROM + 64korppu Integration

## Summary

Test framework that runs real C64 KERNAL ROM code via lib6502 emulator against
64korppu E-variant firmware code, using a cooperative scheduling model over a
shared IEC bus simulator.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Test Runner                        │
│  - loads KERNAL ROM into lib6502                    │
│  - loads test files into 64korppu FAT12 mock        │
│  - runs cooperative loop                            │
│  - collects IEC trace                               │
│  - compares traces to expected sequences            │
└──────────┬──────────────────────┬───────────────────┘
           │                      │
     ┌─────▼─────┐         ┌─────▼──────┐
     │  lib6502   │         │  64korppu  │
     │  + KERNAL  │         │  E-variant │
     │  ROM       │         │  firmware  │
     └─────┬─────┘         └─────┬──────┘
           │                      │
     ┌─────▼──────────────────────▼─────┐
     │        IEC Bus Simulator         │
     │  - shared state: CLK, DATA, ATN  │
     │  - $DD00 <-> GPIO mapping        │
     │  - cycle counter (1 MHz C64)     │
     │  - trace log                     │
     └─────────────────────────────────┘
```

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| C64 code execution | Real 6502 via lib6502 | Tests actual assembly, not C simulation |
| Routines under test | Full IEC stack (LISTEN, TALK, ACPTR, CIOUT, OPEN, CLOSE, LOAD, SAVE) | Bottom-up coverage |
| 64korppu variant | E (Nano+SRAM) | Has JiffyDOS + LZ4 support |
| Code import | vendor/ copy | Self-contained, no submodule dependency |
| Timing model | Cooperative + jitter tests | Deterministic, debuggable |
| Verification | IEC protocol trace (sequence + timing tolerance) | Catches protocol-level bugs |

## Cooperative Loop

```c
typedef struct {
    int step_cycles;        // base step size
    int jitter_range;       // 0 = no jitter, >0 = random +/-N
    uint32_t jitter_seed;   // reproducibility
} sim_config_t;

void sim_tick(sim_state_t *sim) {
    int cycles = sim->config.step_cycles;

    if (sim->config.jitter_range > 0) {
        int jitter = (rand_r(&sim->config.jitter_seed)
                      % (2 * sim->config.jitter_range + 1))
                      - sim->config.jitter_range;
        cycles += jitter;
        if (cycles < 1) cycles = 1;
    }

    c64_harness_run(sim->c64, cycles);
    bus_sim_advance(&sim->bus, cycles);
    firmware_step(sim->fw, cycles);
    trace_record(&sim->trace, sim->total_cycles, &sim->bus);
    sim->total_cycles += cycles;
}
```

## Step Sizes

| Test | step_cycles | jitter_range | Rationale |
|------|-------------|--------------|-----------|
| test_iec_low | 100 | 0 | Standard IEC, deterministic |
| test_iec_high | 100 | 0 | LOAD/SAVE, deterministic |
| test_jiffydos | 5 | 0 | 2-bit, precise timing |
| test_jiffydos (jitter) | 5 | 3 | Timing sensitivity test |
| test_lz4_load | 5 | 0 | JiffyDOS + LZ4 |
| test_lz4_load (jitter) | 5 | 5 | Stress test |

## Tested KERNAL Routines

| Level | Routine | Address | Tests |
|-------|---------|---------|-------|
| 1 | LISTEN | $FFB1 | ATN + device + LISTEN command on bus |
| 1 | TALK | $FFB4 | ATN + device + TALK command on bus |
| 1 | UNLSN | $FFAE | UNLISTEN command |
| 1 | UNTLK | $FFAB | UNTALK command |
| 2 | ACPTR | $FFA5 | Receive byte from talker (1 byte into A) |
| 2 | CIOUT | $FFA8 | Send byte to listener |
| 3 | OPEN | $FFC0 | Open channel (SA + filename) |
| 3 | CLOSE | $FFC3 | Close channel |
| 4 | LOAD | $FFD5 | Full load sequence: OPEN->TALK->ACPTR loop->UNTLK->CLOSE |
| 4 | SAVE | $FFD8 | Full save sequence: OPEN->LISTEN->CIOUT loop->UNLSN->CLOSE |

## IEC Trace Format

```
CYCLE     DIR    EVENT              DATA         BUS_STATE
--------------------------------------------------------------
00000000  C64->  ATN_ASSERT                      ATN=0 CLK=1 DATA=1
00000047  C64->  BYTE_OUT           $28          ATN=0 CLK=0 DATA=0
00000102  DEV->  DATA_ACK                        ATN=0 CLK=1 DATA=0
00000150  C64->  ATN_RELEASE                     ATN=1 CLK=1 DATA=0
00000210  DEV->  BYTE_READY         $41          ATN=1 CLK=0 DATA=1
```

- `C64->` = KERNAL writes $DD00
- `DEV->` = 64korppu changes bus state
- Each row = one bus state change
- Comparison: expected trace vs actual trace, with configurable cycle tolerance

### Trace Comparison Rules

- **Sequence must match** — same events in same order
- **Timing tolerance** — default +/-50 cycles per event (configurable per test)
- **JiffyDOS tests** — stricter tolerance +/-5 cycles for 2-bit rounds

## Timing Risk Mitigations

The cooperative model does not run both sides truly concurrently. Mitigations:

1. **Adjustable step size** — JiffyDOS tests use 5-cycle steps (vs 100 for standard IEC)
2. **Jitter tests** — each JiffyDOS/LZ4 test runs twice: deterministic + randomized step timing
3. **Jitter failure = timing bug** — if jitter test fails but deterministic passes, it indicates a timing-sensitive bug that would manifest on real hardware
4. **Final validation** — VICE emulator + real hardware testing as ultimate verification

## IEC Bus Simulation

CIA2 Port A ($DD00) bit mapping:

```
Bit 3: ATN OUT
Bit 4: CLK OUT
Bit 5: DATA OUT
Bit 6: CLK IN
Bit 7: DATA IN
```

lib6502 write callback on $DD00 updates shared bus state.
lib6502 read callback on $DD00 returns current bus state (including 64korppu's IN bits).
64korppu GPIO functions map to the same shared bus state.

## Directory Structure

```
karskiROM/
├── rom/original/kernal-901227-03.bin
├── vendor/
│   ├── lib6502/
│   │   ├── lib6502.c
│   │   └── lib6502.h
│   └── 64korppu/
│       ├── src/
│       │   ├── iec_protocol.c
│       │   ├── cbm_dos.c
│       │   ├── fat12.c
│       │   ├── d64.c
│       │   ├── fastload.c
│       │   ├── fastload_jiffydos.c
│       │   ├── lz4_compress.c
│       │   └── compress_proto.c
│       └── include/
│           ├── iec_protocol.h
│           ├── cbm_dos.h
│           ├── config.h              (adapted for host build)
│           ├── fastload.h
│           ├── fastload_jiffydos.h
│           └── lz4_compress.h
├── test/
│   ├── Makefile
│   ├── bus_sim.c / .h                IEC bus simulator
│   ├── trace.c / .h                  trace recording and comparison
│   ├── firmware_step.c / .h          64korppu cooperative wrapper
│   ├── c64_harness.c / .h            lib6502 + KERNAL ROM + $DD00 hooks
│   ├── mock_hardware.c / .h          64korppu GPIO/SPI/timer mocks
│   ├── test_iec_low.c                Level 1-2: LISTEN, TALK, ACPTR, CIOUT
│   ├── test_iec_high.c               Level 3-4: OPEN, CLOSE, LOAD, SAVE
│   ├── test_jiffydos.c               JiffyDOS 2-bit handshake + transfer
│   ├── test_lz4_load.c               LZ4 compressed LOAD
│   ├── expected/                     expected traces
│   │   ├── listen_device8.trace
│   │   ├── load_standard.trace
│   │   ├── load_jiffydos.trace
│   │   └── load_lz4.trace
│   └── fixtures/                     test files
│       ├── hello.prg
│       └── large.prg
└── docs/plans/
```

## 64korppu Vendor Adaptation

64korppu E-variant code targets ATmega328P. For host compilation:

- `config.h` — replace AVR-specific definitions (port addresses, SPI registers) with mock versions
- `mock_hardware.c` — implements `gpio_read()`, `gpio_write()`, `spi_transfer()` etc. via bus simulator
- No changes to actual firmware source files — all adaptation via headers and mocks
