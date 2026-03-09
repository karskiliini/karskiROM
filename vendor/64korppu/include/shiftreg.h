#ifndef SHIFTREG_H
#define SHIFTREG_H

#include <stdint.h>

/*
 * 74HC595 shift register driver for floppy output signals.
 *
 * Shares SPI bus with 23LC256 SRAM. RCLK (latch) on D6.
 * All outputs active-low (HIGH = deasserted).
 *
 * Bit mapping (QA..QH):
 *   0: /SIDE1    1: /DENSITY  2: /MOTEA   3: /DRVSEL
 *   4: /MOTOR    5: /DIR      6: /STEP    7: /WGATE
 */

void shiftreg_init(void);

/* Write all 8 bits and latch */
void shiftreg_write(uint8_t value);

/* Set a single bit (0=asserted/LOW, 1=deasserted/HIGH) and update */
void shiftreg_set_bit(uint8_t bit, uint8_t value);

/* Clear a single bit (assert, set LOW) and update */
void shiftreg_assert_bit(uint8_t bit);

/* Set a single bit (deassert, set HIGH) and update */
void shiftreg_release_bit(uint8_t bit);

/* Get current shift register state */
uint8_t shiftreg_get(void);

#endif /* SHIFTREG_H */
