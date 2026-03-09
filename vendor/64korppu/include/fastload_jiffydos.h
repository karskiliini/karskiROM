#ifndef FASTLOAD_JIFFYDOS_H
#define FASTLOAD_JIFFYDOS_H

#include "fastload.h"

/*
 * JiffyDOS fast-load protocol.
 *
 * Replaces standard IEC byte transfer with 2-bit parallel transfer
 * using CLK+DATA lines simultaneously. Requires JiffyDOS KERNAL ROM
 * on the C64 side. No hardware changes on the device side.
 *
 * Standard IEC: 1 bit at a time on DATA, CLK as clock → ~1 KB/s
 * JiffyDOS:     2 bits at a time (CLK+DATA as data)  → ~5-10 KB/s
 *
 * Detection: After ATN release in TALK mode, JiffyDOS-C64 holds
 * DATA low for ~260µs. Standard IEC does not do this.
 *
 * Byte transfer (send, 4 rounds):
 *   Round 1: CLK=bit0, DATA=bit1 (LSB pair)
 *   Round 2: CLK=bit2, DATA=bit3
 *   Round 3: CLK=bit4, DATA=bit5
 *   Round 4: CLK=bit6, DATA=bit7 (MSB pair)
 *   Each round ~13µs → full byte ~52µs
 *
 * EOI: Longer delay (~200µs) before last byte.
 */

/* Detection timing */
#define JIFFY_DETECT_MIN_US    200
#define JIFFY_DETECT_MAX_US    320

/* Byte transfer timing */
#define JIFFY_BIT_PAIR_US       13
#define JIFFY_EOI_DELAY_US     200
#define JIFFY_BYTE_DELAY_US     30
#define JIFFY_TURNAROUND_US     80

/* Register JiffyDOS protocol into fastload registry */
void fastload_jiffydos_register(void);

#endif /* FASTLOAD_JIFFYDOS_H */
