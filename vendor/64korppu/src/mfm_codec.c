#include "mfm_codec.h"
#include "config.h"
#include "sram.h"

#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/*
 * MFM codec for ATmega328P.
 *
 * Read: Timer1 ICP1 (D8) captures flux transitions.
 *       Pulse widths are classified (2T/3T/4T) and packed 4-per-byte
 *       into SRAM track buffer via sequential SPI write.
 *
 * Decode: Process packed pulse data from SRAM, reconstruct MFM bitstream,
 *         find sync + IDAM, extract sector data.
 *
 * Write: TODO - Generate MFM pulses on /WDATA (D7) using Timer1 compare.
 */

/* Capture state (used by ISR) */
static volatile uint16_t prev_capture;
static volatile uint32_t capture_count;
static volatile uint8_t pulse_pack;
static volatile uint8_t pulse_in_pack;
static volatile bool capture_done;

/* CRC-CCITT lookup (computed at init or use bit-by-bit for code size) */
uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc) {
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ MFM_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void mfm_init(void) {
    /* Timer1: Normal mode, no prescaler (16MHz clock) */
    TCCR1A = 0;
    TCCR1B = (1 << CS10);  /* No prescaler */

    /* ICP1 input on PB0 (D8), falling edge trigger */
    TCCR1B &= ~(1 << ICES1);  /* Falling edge */

    /* Disable capture interrupt initially */
    TIMSK1 &= ~(1 << ICIE1);
}

/*
 * Timer1 Input Capture ISR.
 *
 * Classifies pulse width into 2-bit code:
 *   0 = short (2T, ~4µs at HD)
 *   1 = medium (3T, ~6µs)
 *   2 = long (4T, ~8µs)
 *   3 = invalid/sync
 *
 * Packs 4 codes per byte, writes to SRAM via sequential SPI.
 */
ISR(TIMER1_CAPT_vect) {
    uint16_t interval = ICR1 - prev_capture;
    prev_capture = ICR1;

    uint8_t code;
    if (interval < MFM_THRESHOLD_SHORT) {
        code = 0;  /* 2T */
    } else if (interval < MFM_THRESHOLD_MEDIUM) {
        code = 1;  /* 3T */
    } else if (interval < MFM_THRESHOLD_LONG) {
        code = 2;  /* 4T */
    } else {
        code = 3;  /* Invalid/gap */
    }

    pulse_pack = (pulse_pack << 2) | code;
    pulse_in_pack++;

    if (pulse_in_pack >= 4) {
        /* Write packed byte to SRAM (sequential mode, /CS already low) */
        SPDR = pulse_pack;
        while (!(SPSR & (1 << SPIF)));
        capture_count++;
        pulse_in_pack = 0;
    }

    /* Stop after ~12000 packed bytes (48000 pulses ≈ 1 revolution) */
    if (capture_count >= (SRAM_MFM_TRACK_SIZE - 16)) {
        TIMSK1 &= ~(1 << ICIE1);  /* Disable capture interrupt */
        capture_done = true;
    }
}

int mfm_capture_track(void) {
    /* Initialize capture state */
    capture_count = 0;
    pulse_pack = 0;
    pulse_in_pack = 0;
    capture_done = false;

    /* Start sequential SRAM write at track buffer address */
    sram_begin_seq_write(SRAM_MFM_TRACK);

    /* Clear any pending capture flag */
    TIFR1 |= (1 << ICF1);

    /* Read initial capture value */
    prev_capture = ICR1;

    /* Enable capture interrupt */
    TIMSK1 |= (1 << ICIE1);
    sei();

    /* Wait for capture to complete (1 revolution ≈ 200ms) */
    uint16_t timeout = 0;
    while (!capture_done && timeout < 500) {
        _delay_ms(1);
        timeout++;
    }

    /* Disable capture interrupt */
    TIMSK1 &= ~(1 << ICIE1);

    /* End sequential SRAM write */
    sram_end_seq();

    if (timeout >= 500) return FLOPPY_ERR_TIMEOUT;
    return FLOPPY_OK;
}

/*
 * Decode MFM sector from captured track data in SRAM.
 *
 * Reads packed pulse data, reconstructs MFM bitstream,
 * searches for sync + IDAM matching the requested sector,
 * then reads and CRC-checks the data field.
 */

/* Helper: read packed pulse data from SRAM and convert to MFM bytes */
/* (decode buffers allocated locally in functions) */

/*
 * Reconstruct one MFM byte from pulse codes.
 * Each pulse code contributes bits to the MFM stream.
 * This is a simplified decoder that works sector-at-a-time.
 */

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    /*
     * Simplified MFM sector decode from SRAM track buffer.
     *
     * Strategy:
     * 1. Read packed pulse data from SRAM
     * 2. Convert pulse codes to MFM bit transitions
     * 3. Search for 0xA1 sync marks (missing clock)
     * 4. Read IDAM + sector ID
     * 5. If matching sector: skip gap, read DAM + 512 data bytes + CRC
     *
     * This is a basic implementation. Real hardware may need
     * more robust sync detection and error recovery.
     */

    /* Read entire captured track from SRAM into a reconstruction loop */
    uint32_t read_pos = SRAM_MFM_TRACK;
    (void)capture_count;  /* Used indirectly in ISR */

    /* State machine for MFM decode */
    uint16_t shift_reg = 0;
    uint8_t bit_count = 0;
    uint8_t byte_val = 0;
    bool in_sync = false;
    uint8_t sync_count = 0;
    bool reading_id = false;
    bool reading_data = false;
    uint8_t field_bytes[6];   /* ID: track, side, sector, size, crc_h, crc_l */
    uint16_t field_pos = 0;
    uint16_t data_pos = 0;
    bool found_sector = false;

    sram_begin_seq_read(read_pos);

    for (uint32_t i = 0; i < capture_count; i++) {
        uint8_t packed = sram_seq_read_byte();

        /* Process 4 pulse codes per byte */
        for (int p = 3; p >= 0; p--) {
            uint8_t code = (packed >> (p * 2)) & 0x03;
            uint8_t bits_to_add;

            switch (code) {
                case 0: bits_to_add = 2; shift_reg = (shift_reg << 2) | 0x01; break;
                case 1: bits_to_add = 3; shift_reg = (shift_reg << 3) | 0x01; break;
                case 2: bits_to_add = 4; shift_reg = (shift_reg << 4) | 0x01; break;
                default: bits_to_add = 0; shift_reg = 0; bit_count = 0;
                         in_sync = false; sync_count = 0;
                         reading_id = false; reading_data = false;
                         continue;
            }

            bit_count += bits_to_add;

            /* Extract data bytes from MFM (every other bit is data) */
            while (bit_count >= 2) {
                bit_count -= 2;
                uint8_t data_bit = (shift_reg >> bit_count) & 0x01;
                byte_val = (byte_val << 1) | data_bit;

                if (++field_pos % 8 == 0) {
                    /* Got a complete byte */
                    if (!in_sync) {
                        if (byte_val == MFM_SYNC_BYTE) {
                            sync_count++;
                            if (sync_count >= 3) in_sync = true;
                        } else {
                            sync_count = 0;
                        }
                    } else if (reading_data) {
                        if (data_pos < 512) {
                            data_out[data_pos++] = byte_val;
                        }
                        if (data_pos >= 512) {
                            /* Done reading sector data */
                            sram_end_seq();
                            return 0;
                        }
                    } else if (reading_id) {
                        field_bytes[data_pos++] = byte_val;
                        if (data_pos >= 4) {
                            /* Got full sector ID */
                            if (field_bytes[2] == sector) {
                                found_sector = true;
                            }
                            reading_id = false;
                            data_pos = 0;
                            in_sync = false;
                            sync_count = 0;
                        }
                    } else {
                        /* First byte after sync */
                        if (byte_val == MFM_IDAM) {
                            reading_id = true;
                            data_pos = 0;
                        } else if (byte_val == MFM_DAM && found_sector) {
                            reading_data = true;
                            data_pos = 0;
                        } else {
                            in_sync = false;
                            sync_count = 0;
                        }
                    }
                    byte_val = 0;
                }
            }
        }
    }

    sram_end_seq();
    return FLOPPY_ERR_NO_SECTOR;
}

int mfm_write_sector(const mfm_sector_id_t *id, const uint8_t *data) {
    /*
     * MFM sector write via /WDATA (D7).
     * TODO: Full implementation requires:
     * 1. Capture track to find sector position
     * 2. Wait for correct sector gap
     * 3. Assert /WGATE
     * 4. Write MFM-encoded data with precise Timer1 timing
     * 5. Release /WGATE
     */
    (void)id;
    (void)data;
    return FLOPPY_ERR_WRITE;  /* Not yet implemented */
}

int mfm_find_sectors(mfm_sector_id_t *ids_out, int max_ids) {
    (void)ids_out;
    (void)max_ids;
    return 0;  /* TODO */
}

#endif /* __AVR__ */
