#ifndef FASTLOAD_BURST_H
#define FASTLOAD_BURST_H

#include "fastload.h"

/*
 * Burst mode fast serial protocol (1571-style).
 *
 * Clock-synchronized 8-bit serial transfer using CLK line for
 * clocking and DATA line for data. Requires C128 with CIA CNT
 * connected to IEC CLK — does NOT work on C64.
 *
 * Implemented as future-proofing for C128 compatibility.
 *
 * Detection: C128 sends "U0" command on command channel (SA 15).
 *
 * Byte transfer:
 *   Talker sets DATA, pulses CLK — 1 pulse per bit, 8 pulses per byte.
 *   ~8us per bit → ~65us per byte → ~15 KB/s.
 */

/* Timing */
#define BURST_CLK_PULSE_US      8
#define BURST_SETUP_US          4
#define BURST_BYTE_DELAY_US    20

/* U0 command prefix for detection */
#define BURST_CMD_PREFIX       'U'
#define BURST_CMD_SUBCMD       '0'

/* Register Burst protocol into fastload registry */
void fastload_burst_register(void);

/* Check if command buffer contains Burst command (callable from cbm_dos) */
bool fastload_burst_check_command(const char *cmd, uint8_t len);

#endif /* FASTLOAD_BURST_H */
