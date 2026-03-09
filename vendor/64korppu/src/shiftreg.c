#include "shiftreg.h"
#include "config.h"

#ifdef __AVR__

#include <avr/io.h>

/*
 * 74HC595 shift register driver.
 *
 * SPI MOSI → SER (data), SPI SCK → SRCLK (shift clock).
 * D6 → RCLK (latch clock): rising edge transfers shift register to outputs.
 *
 * SRAM /CS must be HIGH during shift register operations to prevent
 * SRAM from interpreting the data as a command.
 */

static uint8_t sr_state = SR_DEFAULT;  /* Current output state */

static inline void spi_transfer_byte(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
}

static void latch(void) {
    SR_RCLK_PORT |= (1 << SR_RCLK_PIN);   /* Rising edge latches */
    SR_RCLK_PORT &= ~(1 << SR_RCLK_PIN);  /* Back to low */
}

void shiftreg_init(void) {
    /* RCLK pin as output, start low */
    SR_RCLK_DDR |= (1 << SR_RCLK_PIN);
    SR_RCLK_PORT &= ~(1 << SR_RCLK_PIN);

    /* Write default state (all deasserted) */
    sr_state = SR_DEFAULT;
    shiftreg_write(sr_state);
}

void shiftreg_write(uint8_t value) {
    /* Ensure SRAM /CS is high */
    PORTB |= (1 << SPI_CS_SRAM);

    sr_state = value;
    spi_transfer_byte(value);
    latch();
}

void shiftreg_set_bit(uint8_t bit, uint8_t value) {
    if (value) {
        sr_state |= (1 << bit);
    } else {
        sr_state &= ~(1 << bit);
    }
    shiftreg_write(sr_state);
}

void shiftreg_assert_bit(uint8_t bit) {
    sr_state &= ~(1 << bit);  /* Active-low: clear bit = assert */
    shiftreg_write(sr_state);
}

void shiftreg_release_bit(uint8_t bit) {
    sr_state |= (1 << bit);   /* Active-low: set bit = release */
    shiftreg_write(sr_state);
}

uint8_t shiftreg_get(void) {
    return sr_state;
}

#endif /* __AVR__ */
