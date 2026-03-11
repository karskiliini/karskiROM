# rom/Makefile — Build karskiROM custom C64 KERNAL ROM
#
# Assembles with ca65/ld65 (cc65 toolchain).
# Verifies byte-for-byte match with original ROM during development.

CA65    = ca65
LD65    = ld65

SRCDIR  = src
ORIGINAL = original/kernal-901227-03.bin

TARGET  = karskiROM.bin
OBJ     = $(SRCDIR)/kernal.o
CFG     = $(SRCDIR)/kernal.cfg

.PHONY: all clean verify

all: $(TARGET)

$(SRCDIR)/kernal.o: $(SRCDIR)/kernal.s $(SRCDIR)/defs.inc
	$(CA65) -I $(SRCDIR) -o $@ $<

$(TARGET): $(OBJ) $(CFG)
	$(LD65) -C $(CFG) -o $@ $(OBJ)

# Verify assembled ROM matches original byte-for-byte.
# This target should PASS during initial development and will
# intentionally FAIL once we start modifying the IEC routines.
verify: $(TARGET)
	@if cmp -s $(TARGET) $(ORIGINAL); then \
		echo "VERIFY: $(TARGET) matches original ROM ✓"; \
	else \
		echo "VERIFY: $(TARGET) differs from original ROM"; \
		echo "  Checking size..."; \
		ls -la $(TARGET) $(ORIGINAL); \
		echo "  First difference:"; \
		cmp $(TARGET) $(ORIGINAL) || true; \
	fi

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)
