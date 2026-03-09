#include "fastload_epyx.h"
#include "config.h"
#include <stddef.h>

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

#define IEC_ASSERT(pin)   do { IEC_DDR |= (1 << (pin)); IEC_PORT &= ~(1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

static bool epyx_pending = false;
static uint8_t mw_detect_count = 0;

/*
 * Detect EPYX FastLoad.
 * Detection is command-based: epyx_pending is set by
 * fastload_epyx_check_command() when M-W sequence is found.
 */
static bool epyx_detect(void) {
    if (epyx_pending) {
        epyx_pending = false;
        return true;
    }
    return false;
}

/*
 * Send one byte via EPYX FastLoad protocol.
 *
 * 2-bit parallel transfer with sync handshake.
 */
static bool epyx_send_byte(uint8_t byte, bool eoi) {
    (void)eoi;

    /* Sync: wait for C64 to drop CLK */
    uint16_t timeout = 0;
    while (!IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
        if (IEC_IS_LOW(IEC_PIN_ATN)) return false;
    }

    /* Respond: drop DATA */
    IEC_ASSERT(IEC_PIN_DATA);
    _delay_us(EPYX_HANDSHAKE_US);

    /* 4 rounds: 2 bits per round on CLK+DATA */
    for (uint8_t round = 0; round < 4; round++) {
        uint8_t bit_clk  = (byte >> (round * 2)) & 0x01;
        uint8_t bit_data = (byte >> (round * 2 + 1)) & 0x01;

        if (bit_clk)  { IEC_RELEASE(IEC_PIN_CLK); }
        else          { IEC_ASSERT(IEC_PIN_CLK);  }
        if (bit_data) { IEC_RELEASE(IEC_PIN_DATA); }
        else          { IEC_ASSERT(IEC_PIN_DATA);  }

        _delay_us(EPYX_BIT_PAIR_US);
    }

    /* ACK: release DATA */
    IEC_RELEASE(IEC_PIN_DATA);
    IEC_RELEASE(IEC_PIN_CLK);

    /* Wait for C64 to release CLK */
    timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    _delay_us(EPYX_BYTE_DELAY_US);
    return true;
}

/*
 * Receive one byte via EPYX FastLoad protocol (not used — LOAD only).
 */
static bool epyx_receive_byte(uint8_t *byte, bool *eoi) {
    (void)byte;
    (void)eoi;
    return false;  /* EPYX device is talker-only (LOAD support) */
}

static void epyx_on_atn_end(void) {
    /* No special turnaround */
}

static const fastload_protocol_t epyx_protocol = {
    .type         = FASTLOAD_EPYX,
    .name         = "EPYX",
    .detect       = epyx_detect,
    .send_byte    = epyx_send_byte,
    .receive_byte = epyx_receive_byte,
    .on_atn_end   = epyx_on_atn_end,
};

void fastload_epyx_register(void) {
    fastload_register(&epyx_protocol);
}

bool fastload_epyx_check_command(const uint8_t *buf, uint8_t len) {
    /* Look for M-W prefix: 0x4D 0x2D 0x57 */
    if (len >= 3 &&
        buf[0] == EPYX_MW_M &&
        buf[1] == EPYX_MW_DASH &&
        buf[2] == EPYX_MW_W) {
        mw_detect_count++;
        /* Multiple M-W commands = drive code upload = EPYX */
        if (mw_detect_count >= 3) {
            epyx_pending = true;
        }
        return true;
    }
    return false;
}

void fastload_epyx_reset(void) {
    epyx_pending = false;
    mw_detect_count = 0;
}

#else

/* Host build stub */
static bool epyx_pending = false;
static uint8_t mw_detect_count = 0;

static const fastload_protocol_t epyx_protocol = {
    .type         = FASTLOAD_EPYX,
    .name         = "EPYX",
    .detect       = NULL,
    .send_byte    = NULL,
    .receive_byte = NULL,
    .on_atn_end   = NULL,
};

void fastload_epyx_register(void) {
    fastload_register(&epyx_protocol);
}

bool fastload_epyx_check_command(const uint8_t *buf, uint8_t len) {
    if (len >= 3 && buf[0] == 0x4D && buf[1] == 0x2D && buf[2] == 0x57) {
        mw_detect_count++;
        if (mw_detect_count >= 3) {
            epyx_pending = true;
        }
        return true;
    }
    return false;
}

void fastload_epyx_reset(void) {
    epyx_pending = false;
    mw_detect_count = 0;
}

#endif /* __AVR__ */
