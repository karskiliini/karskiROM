/*
 * mock_hardware.h — Replace AVR-specific registers and functions
 *                   with host-accessible globals for testing.
 *
 * Allows vendor/64korppu firmware code (written for ATmega328P) to
 * compile and run on macOS / Linux host.
 */

#ifndef MOCK_HARDWARE_H
#define MOCK_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- Mock AVR registers ---- */

extern uint8_t mock_DDRD;
extern uint8_t mock_PORTD;
extern uint8_t mock_PIND;

extern uint8_t mock_DDRB;
extern uint8_t mock_PORTB;
extern uint8_t mock_PINB;

extern uint8_t mock_DDRC;
extern uint8_t mock_PORTC;
extern uint8_t mock_PINC;

extern uint8_t mock_SPDR;
extern uint8_t mock_SPSR;
extern uint8_t mock_SPCR;

/* Map AVR register names to mock globals */
#define DDRD   mock_DDRD
#define PORTD  mock_PORTD
#define PIND   mock_PIND

#define DDRB   mock_DDRB
#define PORTB  mock_PORTB
#define PINB   mock_PINB

#define DDRC   mock_DDRC
#define PORTC  mock_PORTC
#define PINC   mock_PINC

#define SPDR   mock_SPDR
#define SPSR   mock_SPSR
#define SPCR   mock_SPCR

/* ---- AVR pin aliases ---- */

#define PD2  2
#define PD3  3
#define PD4  4
#define PD5  5
#define PD6  6
#define PD7  7

#define PB0  0
#define PB1  1
#define PB2  2
#define PB3  3
#define PB4  4
#define PB5  5

#define PC0  0
#define PC1  1
#define PC2  2

/* ---- SPI status/control bits ---- */

#define SPIF   7
#define SPE    6
#define MSTR   4
#define SPR0   0
#define SPI2X  0   /* In SPSR, bit 0 = SPI2X double-speed */

/* ---- AVR utility macros ---- */

#define _BV(bit)  (1 << (bit))

/* PROGMEM / pgm_read — on host, flash == RAM */
#define PROGMEM              /* nothing */
#define pgm_read_byte(addr)  (*(const uint8_t *)(addr))
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))

/* F_CPU — typical ATmega328P @ 16 MHz */
#ifndef F_CPU
#define F_CPU  16000000UL
#endif

/* ---- Timing mocks ---- */

extern uint64_t mock_delay_us_total;

void mock_delay_us(double us);
void mock_delay_ms(double ms);

#define _delay_us(us)  mock_delay_us(us)
#define _delay_ms(ms)  mock_delay_ms(ms)

/* ---- Interrupt no-ops ---- */

#define cli()  ((void)0)
#define sei()  ((void)0)

/* ---- UART no-ops ---- */

static inline void uart_init(void) {}
static inline void uart_putc(char c) { (void)c; }
static inline void uart_puts(const char *s) { (void)s; }

/* ---- Mock SRAM backing store ---- */

#define MOCK_SRAM_SIZE  65536
extern uint8_t mock_sram[MOCK_SRAM_SIZE];

/* ---- Reset everything ---- */

void mock_hardware_init(void);

#endif /* MOCK_HARDWARE_H */
