#include "fastload_burst.h"
#include "config.h"
#include <stddef.h>

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

#define IEC_ASSERT(pin)   do { IEC_DDR |= (1 << (pin)); IEC_PORT &= ~(1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

static bool burst_pending = false;

/*
 * Detect Burst mode.
 * Detection is command-based: burst_pending is set by
 * fastload_burst_check_command() when "U0" is received.
 */
static bool burst_detect(void) {
    if (burst_pending) {
        burst_pending = false;
        return true;
    }
    return false;
}

/*
 * Send one byte via Burst protocol.
 * Clock-synchronized serial: set DATA, pulse CLK, 8 times.
 */
static bool burst_send_byte(uint8_t byte, bool eoi) {
    (void)eoi;  /* Burst uses separate EOI signaling */

    for (uint8_t bit = 0; bit < 8; bit++) {
        /* Set DATA line */
        if (byte & (1 << bit)) {
            IEC_RELEASE(IEC_PIN_DATA);
        } else {
            IEC_ASSERT(IEC_PIN_DATA);
        }
        _delay_us(BURST_SETUP_US);

        /* Pulse CLK */
        IEC_RELEASE(IEC_PIN_CLK);
        _delay_us(BURST_CLK_PULSE_US);
        IEC_ASSERT(IEC_PIN_CLK);
    }

    IEC_RELEASE(IEC_PIN_DATA);
    _delay_us(BURST_BYTE_DELAY_US);
    return true;
}

/*
 * Receive one byte via Burst protocol.
 */
static bool burst_receive_byte(uint8_t *byte, bool *eoi) {
    *byte = 0;
    *eoi = false;

    for (uint8_t bit = 0; bit < 8; bit++) {
        /* Wait for CLK released (rising edge) */
        uint16_t timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }

        /* Read DATA */
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }

        /* Wait for CLK asserted (falling edge) */
        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
    }

    return true;
}

static void burst_on_atn_end(void) {
    /* No special turnaround needed for Burst */
}

static const fastload_protocol_t burst_protocol = {
    .type         = FASTLOAD_BURST,
    .name         = "Burst",
    .detect       = burst_detect,
    .send_byte    = burst_send_byte,
    .receive_byte = burst_receive_byte,
    .on_atn_end   = burst_on_atn_end,
};

void fastload_burst_register(void) {
    fastload_register(&burst_protocol);
}

bool fastload_burst_check_command(const char *cmd, uint8_t len) {
    if (len >= 2 && cmd[0] == BURST_CMD_PREFIX && cmd[1] == BURST_CMD_SUBCMD) {
        burst_pending = true;
        return true;
    }
    return false;
}

#else

/* Host build stub */
static bool burst_pending = false;

static const fastload_protocol_t burst_protocol = {
    .type         = FASTLOAD_BURST,
    .name         = "Burst",
    .detect       = NULL,
    .send_byte    = NULL,
    .receive_byte = NULL,
    .on_atn_end   = NULL,
};

void fastload_burst_register(void) {
    fastload_register(&burst_protocol);
}

bool fastload_burst_check_command(const char *cmd, uint8_t len) {
    if (len >= 2 && cmd[0] == 'U' && cmd[1] == '0') {
        burst_pending = true;
        return true;
    }
    return false;
}

#endif /* __AVR__ */
