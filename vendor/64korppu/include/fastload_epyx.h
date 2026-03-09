#ifndef FASTLOAD_EPYX_H
#define FASTLOAD_EPYX_H

#include "fastload.h"

/*
 * EPYX FastLoad protocol.
 *
 * EPYX FastLoad cartridge normally uploads "drive code" to the
 * 1541's RAM via M-W (Memory-Write) commands. Since this device
 * is not a 1541, we detect the M-W sequence and switch to the
 * EPYX fast byte transfer protocol directly.
 *
 * Supports LOAD only (not SAVE, drive monitor, or disk copy).
 *
 * Detection: EPYX sends M-W commands (0x4D, 0x2D, 0x57) on
 * the command channel. We detect this prefix and flag EPYX mode.
 *
 * Byte transfer (2-bit parallel, like JiffyDOS but different timing):
 *   1. C64 drops CLK → device drops DATA (sync)
 *   2. 4 rounds: CLK=bit(2n), DATA=bit(2n+1), ~14µs/round
 *   3. Device raises DATA, C64 raises CLK (ACK)
 */

/* Timing */
#define EPYX_BIT_PAIR_US        14
#define EPYX_HANDSHAKE_US       20
#define EPYX_BYTE_DELAY_US      25

/* M-W command bytes: "M-W" = 0x4D 0x2D 0x57 */
#define EPYX_MW_M              0x4D
#define EPYX_MW_DASH           0x2D
#define EPYX_MW_W              0x57

/* Register EPYX protocol into fastload registry */
void fastload_epyx_register(void);

/* Check if command buffer contains EPYX drive code upload */
bool fastload_epyx_check_command(const uint8_t *buf, uint8_t len);

/* Reset EPYX detection state */
void fastload_epyx_reset(void);

#endif /* FASTLOAD_EPYX_H */
