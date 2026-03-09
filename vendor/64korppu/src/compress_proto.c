/*
 * XZ compression command channel protocol implementation.
 *
 * Commands:
 *   XZ:1  — enable compression
 *   XZ:0  — disable compression
 *   XZ:S  — query status (read back with get_status)
 *
 * Block frame format:
 *   [comp_size LE 16-bit] [raw_size LE 16-bit] [LZ4 compressed payload]
 *
 * EOF marker:
 *   [0x00 0x00]
 */

#include "compress_proto.h"
#include "lz4_compress.h"
#include <string.h>

static bool compression_enabled = false;
static bool status_pending = false;

void compress_proto_init(void)
{
    compression_enabled = false;
    status_pending = false;
}

bool compress_proto_handle_command(const char *cmd, uint8_t len)
{
    if (len < 4)
        return false;

    /* Check for "XZ:" prefix (case-insensitive) */
    if (!((cmd[0] == 'X' || cmd[0] == 'x') &&
          (cmd[1] == 'Z' || cmd[1] == 'z') &&
          cmd[2] == ':'))
        return false;

    char sub = cmd[3];
    /* Uppercase it */
    if (sub >= 'a' && sub <= 'z')
        sub = sub - 'a' + 'A';

    switch (sub) {
    case '1':
        compression_enabled = true;
        return true;
    case '0':
        compression_enabled = false;
        return true;
    case 'S':
        status_pending = true;
        return true;
    default:
        /* Unknown subcommand — not ours */
        return false;
    }
}

bool compress_proto_enabled(void)
{
    return compression_enabled;
}

int compress_proto_get_status(char *buf, int buf_size)
{
    if (!status_pending)
        return -1;

    const char *msg = compression_enabled ? "XZ:1" : "XZ:0";
    int needed = 4; /* "XZ:0" or "XZ:1" */

    if (buf_size < needed + 1)
        return -1;

    memcpy(buf, msg, needed);
    buf[needed] = '\0';
    status_pending = false;
    return needed;
}

int compress_proto_frame_block(const uint8_t *raw, int raw_len,
                               uint8_t *out, int out_cap)
{
    if (out_cap < 4)
        return -1;

    /* Compress into the space after the 4-byte header */
    int comp_size = lz4_compress_block(raw, raw_len, out + 4, out_cap - 4);
    if (comp_size < 0)
        return -1;

    /* Write header: comp_size LE, raw_size LE */
    out[0] = (uint8_t)(comp_size & 0xFF);
    out[1] = (uint8_t)((comp_size >> 8) & 0xFF);
    out[2] = (uint8_t)(raw_len & 0xFF);
    out[3] = (uint8_t)((raw_len >> 8) & 0xFF);

    return 4 + comp_size;
}

int compress_proto_frame_eof(uint8_t *out, int out_cap)
{
    if (out_cap < 2)
        return -1;

    out[0] = 0x00;
    out[1] = 0x00;
    return 2;
}
