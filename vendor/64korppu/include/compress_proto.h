#ifndef COMPRESS_PROTO_H
#define COMPRESS_PROTO_H

#include <stdint.h>
#include <stdbool.h>

/*
 * XZ compression command channel protocol.
 *
 * Handles "XZ:" commands to enable/disable LZ4 block compression
 * on IEC data transfers, and frames compressed blocks with a
 * 4-byte header (comp_size LE + raw_size LE).
 */

/* Reset protocol state (compression disabled). */
void compress_proto_init(void);

/*
 * Try to handle a command as an XZ command.
 * Returns true if cmd was a recognized XZ command, false otherwise.
 * cmd: null-terminated command string, len: string length.
 */
bool compress_proto_handle_command(const char *cmd, uint8_t len);

/* Returns true if compression is currently enabled. */
bool compress_proto_enabled(void);

/*
 * Write current status string to buf ("XZ:0" or "XZ:1").
 * Returns number of bytes written (excluding NUL), or -1 if buf too small.
 */
int compress_proto_get_status(char *buf, int buf_size);

/*
 * Compress raw data and frame it with a 4-byte header:
 *   [comp_size LE 16-bit] [raw_size LE 16-bit] [compressed payload]
 *
 * Returns total frame size (header + payload), or -1 on error.
 */
int compress_proto_frame_block(const uint8_t *raw, int raw_len,
                               uint8_t *out, int out_cap);

/*
 * Write a 2-byte EOF marker (0x00 0x00) to out.
 * Returns 2 on success, -1 if out_cap < 2.
 */
int compress_proto_frame_eof(uint8_t *out, int out_cap);

#endif /* COMPRESS_PROTO_H */
