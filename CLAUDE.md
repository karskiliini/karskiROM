# karskiROM

Custom Commodore 64 KERNAL ROM replacement designed to work with the [64korppu](https://github.com/karskiliini/64korppu) controller.

## Project Idea

karskiROM provides JiffyDOS-like features (fast 2-bit serial transfer, DOS wedge) but additionally adds support for compression (LZ4) over IEC bus communication. When paired with 64korppu, compressed transfers reach ~9 KB/s effective throughput vs ~0.4 KB/s standard IEC.

## Architecture

- Target: C64 KERNAL ROM, 8 KB ($E000–$FFFF)
- Base: Original KERNAL 901227-03 in `rom/original/`
- Language: 6502 assembly (ca65/cc65)
- Testing: VICE emulator + real hardware

## Key Protocols

- **2-bit transfer**: JiffyDOS-compatible, auto-detected via ATN timing handshake
- **LZ4 compression**: Activated via `XZ:1` command on command channel, block-based protocol
- **Fallback**: Standard IEC for non-compatible devices
