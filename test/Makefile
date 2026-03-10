# test/Makefile -- Build and run karskiROM test suite.
#
# All vendor code is compiled with -DHOST_TEST so that AVR-specific
# headers are replaced by mock_hardware.h via config.h.

CC      = cc
CFLAGS  = -Wall -g -std=c11 -DHOST_TEST
INCS    = -I. -I../vendor/lib6502 -I../vendor/64korppu/include

KERNAL_ROM = ../rom/original/kernal-901227-03.bin

# ---- Source paths ----

VPATH_VENDOR = ../vendor/64korppu/src
VPATH_LIB6502 = ../vendor/lib6502

# ---- Object files ----

# Mock / test infrastructure
MOCK_OBJS = mock_hardware.o mock_sram.o mock_fat12.o mock_stubs.o
INFRA_OBJS = bus_sim.o trace.o c64_harness.o firmware_step.o
LIB6502_OBJ = lib6502.o

# Vendor .c files (compiled with HOST_TEST).
# NOTE: sram.c and fat12.c are replaced by mock_sram.c and mock_fat12.c.
VENDOR_OBJS = lz4_compress.o compress_proto.o \
              fastload.o fastload_jiffydos.o fastload_burst.o fastload_epyx.o \
              cbm_dos.o iec_protocol.o

# All shared objects (everything except test main files)
ALL_OBJS = $(MOCK_OBJS) $(INFRA_OBJS) $(LIB6502_OBJ) $(VENDOR_OBJS)

# ---- Test executables ----

TESTS = test_bus_sim test_trace test_c64_harness \
        test_iec_low test_iec_high test_jiffydos test_lz4_load

.PHONY: all test clean

all: $(TESTS)

test: $(TESTS)
	@echo "========================================"
	@echo "Running all tests..."
	@echo "========================================"
	@failed=0; \
	for t in $(TESTS); do \
		echo "--- $$t ---"; \
		case $$t in \
			test_c64_harness|test_iec_low|test_iec_high) \
				./$$t $(KERNAL_ROM) || failed=$$((failed + 1));; \
			*) \
				./$$t || failed=$$((failed + 1));; \
		esac; \
		echo ""; \
	done; \
	echo "========================================"; \
	if [ $$failed -eq 0 ]; then \
		echo "All tests passed."; \
	else \
		echo "$$failed test suite(s) FAILED."; \
		exit 1; \
	fi

clean:
	rm -f *.o $(TESTS)

# ---- Compile rules ----

# Test / mock sources (in current directory)
%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# Vendor sources
lz4_compress.o: $(VPATH_VENDOR)/lz4_compress.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

compress_proto.o: $(VPATH_VENDOR)/compress_proto.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

fastload.o: $(VPATH_VENDOR)/fastload.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

fastload_jiffydos.o: $(VPATH_VENDOR)/fastload_jiffydos.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

fastload_burst.o: $(VPATH_VENDOR)/fastload_burst.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

fastload_epyx.o: $(VPATH_VENDOR)/fastload_epyx.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

cbm_dos.o: $(VPATH_VENDOR)/cbm_dos.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

iec_protocol.o: $(VPATH_VENDOR)/iec_protocol.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# lib6502
lib6502.o: $(VPATH_LIB6502)/lib6502.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# ---- Link rules ----

# test_bus_sim: only needs bus_sim + mock_hardware
test_bus_sim: test_bus_sim.o bus_sim.o mock_hardware.o
	$(CC) $(CFLAGS) -o $@ $^

# test_trace: only needs trace
test_trace: test_trace.o trace.o
	$(CC) $(CFLAGS) -o $@ $^

# test_c64_harness: needs harness + bus_sim + trace + lib6502 + mock_hardware
# Also needs all vendor/mock objects since c64_harness now calls firmware_step
test_c64_harness: test_c64_harness.o $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# test_iec_low: needs everything (both sides of the bus)
test_iec_low: test_iec_low.o $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# test_iec_high: needs everything
test_iec_high: test_iec_high.o $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# test_jiffydos: standalone scaffold
test_jiffydos: test_jiffydos.o
	$(CC) $(CFLAGS) -o $@ $^

# test_lz4_load: standalone scaffold
test_lz4_load: test_lz4_load.o
	$(CC) $(CFLAGS) -o $@ $^
