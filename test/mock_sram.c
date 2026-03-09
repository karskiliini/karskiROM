/*
 * mock_sram.c — Host implementation of the 23LC512 SPI SRAM driver.
 *
 * Replaces vendor/64korppu/src/sram.c.  Every operation is backed by
 * mock_sram[MOCK_SRAM_SIZE] from mock_hardware.h, with simple
 * bounds-checked memcpy.
 */

#include "mock_hardware.h"
#include "sram.h"

/* Sequential-access cursor (mirrors real chip auto-increment) */
static uint32_t seq_addr;

void sram_init(void) {
    /* Nothing to do on host — mock_hardware_init() zeroes the array */
}

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint32_t a = (addr + i) & (MOCK_SRAM_SIZE - 1);
        buf[i] = mock_sram[a];
    }
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint32_t a = (addr + i) & (MOCK_SRAM_SIZE - 1);
        mock_sram[a] = buf[i];
    }
}

uint8_t sram_read_byte(uint32_t addr) {
    return mock_sram[addr & (MOCK_SRAM_SIZE - 1)];
}

void sram_write_byte(uint32_t addr, uint8_t byte) {
    mock_sram[addr & (MOCK_SRAM_SIZE - 1)] = byte;
}

/* Sequential write — matches real chip: CS stays asserted, address auto-increments */
void sram_begin_seq_write(uint32_t addr) {
    seq_addr = addr;
}

void sram_seq_write_byte(uint8_t byte) {
    mock_sram[seq_addr & (MOCK_SRAM_SIZE - 1)] = byte;
    seq_addr++;
}

void sram_end_seq(void) {
    /* Nothing to do — no CS line on host */
}

/* Sequential read */
void sram_begin_seq_read(uint32_t addr) {
    seq_addr = addr;
}

uint8_t sram_seq_read_byte(void) {
    uint8_t val = mock_sram[seq_addr & (MOCK_SRAM_SIZE - 1)];
    seq_addr++;
    return val;
}
