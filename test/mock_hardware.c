/*
 * mock_hardware.c — Definitions for mock AVR hardware registers and helpers.
 */

#include "mock_hardware.h"

/* AVR register backing variables */
uint8_t mock_DDRD;
uint8_t mock_PORTD;
uint8_t mock_PIND;

uint8_t mock_DDRB;
uint8_t mock_PORTB;
uint8_t mock_PINB;

uint8_t mock_DDRC;
uint8_t mock_PORTC;
uint8_t mock_PINC;

uint8_t mock_SPDR;
uint8_t mock_SPSR;
uint8_t mock_SPCR;

/* Timing accumulator */
uint64_t mock_delay_us_total;

/* SRAM backing array */
uint8_t mock_sram[MOCK_SRAM_SIZE];

void mock_delay_us(double us) {
    if (us > 0) {
        mock_delay_us_total += (uint64_t)us;
    }
}

void mock_delay_ms(double ms) {
    if (ms > 0) {
        mock_delay_us_total += (uint64_t)(ms * 1000.0);
    }
}

void mock_hardware_init(void) {
    mock_DDRD  = 0;
    mock_PORTD = 0;
    mock_PIND  = 0;

    mock_DDRB  = 0;
    mock_PORTB = 0;
    mock_PINB  = 0;

    mock_DDRC  = 0;
    mock_PORTC = 0;
    mock_PINC  = 0;

    mock_SPDR  = 0;
    mock_SPSR  = 0;
    mock_SPCR  = 0;

    mock_delay_us_total = 0;

    memset(mock_sram, 0, MOCK_SRAM_SIZE);
}
