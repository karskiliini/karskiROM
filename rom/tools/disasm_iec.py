#!/usr/bin/env python3
"""Disassemble C64 KERNAL IEC serial bus routines into ca65 source.

Produces labeled 6502 assembly for the IEC protocol section ($ED00-$EEB3).
Uses known labels from the KERNAL memory map.
"""

import sys
import struct

# 6502 addressing modes and instruction table
OPCODES = {
    0x00: ("BRK", "imp", 1), 0x01: ("ORA", "izx", 2), 0x05: ("ORA", "zp", 2),
    0x06: ("ASL", "zp", 2), 0x08: ("PHP", "imp", 1), 0x09: ("ORA", "imm", 2),
    0x0A: ("ASL", "acc", 1), 0x0D: ("ORA", "abs", 3), 0x0E: ("ASL", "abs", 3),
    0x10: ("BPL", "rel", 2), 0x11: ("ORA", "izy", 2), 0x15: ("ORA", "zpx", 2),
    0x16: ("ASL", "zpx", 2), 0x18: ("CLC", "imp", 1), 0x19: ("ORA", "aby", 3),
    0x1D: ("ORA", "abx", 3), 0x1E: ("ASL", "abx", 3),
    0x20: ("JSR", "abs", 3), 0x21: ("AND", "izx", 2), 0x24: ("BIT", "zp", 2),
    0x25: ("AND", "zp", 2), 0x26: ("ROL", "zp", 2), 0x28: ("PLP", "imp", 1),
    0x29: ("AND", "imm", 2), 0x2A: ("ROL", "acc", 1), 0x2C: ("BIT", "abs", 3),
    0x2D: ("AND", "abs", 3), 0x2E: ("ROL", "abs", 3),
    0x30: ("BMI", "rel", 2), 0x31: ("AND", "izy", 2), 0x35: ("AND", "zpx", 2),
    0x36: ("ROL", "zpx", 2), 0x38: ("SEC", "imp", 1), 0x39: ("AND", "aby", 3),
    0x3D: ("AND", "abx", 3), 0x3E: ("ROL", "abx", 3),
    0x40: ("RTI", "imp", 1), 0x41: ("EOR", "izx", 2), 0x45: ("EOR", "zp", 2),
    0x46: ("LSR", "zp", 2), 0x48: ("PHA", "imp", 1), 0x49: ("EOR", "imm", 2),
    0x4A: ("LSR", "acc", 1), 0x4C: ("JMP", "abs", 3), 0x4D: ("EOR", "abs", 3),
    0x4E: ("LSR", "abs", 3),
    0x50: ("BVC", "rel", 2), 0x51: ("EOR", "izy", 2), 0x55: ("EOR", "zpx", 2),
    0x56: ("LSR", "zpx", 2), 0x58: ("CLI", "imp", 1), 0x59: ("EOR", "aby", 3),
    0x5D: ("EOR", "abx", 3), 0x5E: ("LSR", "abx", 3),
    0x60: ("RTS", "imp", 1), 0x61: ("ADC", "izx", 2), 0x65: ("ADC", "zp", 2),
    0x66: ("ROR", "zp", 2), 0x68: ("PLA", "imp", 1), 0x69: ("ADC", "imm", 2),
    0x6A: ("ROR", "acc", 1), 0x6C: ("JMP", "ind", 3), 0x6D: ("ADC", "abs", 3),
    0x6E: ("ROR", "abs", 3),
    0x70: ("BVS", "rel", 2), 0x71: ("ADC", "izy", 2), 0x75: ("ADC", "zpx", 2),
    0x76: ("ROR", "zpx", 2), 0x78: ("SEI", "imp", 1), 0x79: ("ADC", "aby", 3),
    0x7D: ("ADC", "abx", 3), 0x7E: ("ROR", "abx", 3),
    0x81: ("STA", "izx", 2), 0x84: ("STY", "zp", 2), 0x85: ("STA", "zp", 2),
    0x86: ("STX", "zp", 2), 0x88: ("DEY", "imp", 1), 0x8A: ("TXA", "imp", 1),
    0x8C: ("STY", "abs", 3), 0x8D: ("STA", "abs", 3), 0x8E: ("STX", "abs", 3),
    0x90: ("BCC", "rel", 2), 0x91: ("STA", "izy", 2), 0x94: ("STY", "zpx", 2),
    0x95: ("STA", "zpx", 2), 0x96: ("STX", "zpy", 2), 0x98: ("TYA", "imp", 1),
    0x99: ("STA", "aby", 3), 0x9A: ("TXS", "imp", 1), 0x9D: ("STA", "abx", 3),
    0xA0: ("LDY", "imm", 2), 0xA1: ("LDA", "izx", 2), 0xA2: ("LDX", "imm", 2),
    0xA4: ("LDY", "zp", 2), 0xA5: ("LDA", "zp", 2), 0xA6: ("LDX", "zp", 2),
    0xA8: ("TAY", "imp", 1), 0xA9: ("LDA", "imm", 2), 0xAA: ("TAX", "imp", 1),
    0xAC: ("LDY", "abs", 3), 0xAD: ("LDA", "abs", 3), 0xAE: ("LDX", "abs", 3),
    0xB0: ("BCS", "rel", 2), 0xB1: ("LDA", "izy", 2), 0xB4: ("LDY", "zpx", 2),
    0xB5: ("LDA", "zpx", 2), 0xB6: ("LDX", "zpy", 2), 0xB8: ("CLV", "imp", 1),
    0xB9: ("LDA", "aby", 3), 0xBA: ("TSX", "imp", 1), 0xBC: ("LDY", "abx", 3),
    0xBD: ("LDA", "abx", 3), 0xBE: ("LDX", "aby", 3),
    0xC0: ("CPY", "imm", 2), 0xC1: ("CMP", "izx", 2), 0xC4: ("CPY", "zp", 2),
    0xC5: ("CMP", "zp", 2), 0xC6: ("DEC", "zp", 2), 0xC8: ("INY", "imp", 1),
    0xC9: ("CMP", "imm", 2), 0xCA: ("DEX", "imp", 1), 0xCC: ("CPY", "abs", 3),
    0xCD: ("CMP", "abs", 3), 0xCE: ("DEC", "abs", 3),
    0xD0: ("BNE", "rel", 2), 0xD1: ("CMP", "izy", 2), 0xD5: ("CMP", "zpx", 2),
    0xD6: ("DEC", "zpx", 2), 0xD8: ("CLD", "imp", 1), 0xD9: ("CMP", "aby", 3),
    0xDD: ("CMP", "abx", 3), 0xDE: ("DEC", "abx", 3),
    0xE0: ("CPX", "imm", 2), 0xE1: ("SBC", "izx", 2), 0xE4: ("CPX", "zp", 2),
    0xE5: ("SBC", "zp", 2), 0xE6: ("INC", "zp", 2), 0xE8: ("INX", "imp", 1),
    0xE9: ("SBC", "imm", 2), 0xEA: ("NOP", "imp", 1), 0xEC: ("CPX", "abs", 3),
    0xED: ("SBC", "abs", 3), 0xEE: ("INC", "abs", 3),
    0xF0: ("BEQ", "rel", 2), 0xF1: ("SBC", "izy", 2), 0xF5: ("SBC", "zpx", 2),
    0xF6: ("INC", "zpx", 2), 0xF8: ("SED", "imp", 1), 0xF9: ("SBC", "aby", 3),
    0xFD: ("SBC", "abx", 3), 0xFE: ("INC", "abx", 3),
}

