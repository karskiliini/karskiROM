#ifndef SRAM_H
#define SRAM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * 23LC512 SPI SRAM driver (64KB).
 *
 * SPI commands (same as 23LC256/23LC1024):
 *   0x03 = READ   - Read data from address
 *   0x02 = WRITE  - Write data to address
 *   0x05 = RDMR   - Read mode register
 *   0x01 = WRMR   - Write mode register
 *
 * Modes:
 *   0x00 = Byte mode (default)
 *   0x40 = Sequential mode (auto-increment address)
 *   0x80 = Page mode (wrap at 32-byte boundary)
 *
 * We use sequential mode for bulk transfers and
 * byte mode for random access.
 *
 * 16-bit addresses (vs 24-bit on 23LC1024). Same DIP-8 pinout as 23LC256.
 * Drop-in upgrade from 23LC256: same pinout, same SPI protocol, double capacity.
 */

#define SRAM_CMD_READ    0x03
#define SRAM_CMD_WRITE   0x02
#define SRAM_CMD_RDMR    0x05
#define SRAM_CMD_WRMR    0x01

#define SRAM_MODE_BYTE   0x00
#define SRAM_MODE_SEQ    0x40
#define SRAM_MODE_PAGE   0x80

void sram_init(void);

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len);
void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len);

uint8_t sram_read_byte(uint32_t addr);
void sram_write_byte(uint32_t addr, uint8_t byte);

/* Sequential write for MFM capture (ISR-safe) */
void sram_begin_seq_write(uint32_t addr);
void sram_seq_write_byte(uint8_t byte);
void sram_end_seq(void);

/* Sequential read */
void sram_begin_seq_read(uint32_t addr);
uint8_t sram_seq_read_byte(void);

#endif /* SRAM_H */
