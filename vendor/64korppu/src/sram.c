#include "sram.h"
#include "config.h"

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

/*
 * 23LC512 SPI SRAM driver for ATmega328P.
 *
 * SPI at F_CPU/2 = 8 MHz (maximum for 23LC512 at 5V).
 * Sequential mode for bulk transfers.
 * /CS on PB2 (D10).
 *
 * 23LC512 uses 16-bit addresses (64KB), same as 23LC256 (32KB).
 * Drop-in upgrade: same pinout, same SPI protocol, double capacity.
 */

#define CS_LOW()    (PORTB &= ~(1 << SPI_CS_SRAM))
#define CS_HIGH()   (PORTB |= (1 << SPI_CS_SRAM))

static inline uint8_t spi_transfer(uint8_t data) {
    SPDR = data;
    while (!(SPSR & (1 << SPIF)));
    return SPDR;
}

static void sram_send_addr(uint32_t addr) {
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
}

void sram_init(void) {
    /* Configure SPI pins: MOSI, SCK, /CS as outputs */
    DDRB |= (1 << SPI_MOSI) | (1 << SPI_SCK) | (1 << SPI_CS_SRAM);
    DDRB &= ~(1 << SPI_MISO);  /* MISO as input */

    CS_HIGH();  /* Deselect SRAM */

    /* Enable SPI, Master mode, F_CPU/2 (8MHz) */
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR |= (1 << SPI2X);  /* Double speed */

    /* Set sequential mode */
    CS_LOW();
    spi_transfer(SRAM_CMD_WRMR);
    spi_transfer(SRAM_MODE_SEQ);
    CS_HIGH();
}

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    CS_LOW();
    spi_transfer(SRAM_CMD_READ);
    sram_send_addr(addr);
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(0x00);
    }
    CS_HIGH();
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    CS_LOW();
    spi_transfer(SRAM_CMD_WRITE);
    sram_send_addr(addr);
    for (uint16_t i = 0; i < len; i++) {
        spi_transfer(buf[i]);
    }
    CS_HIGH();
}

uint8_t sram_read_byte(uint32_t addr) {
    uint8_t val;
    CS_LOW();
    spi_transfer(SRAM_CMD_READ);
    sram_send_addr(addr);
    val = spi_transfer(0x00);
    CS_HIGH();
    return val;
}

void sram_write_byte(uint32_t addr, uint8_t byte) {
    CS_LOW();
    spi_transfer(SRAM_CMD_WRITE);
    sram_send_addr(addr);
    spi_transfer(byte);
    CS_HIGH();
}

void sram_begin_seq_write(uint32_t addr) {
    CS_LOW();
    spi_transfer(SRAM_CMD_WRITE);
    sram_send_addr(addr);
    /* /CS stays low, address auto-increments on each write */
}

void sram_seq_write_byte(uint8_t byte) {
    SPDR = byte;
    while (!(SPSR & (1 << SPIF)));
}

void sram_end_seq(void) {
    CS_HIGH();
}

void sram_begin_seq_read(uint32_t addr) {
    CS_LOW();
    spi_transfer(SRAM_CMD_READ);
    sram_send_addr(addr);
}

uint8_t sram_seq_read_byte(void) {
    SPDR = 0x00;
    while (!(SPSR & (1 << SPIF)));
    return SPDR;
}

#endif /* __AVR__ */