# Known labels in the KERNAL IEC section
LABELS = {
    # IEC protocol entry points
    0xED00: "iec_listen_or_talk",
    0xED09: "iec_talk",         # TALK entry
    0xED0C: "iec_listen",       # LISTEN entry
    0xED11: "iec_send_atn_byte",
    0xED20: "iec_send_atn_cont",
    0xED2E: "iec_atn_assert",
    0xED36: "iec_atn_send_setup",
    0xED40: "iec_send_atn_main",
    0xED49: "iec_clk_release_for_byte",
    0xED4C: "iec_check_eoi_flag",
    0xED50: "iec_eoi_wait_data_hi",
    0xED55: "iec_eoi_wait_data_lo",
    0xED5A: "iec_wait_listener_ready",
    0xED5F: "iec_send_bit_loop",
    0xED6F: "iec_send_bit_wait_clk",
    0xED76: "iec_send_bit_dec",
    0xED7B: "iec_send_bit_done",
    0xED84: "iec_send_byte_entry",
    0xEDAD: "iec_timeout_error",
    0xEDB2: "iec_set_status_timeout",
    0xEDB9: "iec_second",       # SECOND/TKSA entry
    0xEDC7: "iec_turnaround",   # Talk turnaround
    0xEDDD: "iec_ciout",        # CIOUT entry
    0xEDE5: "iec_ciout_cont",
    0xEDEF: "iec_untalk",       # UNTALK entry
    0xEDFE: "iec_unlisten",     # UNLISTEN entry (shared with UNTALK)
    0xEE00: "iec_untk_unls_send",
    0xEE06: "iec_release_delay",
    0xEE13: "iec_acptr",        # ACPTR entry
    0xEE1B: "iec_acptr_wait_clk",
    0xEE20: "iec_acptr_timer_setup",
    0xEE2A: "iec_acptr_release_data",
    0xEE2D: "iec_acptr_timer_debounce",
    0xEE30: "iec_acptr_timer_loop",
    0xEE3C: "iec_acptr_no_timeout",
    0xEE3E: "iec_acptr_eoi",
    0xEE47: "iec_acptr_eoi_ack",
    0xEE56: "iec_acptr_receive_byte",
    0xEE5A: "iec_acptr_wait_clk_hi",
    0xEE67: "iec_acptr_wait_clk_lo",
    0xEE72: "iec_acptr_bit_dec",
    0xEE76: "iec_acptr_byte_done",
    0xEE85: "iec_release_clk",
    0xEE8E: "iec_assert_clk",
    0xEE97: "iec_release_data",
    0xEEA0: "iec_assert_data",
    0xEEA9: "iec_debounce",     # Debounce read: LDA/CMP/BNE/ASL/RTS
    0xEEB3: "iec_delay_1ms",    # ~1ms delay loop

    # Subroutines called from IEC code
    0xEDBE: "iec_release_atn",
    0xF070: "iec_find_file",
    0xF0A4: "iec_find_device",
    0xFE1C: "iec_or_status",    # OR A into ST ($90)

    # Referenced from UNTALK/UNLISTEN
    0xEDF0: "iec_untalk_body",
    0xEDFC: "iec_unls_or_untk",
}

