#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Hardware configuration for Alternative E:
 * Arduino Nano (ATmega328P) + 23LC512 SPI SRAM + 74HC595 shift register
 */

/* --- SRAM Memory Map (23LC512, 64KB) --- */

#define SRAM_SIZE           0x10000UL   /* 65536 bytes */

#define SRAM_MFM_TRACK      0x00000UL   /* MFM raw track buffer */
#define SRAM_MFM_TRACK_SIZE 12496

#define SRAM_FAT_CACHE      0x030D0UL   /* FAT table cache (9 sectors) */
#define SRAM_FAT_CACHE_SIZE 4608        /* 9 * 512 */

#define SRAM_SECTOR_BUF1    0x042D0UL   /* Sector buffer #1 */
#define SRAM_SECTOR_BUF2    0x044D0UL   /* Sector buffer #2 */
#define SRAM_SECTOR_SIZE    512

#define SRAM_DIR_BUF        0x046D0UL   /* Directory listing buffer */
#define SRAM_DIR_BUF_SIZE   4096

#define SRAM_IEC_BUF        0x056D0UL   /* IEC channel buffer */
#define SRAM_IEC_BUF_SIZE   512

/* LZ4 compression buffers (in external SRAM, not internal RAM) */
#define SRAM_COMP_RAW       0x058D0UL   /* Raw data before compression */
#define SRAM_COMP_RAW_SIZE  512         /* = COMPRESS_BLOCK_SIZE */
#define SRAM_COMP_FRAME     0x05AD0UL   /* Framed LZ4 output */
#define SRAM_COMP_FRAME_SIZE 556        /* = COMPRESS_FRAME_BUF_SIZE */
                                        /* Vapaa: 0x05D0C - 0x0FFFF = ~41.2 KB */

/* --- Floppy Geometry --- */

#define FLOPPY_TRACKS         80
#define FLOPPY_SIDES           2
#define FLOPPY_SECTORS_HD     18
#define FLOPPY_SECTOR_SIZE   512
#define FLOPPY_TOTAL_SECTORS (FLOPPY_TRACKS * FLOPPY_SIDES * FLOPPY_SECTORS_HD)

/* Timing constants (microseconds) */
#define FLOPPY_STEP_PULSE_US    3
#define FLOPPY_STEP_SETTLE_MS  15
#define FLOPPY_MOTOR_SPIN_MS  500
#define FLOPPY_STEP_RATE_MS     3

/* Error codes */
#define FLOPPY_OK               0
#define FLOPPY_ERR_NO_TRK0    -1
#define FLOPPY_ERR_SEEK        -2
#define FLOPPY_ERR_READ        -3
#define FLOPPY_ERR_WRITE       -4
#define FLOPPY_ERR_WP          -5
#define FLOPPY_ERR_NO_DISK     -6
#define FLOPPY_ERR_CRC         -7
#define FLOPPY_ERR_NO_SECTOR   -8
#define FLOPPY_ERR_TIMEOUT     -9

/* --- AVR-specific Pin Assignments --- */

#ifdef __AVR__

#include <avr/io.h>

/* IEC bus (direct GPIO, active-low open-collector) */
#define IEC_PIN_ATN      PD2    /* INT0 interrupt */
#define IEC_PIN_CLK      PD3
#define IEC_PIN_DATA     PD4
#define IEC_PIN_RESET    PD5
#define IEC_DDR          DDRD
#define IEC_PORT         PORTD
#define IEC_PINR         PIND

/* SPI (shared SRAM + 74HC595) */
#define SPI_CS_SRAM      PB2    /* D10 = /CS_SRAM */
#define SPI_MOSI         PB3    /* D11 */
#define SPI_MISO         PB4    /* D12 */
#define SPI_SCK          PB5    /* D13 */

/* 74HC595 latch */
#define SR_RCLK_PIN      PD6    /* D6 = RCLK (latch) */
#define SR_RCLK_DDR      DDRD
#define SR_RCLK_PORT     PORTD

/* Floppy direct GPIO */
#define FLOPPY_WDATA_PIN    PD7    /* D7 = /WDATA (direct for timing) */
#define FLOPPY_WDATA_DDR    DDRD
#define FLOPPY_WDATA_PORT   PORTD
#define FLOPPY_RDATA_PIN    PB0    /* D8 = /RDATA (ICP1 input capture) */
#define FLOPPY_LED_PIN      PB1    /* D9 = LED */

/* Floppy input (active-low, internal pull-up) */
#define FLOPPY_TRK00_PIN    PC0    /* A0 = /TRK00 */
#define FLOPPY_WPT_PIN      PC1    /* A1 = /WPT */
#define FLOPPY_DSKCHG_PIN   PC2    /* A2 = /DSKCHG */
#define FLOPPY_IN_DDR       DDRC
#define FLOPPY_IN_PORT      PORTC
#define FLOPPY_IN_PINR      PINC

