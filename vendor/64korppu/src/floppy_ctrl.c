#include "floppy_ctrl.h"
#include "config.h"
#include "shiftreg.h"
#include "mfm_codec.h"

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

/*
 * Floppy drive control for Arduino Nano.
 *
 * Output signals (/DENSITY, /MOTEA, /DRVSEL, /MOTOR, /DIR, /STEP,
 * /WGATE, /SIDE1) go through 74HC595 shift register.
 *
 * Input signals (/TRK00, /WPT, /DSKCHG) are direct GPIO on A0-A2.
 * /RDATA on D8 (Timer1 ICP1) for MFM capture.
 * /WDATA on D7 (direct GPIO) for MFM write.
 */

static floppy_state_t state = {0};

void floppy_init(void) {
    /* Input pins with pull-ups: /TRK00 (A0), /WPT (A1), /DSKCHG (A2) */
    FLOPPY_IN_DDR &= ~((1 << FLOPPY_TRK00_PIN) |
                        (1 << FLOPPY_WPT_PIN) |
                        (1 << FLOPPY_DSKCHG_PIN));
    FLOPPY_IN_PORT |= (1 << FLOPPY_TRK00_PIN) |
                      (1 << FLOPPY_WPT_PIN) |
                      (1 << FLOPPY_DSKCHG_PIN);

    /* /RDATA (D8 = PB0) as input */
    DDRB &= ~(1 << FLOPPY_RDATA_PIN);

    /* /WDATA (D7 = PD7) as output, HIGH (deasserted) */
    FLOPPY_WDATA_DDR |= (1 << FLOPPY_WDATA_PIN);
    FLOPPY_WDATA_PORT |= (1 << FLOPPY_WDATA_PIN);

    /* LED (D9 = PB1) as output */
    DDRB |= (1 << FLOPPY_LED_PIN);

    /* Set shift register: HD density, drive selected, everything else deasserted */
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);  /* Assert /DENSITY (LOW = HD) */
    sr &= ~(1 << SR_BIT_DRVSEL);   /* Assert /DRVSEL (select drive) */
    shiftreg_write(sr);

    state.current_track = 0;
    state.current_side = 0;
    state.motor_on = false;
    state.disk_present = true;
    state.write_protected = false;
    state.initialized = false;
}

void floppy_motor_on(void) {
    if (!state.motor_on) {
        shiftreg_assert_bit(SR_BIT_MOTEA);
        shiftreg_assert_bit(SR_BIT_MOTOR);
        _delay_ms(FLOPPY_MOTOR_SPIN_MS);
        state.motor_on = true;
        PORTB |= (1 << FLOPPY_LED_PIN);  /* LED on */
    }
}

void floppy_motor_off(void) {
    shiftreg_release_bit(SR_BIT_MOTEA);
    shiftreg_release_bit(SR_BIT_MOTOR);
    state.motor_on = false;
    PORTB &= ~(1 << FLOPPY_LED_PIN);  /* LED off */
}

static void step_once(bool inward) {
    /* Set direction: assert /DIR = inward (toward center) */
    if (inward) {
        shiftreg_assert_bit(SR_BIT_DIR);
    } else {
        shiftreg_release_bit(SR_BIT_DIR);
    }
    _delay_us(10);

    /* Step pulse */
    shiftreg_assert_bit(SR_BIT_STEP);
    _delay_us(FLOPPY_STEP_PULSE_US);
    shiftreg_release_bit(SR_BIT_STEP);
    _delay_ms(FLOPPY_STEP_RATE_MS);
}

int floppy_recalibrate(void) {
    for (int i = 0; i < 85; i++) {
        if (!(FLOPPY_IN_PINR & (1 << FLOPPY_TRK00_PIN))) {
            /* /TRK00 active low - we're at track 0 */
            state.current_track = 0;
            state.initialized = true;
            _delay_ms(FLOPPY_STEP_SETTLE_MS);
            return FLOPPY_OK;
        }
        step_once(false);  /* Step outward */
    }
    return FLOPPY_ERR_NO_TRK0;
}

int floppy_seek(uint8_t track) {
    if (track >= FLOPPY_TRACKS) return FLOPPY_ERR_SEEK;

    if (!state.initialized) {
        int rc = floppy_recalibrate();
        if (rc != FLOPPY_OK) return rc;
    }

    while (state.current_track != track) {
        if (state.current_track < track) {
            step_once(true);
            state.current_track++;
        } else {
            step_once(false);
            state.current_track--;
        }
    }

    _delay_ms(FLOPPY_STEP_SETTLE_MS);
    return FLOPPY_OK;
}

void floppy_select_side(uint8_t side) {
    if (side) {
        shiftreg_assert_bit(SR_BIT_SIDE1);   /* LOW = side 1 */
    } else {
        shiftreg_release_bit(SR_BIT_SIDE1);  /* HIGH = side 0 */
    }
    state.current_side = side;
    _delay_us(100);
}

int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector,
                        uint8_t *buf) {
    if (sector < 1 || sector > FLOPPY_SECTORS_HD) return FLOPPY_ERR_NO_SECTOR;

    /* Check write protect */
    state.write_protected = !(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN));

    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) return rc;

    floppy_select_side(side);

    /* Capture raw MFM track to SRAM */
    rc = mfm_capture_track();
    if (rc != 0) return FLOPPY_ERR_READ;

    /* Decode sector from SRAM track buffer */
    rc = mfm_decode_sector(sector, buf);
    if (rc != 0) return FLOPPY_ERR_READ;

    return FLOPPY_OK;
}

int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector,
                         const uint8_t *buf) {
    if (sector < 1 || sector > FLOPPY_SECTORS_HD) return FLOPPY_ERR_NO_SECTOR;

    if (!(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN))) {
        return FLOPPY_ERR_WP;
    }

    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) return rc;

    floppy_select_side(side);

    /* Encode and write sector */
    mfm_sector_id_t id = {
        .track = track,
        .side = side,
        .sector = sector,
        .size_code = 2,
    };

    rc = mfm_write_sector(&id, buf);
    if (rc != 0) return FLOPPY_ERR_WRITE;

    return FLOPPY_OK;
}

floppy_state_t floppy_get_state(void) {
    state.write_protected = !(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN));
    return state;
}

#endif /* __AVR__ */