# Known zero-page symbols
ZP_SYMBOLS = {
    0x90: "ST", 0x93: "VERCK", 0x94: "C3PO", 0x95: "BTEFNR",
    0xA3: "BTEFNR2", 0xA4: "BUFPNT", 0xA5: "CNTDN",
    0xAE: "EAL", 0xAF: "EAH", 0xB7: "FNLEN", 0xB8: "LA",
    0xB9: "SA", 0xBA: "FA", 0xBB: "FNADR", 0xBC: "FNADR+1",
    0x91: "STKEY", 0x98: "LDTND", 0x99: "DESSION", 0x9A: "MSESSION",
    0x9D: "MSGFLG", 0xC1: "STAL", 0xC2: "STAH", 0xC3: "MEMUSS",
}

# Known absolute address symbols
ABS_SYMBOLS = {
    0xDD00: "CIA2_PRA",
    0xDC06: "CIA1_TBLO", 0xDC07: "CIA1_TBHI",
    0xDC0D: "CIA1_ICR", 0xDC0F: "CIA1_CRB",
}


def format_operand(mode, data, pc, labels):
    """Format operand based on addressing mode."""
    if mode == "imp" or mode == "acc":
        return "a" if mode == "acc" else ""
    elif mode == "imm":
        return f"#${data[0]:02X}"
    elif mode == "zp":
        addr = data[0]
        sym = ZP_SYMBOLS.get(addr)
        return sym if sym else f"${addr:02X}"
    elif mode == "zpx":
        addr = data[0]
        sym = ZP_SYMBOLS.get(addr)
        base = sym if sym else f"${addr:02X}"
        return f"{base},x"
    elif mode == "zpy":
        addr = data[0]
        sym = ZP_SYMBOLS.get(addr)
        base = sym if sym else f"${addr:02X}"
        return f"{base},y"
    elif mode == "abs":
        addr = data[0] | (data[1] << 8)
        sym = ABS_SYMBOLS.get(addr) or labels.get(addr)
        return sym if sym else f"${addr:04X}"
    elif mode == "abx":
        addr = data[0] | (data[1] << 8)
        sym = ABS_SYMBOLS.get(addr) or labels.get(addr)
        base = sym if sym else f"${addr:04X}"
        return f"{base},x"
    elif mode == "aby":
        addr = data[0] | (data[1] << 8)
        sym = ABS_SYMBOLS.get(addr) or labels.get(addr)
        base = sym if sym else f"${addr:04X}"
        return f"{base},y"
    elif mode == "ind":
        addr = data[0] | (data[1] << 8)
        sym = ABS_SYMBOLS.get(addr) or labels.get(addr)
        base = sym if sym else f"${addr:04X}"
        return f"({base})"
    elif mode == "izx":
        addr = data[0]
        sym = ZP_SYMBOLS.get(addr)
        base = sym if sym else f"${addr:02X}"
        return f"({base},x)"
    elif mode == "izy":
        addr = data[0]
        sym = ZP_SYMBOLS.get(addr)
        base = sym if sym else f"${addr:02X}"
        return f"({base}),y"
    elif mode == "rel":
        offset = data[0]
        if offset >= 0x80:
            offset -= 0x100
        target = pc + 2 + offset
        sym = labels.get(target)
        return sym if sym else f"${target:04X}"
    return "???"


