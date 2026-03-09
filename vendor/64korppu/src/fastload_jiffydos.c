#include "fastload_jiffydos.h"
#include "config.h"

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

/*
 * JiffyDOS fast-load protocol for ATmega328P.
 *
 * Uses same CLK (PD3) and DATA (PD4) pins as standard IEC,
 * but both carry data simultaneously (2 bits per round).
 */

/* Pin macros (same as iec_protocol.c) */
#define IEC_ASSERT(pin)   do { IEC_DDR |= (1 << (pin)); IEC_PORT &= ~(1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

/*
 * Detect JiffyDOS.
 *
 * After ATN release in TALK mode, JiffyDOS-C64 holds DATA low
 * for ~260µs as a handshake. Measure the pulse width:
 * if 200-320µs → JiffyDOS detected.
 */
static bool jiffy_detect(void) {
    if (!IEC_IS_LOW(IEC_PIN_DATA)) return false;

    uint16_t count = 0;
    while (IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++count > JIFFY_DETECT_MAX_US + 50) return false;
    }

    return (count >= JIFFY_DETECT_MIN_US && count <= JIFFY_DETECT_MAX_US);
}

/*
 * Send one byte via JiffyDOS protocol.
 *
 * 4 rounds, each sends 2 bits on CLK+DATA simultaneously.
 * CLK and DATA are used as data lines (active-low = 0, released = 1).
 */
static bool jiffy_send_byte(uint8_t byte, bool eoi) {
    /* EOI: hold longer delay before sending */
    if (eoi) {
        _delay_us(JIFFY_EOI_DELAY_US);
    }

    /* Wait for listener ready (DATA released) */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    /* 4 rounds: send bit pairs on CLK and DATA */
    for (uint8_t round = 0; round < 4; round++) {
        uint8_t bit_clk  = (byte >> (round * 2)) & 0x01;
        uint8_t bit_data = (byte >> (round * 2 + 1)) & 0x01;

        /* Set CLK and DATA according to bit values */
        /* Active-low: 0 = assert (LOW), 1 = release (HIGH) */
        if (bit_clk)  { IEC_RELEASE(IEC_PIN_CLK); }
        else          { IEC_ASSERT(IEC_PIN_CLK);  }
        if (bit_data) { IEC_RELEASE(IEC_PIN_DATA); }
        else          { IEC_ASSERT(IEC_PIN_DATA);  }

        _delay_us(JIFFY_BIT_PAIR_US);
    }

    /* Release both lines */
    IEC_RELEASE(IEC_PIN_CLK);
    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for listener acknowledge (DATA asserted) */
    timeout = 0;
    while (!IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    _delay_us(JIFFY_BYTE_DELAY_US);
    return true;
}

/*
 * Receive one byte via JiffyDOS protocol.
 */
static bool jiffy_receive_byte(uint8_t *byte, bool *eoi) {
    *byte = 0;
    *eoi = false;

    /* Signal ready by releasing DATA */
    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for talker to start (CLK or DATA changes) */
    /* EOI detection: if talker waits > JIFFY_EOI_DELAY_US, it's EOI */
    uint16_t wait = 0;
    while (!IEC_IS_LOW(IEC_PIN_CLK) && !IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++wait > JIFFY_EOI_DELAY_US) {
            *eoi = true;
            /* Acknowledge EOI */
            IEC_ASSERT(IEC_PIN_DATA);
            _delay_us(JIFFY_BIT_PAIR_US);
            IEC_RELEASE(IEC_PIN_DATA);
            wait = 0;
            while (!IEC_IS_LOW(IEC_PIN_CLK) && !IEC_IS_LOW(IEC_PIN_DATA)) {
                _delay_us(1);
                if (++wait > IEC_TIMEOUT_US) return false;
            }
            break;
        }
        if (IEC_IS_LOW(IEC_PIN_ATN)) return false;
    }

    /* 4 rounds: read bit pairs from CLK and DATA */
    for (uint8_t round = 0; round < 4; round++) {
        uint8_t bit_clk  = IEC_IS_LOW(IEC_PIN_CLK)  ? 0 : 1;
        uint8_t bit_data = IEC_IS_LOW(IEC_PIN_DATA) ? 0 : 1;

        *byte |= (bit_clk  << (round * 2));
        *byte |= (bit_data << (round * 2 + 1));

        _delay_us(JIFFY_BIT_PAIR_US);
    }

    /* Acknowledge by asserting DATA */
    IEC_ASSERT(IEC_PIN_DATA);
    _delay_us(JIFFY_BYTE_DELAY_US);

    return true;
}

static void jiffy_on_atn_end(void) {
    /* JiffyDOS needs CLK asserted after ATN turnaround */
    IEC_ASSERT(IEC_PIN_CLK);
    _delay_us(JIFFY_TURNAROUND_US);
}

static const fastload_protocol_t jiffy_protocol = {
    .type        = FASTLOAD_JIFFYDOS,
    .name        = "JiffyDOS",
    .detect      = jiffy_detect,
    .send_byte   = jiffy_send_byte,
    .receive_byte = jiffy_receive_byte,
    .on_atn_end  = jiffy_on_atn_end,
};

void fastload_jiffydos_register(void) {
    fastload_register(&jiffy_protocol);
}

#else

/* Host build: register with NULL function pointers for testing registry */
static const fastload_protocol_t jiffy_protocol = {
    .type        = FASTLOAD_JIFFYDOS,
    .name        = "JiffyDOS",
    .detect      = NULL,
    .send_byte   = NULL,
    .receive_byte = NULL,
    .on_atn_end  = NULL,
};

void fastload_jiffydos_register(void) {
    fastload_register(&jiffy_protocol);
}

#endif /* __AVR__ */
