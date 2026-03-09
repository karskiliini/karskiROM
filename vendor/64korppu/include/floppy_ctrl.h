#ifndef FLOPPY_CTRL_H
#define FLOPPY_CTRL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * PC 3.5" HD floppy drive control via 74HC595 shift register.
 *
 * Output signals go through 74HC595 (SPI shared with SRAM).
 * Input signals are direct GPIO (A0-A2).
 * /WDATA on D7 (direct GPIO for timing accuracy).
 * /RDATA on D8 (Timer1 ICP1 for MFM capture).
 */

typedef struct {
    uint8_t current_track;
    uint8_t current_side;
    bool motor_on;
    bool disk_present;
    bool write_protected;
    bool initialized;
} floppy_state_t;

void floppy_init(void);
void floppy_motor_on(void);
void floppy_motor_off(void);
int floppy_recalibrate(void);
int floppy_seek(uint8_t track);
void floppy_select_side(uint8_t side);
int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector, uint8_t *buf);
int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector, const uint8_t *buf);
floppy_state_t floppy_get_state(void);

/* LBA/CHS conversion */
static inline void floppy_lba_to_chs(uint16_t lba, uint8_t *track,
                                      uint8_t *side, uint8_t *sector) {
    *track = lba / (2 * 18);
    uint16_t temp = lba % (2 * 18);
    *side = temp / 18;
    *sector = (temp % 18) + 1;
}

static inline uint16_t floppy_chs_to_lba(uint8_t track, uint8_t side,
                                          uint8_t sector) {
    return (track * 2 + side) * 18 + (sector - 1);
}

#endif /* FLOPPY_CTRL_H */