def disassemble(rom_data, start, end, labels):
    """Disassemble a range of ROM, returning list of (addr, asm_line, is_label)."""
    lines = []
    pc = start
    while pc < end:
        offset = pc - 0xE000
        if offset >= len(rom_data):
            break

        # Check for label
        if pc in labels:
            lines.append((pc, f"{labels[pc]}:", True))

        opcode = rom_data[offset]
        if opcode in OPCODES:
            mnemonic, mode, size = OPCODES[opcode]
            operand_data = rom_data[offset+1:offset+size]
            operand = format_operand(mode, operand_data, pc, labels)
            if operand:
                asm = f"    {mnemonic.lower()} {operand}"
            else:
                asm = f"    {mnemonic.lower()}"
            lines.append((pc, asm, False))
            pc += size
        else:
            # Unknown opcode — emit as .byte
            lines.append((pc, f"    .byte ${opcode:02X}  ; unknown opcode", False))
            pc += 1

    return lines


def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else "rom/original/kernal-901227-03.bin"
    with open(rom_path, "rb") as f:
        rom = f.read()

    # Disassemble IEC section: $ED00-$EEB3
    lines = disassemble(rom, 0xED00, 0xEEB4, LABELS)

    print("; Auto-generated disassembly of C64 KERNAL IEC routines")
    print("; Source ROM: 901227-03")
    print("; Range: $ED00-$EEB3")
    print()

    for addr, line, is_label in lines:
        if is_label:
            print(line)
        else:
            print(line)


if __name__ == "__main__":
    main()
