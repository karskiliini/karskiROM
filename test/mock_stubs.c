/*
 * mock_stubs.c -- Stub implementations for vendor functions that are
 *                 not needed in host test builds but must link.
 *
 * Covers: shiftreg, floppy_ctrl, mfm_codec, disk I/O
 */

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* ---- 74HC595 shift register stubs ---- */

static uint8_t sr_value = 0xFF;

void shiftreg_init(void) {
    sr_value = 0xFF;
}

void shiftreg_write(uint8_t value) {
    sr_value = value;
}

void shiftreg_set_bit(uint8_t bit, uint8_t value) {
    if (value)
        sr_value |= (1 << bit);
    else
        sr_value &= ~(1 << bit);
}

void shiftreg_assert_bit(uint8_t bit) {
    sr_value &= ~(1 << bit);
}

void shiftreg_release_bit(uint8_t bit) {
    sr_value |= (1 << bit);
}

uint8_t shiftreg_get(void) {
    return sr_value;
}

/* ---- Floppy controller stubs ---- */

void floppy_init(void) {}
void floppy_motor_on(void) {}
void floppy_motor_off(void) {}

int floppy_recalibrate(void) {
    return 0;  /* FLOPPY_OK */
}

int floppy_seek(uint8_t track) {
    (void)track;
    return 0;
}

void floppy_select_side(uint8_t side) {
    (void)side;
}

int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector, uint8_t *buf) {
    (void)track; (void)side; (void)sector; (void)buf;
    return -3;  /* FLOPPY_ERR_READ */
}

int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector, const uint8_t *buf) {
    (void)track; (void)side; (void)sector; (void)buf;
    return -4;  /* FLOPPY_ERR_WRITE */
}

/* ---- MFM codec stubs ---- */

void mfm_init(void) {}

uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc) {
    (void)data; (void)length;
    return crc;
}

int mfm_capture_track(void) {
    return -3;
}

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    (void)sector; (void)data_out;
    return -3;
}