#endif /* __AVR__ */

/* --- 74HC595 Shift Register Bit Assignments --- */

#define SR_BIT_SIDE1     0    /* QA (pin 15) = /SIDE1 */
#define SR_BIT_DENSITY   1    /* QB (pin  1) = /DENSITY */
#define SR_BIT_MOTEA     2    /* QC (pin  2) = /MOTEA */
#define SR_BIT_DRVSEL    3    /* QD (pin  3) = /DRVSEL */
#define SR_BIT_MOTOR     4    /* QE (pin  4) = /MOTOR */
#define SR_BIT_DIR       5    /* QF (pin  5) = /DIR */
#define SR_BIT_STEP      6    /* QG (pin  6) = /STEP */
#define SR_BIT_WGATE     7    /* QH (pin  7) = /WGATE */

/* Default: all deasserted (HIGH for active-low signals) */
#define SR_DEFAULT       0xFF

/* --- IEC Protocol --- */

#define IEC_DEFAULT_DEVICE   8
#define IEC_NUM_CHANNELS    16

/* IEC bus commands (under ATN) */
#define IEC_CMD_LISTEN      0x20
#define IEC_CMD_UNLISTEN    0x3F
#define IEC_CMD_TALK        0x40
#define IEC_CMD_UNTALK      0x5F
#define IEC_CMD_OPEN        0x60
#define IEC_CMD_CLOSE       0xE0

/* Secondary addresses */
#define IEC_SA_LOAD          0
#define IEC_SA_SAVE          1
#define IEC_SA_COMMAND      15

/* Timing (microseconds) */
#define IEC_TIMING_LISTENER_HOLD    80
#define IEC_TIMING_TALKER_SETUP     80
#define IEC_TIMING_CLK_LOW          60
#define IEC_TIMING_CLK_HIGH         60
#define IEC_TIMING_EOI_TIMEOUT     200
#define IEC_TIMING_EOI_ACK          60
#define IEC_TIMING_BETWEEN_BYTES   100
#define IEC_TIMEOUT_US           10000

/* --- MFM Codec --- */

/* At 16MHz timer, HD 500kbps: bit cell = 2µs = 32 ticks */
#define MFM_TICKS_PER_CELL_HD    32
/* Pulse classification thresholds (Timer1 ticks at 16MHz) */
#define MFM_THRESHOLD_SHORT      80     /* < 80: 2T (short) */
#define MFM_THRESHOLD_MEDIUM    112     /* < 112: 3T (medium) */
#define MFM_THRESHOLD_LONG      160     /* < 160: 4T (long) */

/* MFM address marks */
#define MFM_SYNC_BYTE   0xA1
#define MFM_IDAM        0xFE
#define MFM_DAM         0xFB
#define MFM_CRC_INIT    0xFFFF
#define MFM_CRC_POLY    0x1021

/* --- LZ4 Compression --- */

/*
 * COMPRESS_BLOCK_SIZE: raakadatan lohkokoko tavuina.
 * Suurempi lohko = parempi pakkaus, mutta enemmän RAMia:
 *
 *   Lohko   raw_buf   frame_buf   hash (pino)   Yhteensä   Pakkaus
 *   ─────   ───────   ─────────   ───────────   ────────   ───────
 *    64B      64B       100B       128B (64×2)    292B      ~1.3:1
 *   128B     128B       172B       256B (128×2)   556B      ~1.5:1
 *   256B     256B       300B       512B (256×2)  1068B      ~1.8:1
 *
 * 23LC512:n 64KB SRAM:issa raakadata- ja kehyspuskurit ovat
 * ulkoisessa muistissa (SRAM_COMP_RAW / SRAM_COMP_FRAME).
 * Hash-taulu (COMPRESS_HASH_SIZE × 2 tavua) on pinossa
 * kompression aikana: 256 × 2 = 512 tavua.
 *
 * 512B lohkokoko = FAT12-sektorikoko → suoraviivainen pipeline.
 *
 * COMPRESS_HASH_SIZE pitää olla sama kuin COMPRESS_BLOCK_SIZE
 * (hash-taulun koko = lohkokoko entryjä × 2 tavua pinossa).
 *
 * COMPRESS_FRAME_BUF_SIZE = 4 (header) + BLOCK_SIZE + BLOCK_SIZE/255 + 16
 * (worst case: ei-pakattava data + LZ4 token overhead).
 */
#define COMPRESS_BLOCK_SIZE     512
#define COMPRESS_HASH_SIZE      256
#define COMPRESS_FRAME_BUF_SIZE 556

#endif /* CONFIG_H */
