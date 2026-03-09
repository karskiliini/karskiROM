#include "cbm_dos.h"
#include "iec_protocol.h"
#include "fat12.h"
#include "config.h"
#include "sram.h"
#include "fastload_burst.h"
#include "fastload_epyx.h"
#include "compress_proto.h"
#include <string.h>
#include <stdio.h>

#include <ctype.h>

/*
 * CBM-DOS emulation for Arduino Nano.
 *
 * Simplified: FAT12 only, no D64 support (image doesn't fit 128KB SRAM).
 * Directory listing stored in SRAM_DIR_BUF, sent byte-by-byte.
 */

/* File handle for active channel */
static fat12_file_t channel_file;

/* Directory listing state */
static uint16_t dir_len = 0;
static uint16_t dir_pos = 0;
static bool dir_active = false;

/*
 * Compressed transfer state.
 * Raw and frame buffers are in external SRAM (SRAM_COMP_RAW / SRAM_COMP_FRAME),
 * not in internal RAM. Only the state variables live here (~8 bytes).
 * During compression, lz4_compress_block allocates the hash table on the stack
 * (COMPRESS_HASH_SIZE × 2 = 512 bytes, temporarily).
 */
static uint16_t comp_raw_pos;
static uint16_t comp_frame_len;
static uint16_t comp_frame_pos;
static bool     comp_filling;
static bool     comp_eof_sent;

void cbm_dos_init(void) {
    memset(&channel_file, 0, sizeof(channel_file));
    dir_active = false;
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

int cbm_dos_format_error(uint8_t code, const char *msg,
                          uint8_t track, uint8_t sector,
                          char *buf, uint8_t buflen) {
    return snprintf(buf, buflen, "%02d, %s,%02d,%02d\r",
                     code, msg, track, sector);
}

/*
 * Generate BASIC-format directory listing into SRAM_DIR_BUF.
 */
static void generate_directory(void) {
    uint8_t line[48];  /* Small line buffer in internal RAM */
    uint16_t pos = 0;
    uint16_t max_pos = SRAM_DIR_BUF_SIZE - 8;

    /* Load address: $0401 */
    line[0] = 0x01;
    line[1] = 0x04;
    sram_write(SRAM_DIR_BUF + pos, line, 2);
    pos += 2;

    /* Header line */
    uint8_t line_pos = 0;
    line[line_pos++] = 0x00;  /* Next line pointer (placeholder) */
    line[line_pos++] = 0x00;
    line[line_pos++] = 0x00;  /* Line number = 0 */
    line[line_pos++] = 0x00;
    line[line_pos++] = 0x12;  /* RVS ON */
    line[line_pos++] = '"';

    const char *label = "64KORPPU        ";
    for (int i = 0; i < 16; i++) line[line_pos++] = label[i];

    line[line_pos++] = '"';
    line[line_pos++] = ' ';

    const char *id = "64 FAT";
    for (const char *p = id; *p; p++) line[line_pos++] = *p;

    line[line_pos++] = 0x00;  /* End of line */

    /* Fix next line pointer */
    uint16_t next_addr = pos + line_pos + 0x0401;
    line[0] = next_addr & 0xFF;
    line[1] = (next_addr >> 8) & 0xFF;

    sram_write(SRAM_DIR_BUF + pos, line, line_pos);
    pos += line_pos;

    /* File entries */
    uint16_t dir_index = 0;
    fat12_dirent_t entry;

    while (fat12_readdir(&dir_index, &entry) == FAT12_OK && pos < max_pos) {
        if (entry.attr & FAT12_ATTR_VOLUME_ID) continue;

        uint16_t blocks = (entry.file_size + 255) / 256;
        if (blocks == 0) blocks = 1;

        line_pos = 0;
        line[line_pos++] = 0x00;
        line[line_pos++] = 0x00;
        line[line_pos++] = blocks & 0xFF;
        line[line_pos++] = (blocks >> 8) & 0xFF;

        /* Alignment spaces */
        if (blocks < 10) line[line_pos++] = ' ';
        if (blocks < 100) line[line_pos++] = ' ';
        if (blocks < 1000) line[line_pos++] = ' ';

        line[line_pos++] = '"';

        /* Filename (trim trailing spaces) */
        int name_len = 8;
        while (name_len > 0 && entry.name[name_len - 1] == ' ') name_len--;
        for (int i = 0; i < name_len; i++) line[line_pos++] = entry.name[i];

        /* Extension */
        int ext_len = 3;
        while (ext_len > 0 && entry.ext[ext_len - 1] == ' ') ext_len--;
        if (ext_len > 0) {
            line[line_pos++] = '.';
            for (int i = 0; i < ext_len; i++) line[line_pos++] = entry.ext[i];
            name_len += ext_len + 1;
        }

        line[line_pos++] = '"';

        /* Pad for type */
        for (int i = name_len; i < 16; i++) line[line_pos++] = ' ';

        /* File type */
        const char *type_str;
        if (entry.attr & FAT12_ATTR_DIRECTORY) {
            type_str = "DIR";
        } else if (ext_len >= 3 && memcmp(entry.ext, "SEQ", 3) == 0) {
            type_str = "SEQ";
        } else {
            type_str = "PRG";
        }
        for (const char *p = type_str; *p; p++) line[line_pos++] = *p;

        line[line_pos++] = 0x00;

        next_addr = pos + line_pos + 0x0401;
        line[0] = next_addr & 0xFF;
        line[1] = (next_addr >> 8) & 0xFF;

        sram_write(SRAM_DIR_BUF + pos, line, line_pos);
        pos += line_pos;
    }

    /* "BLOCKS FREE." line */
    uint32_t free_bytes = fat12_free_space();
    uint16_t free_blocks = free_bytes / 256;

    line_pos = 0;
    line[line_pos++] = 0x00;
    line[line_pos++] = 0x00;
    line[line_pos++] = free_blocks & 0xFF;
    line[line_pos++] = (free_blocks >> 8) & 0xFF;

    const char *free_msg = "BLOCKS FREE.";
    for (const char *p = free_msg; *p; p++) line[line_pos++] = *p;
    line[line_pos++] = 0x00;

    /* End of program */
    line[line_pos++] = 0x00;
    line[line_pos++] = 0x00;

    sram_write(SRAM_DIR_BUF + pos, line, line_pos);
    pos += line_pos;

    dir_len = pos;
    dir_pos = 0;
    dir_active = true;
}

void cbm_dos_open(uint8_t sa, const char *filename, uint8_t len) {
    char name8[9] = {0};
    char ext3[4] = {0};

    if (sa == IEC_SA_LOAD || sa == IEC_SA_SAVE) {
        /* Directory listing? */
        if (sa == IEC_SA_LOAD && len == 1 && filename[0] == '$') {
            generate_directory();
            /* Reset compression buffers for new transfer */
            comp_raw_pos = 0;
            comp_frame_len = 0;
            comp_frame_pos = 0;
            comp_filling = true;
            comp_eof_sent = false;
            iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            return;
        }

        fat12_parse_filename(filename, name8, ext3);

        if (sa == IEC_SA_LOAD) {
            int rc = fat12_open_read(name8, ext3, &channel_file);
            if (rc != FAT12_OK) {
                iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                return;
            }
        } else {
            int rc = fat12_create(name8, ext3, &channel_file);
            if (rc != FAT12_OK) {
                if (rc == FAT12_ERR_DISK_FULL) {
                    iec_set_error(CBM_ERR_DISK_FULL, "DISK FULL", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
                return;
            }
        }
        /* Reset compression buffers for new transfer */
        comp_raw_pos = 0;
        comp_frame_len = 0;
        comp_frame_pos = 0;
        comp_filling = true;
        comp_eof_sent = false;
        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
    }
}

void cbm_dos_close(uint8_t sa) {
    (void)sa;
    if (channel_file.active) {
        fat12_close(&channel_file);
    }
    dir_active = false;
}

/*
 * Read one raw byte from the underlying source (directory or file).
 * Returns true if a byte was read, false if source exhausted.
 */
static bool comp_read_raw_byte(uint8_t *out) {
    if (dir_active) {
        if (dir_pos >= dir_len) return false;
        sram_read(SRAM_DIR_BUF + dir_pos, out, 1);
        dir_pos++;
        return true;
    }
    if (channel_file.active) {
        int rc = fat12_read(&channel_file, out, 1);
        return (rc == 1);
    }
    return false;
}

/*
 * Compress raw data from SRAM_COMP_RAW and write framed output to SRAM_COMP_FRAME.
 * Uses stack-allocated temporary buffers (~1068 bytes) only during this call.
 * Returns frame length, or -1 on failure.
 */
static int compress_block_via_sram(uint16_t raw_len) {
    uint8_t raw[COMPRESS_BLOCK_SIZE];
    uint8_t frame[COMPRESS_FRAME_BUF_SIZE];

    sram_read(SRAM_COMP_RAW, raw, raw_len);
    int flen = compress_proto_frame_block(raw, raw_len, frame, sizeof(frame));
    if (flen > 0) {
        sram_write(SRAM_COMP_FRAME, frame, (uint16_t)flen);
    }
    return flen;
}

/*
 * Compressed talk: accumulate raw bytes into SRAM_COMP_RAW (512-byte blocks),
 * compress + frame each block to SRAM_COMP_FRAME, then drain byte-by-byte.
 * After the last data frame, send a 2-byte EOF marker (0x0000).
 *
 * Buffers are in external SRAM — only state variables use internal RAM.
 * During the compress call, ~1068 bytes of stack are used temporarily.
 */
static bool cbm_dos_talk_byte_compressed(uint8_t sa, uint8_t *byte, bool *eoi) {
    (void)sa;
    *eoi = false;

    /* Draining a framed block from SRAM? */
    if (comp_frame_pos < comp_frame_len) {
        *byte = sram_read_byte(SRAM_COMP_FRAME + comp_frame_pos);
        comp_frame_pos++;
        if (comp_frame_pos >= comp_frame_len && comp_eof_sent) {
            *eoi = true;
        }
        return true;
    }

    /* If we already sent the EOF marker fully, we're done */
    if (comp_eof_sent) {
        *eoi = true;
        return false;
    }

    /* Source was exhausted on previous round — now send EOF marker */
    if (!comp_filling) {
        uint8_t eof[2] = {0x00, 0x00};
        sram_write(SRAM_COMP_FRAME, eof, 2);
        comp_frame_len = 2;
        comp_frame_pos = 0;
        comp_eof_sent = true;

        *byte = sram_read_byte(SRAM_COMP_FRAME);
        comp_frame_pos = 1;
        if (comp_frame_pos >= comp_frame_len) {
            *eoi = true;
        }
        return true;
    }

    /* Fill raw buffer in SRAM up to COMPRESS_BLOCK_SIZE bytes */
    bool source_done = false;
    while (comp_raw_pos < COMPRESS_BLOCK_SIZE) {
        uint8_t b;
        if (!comp_read_raw_byte(&b)) {
            source_done = true;
            break;
        }
        sram_write_byte(SRAM_COMP_RAW + comp_raw_pos, b);
        comp_raw_pos++;
    }

    if (comp_raw_pos > 0) {
        /* Compress from SRAM_COMP_RAW → SRAM_COMP_FRAME */
        int flen = compress_block_via_sram(comp_raw_pos);
        if (flen > 0) {
            comp_frame_len = (uint16_t)flen;
        } else {
            /* Compression failed — abort transfer */
            *eoi = true;
            return false;
        }
        comp_frame_pos = 0;
        comp_raw_pos = 0;

        if (source_done) {
            comp_filling = false;
        }

        *byte = sram_read_byte(SRAM_COMP_FRAME);
        comp_frame_pos = 1;
        return true;
    }

    /* No raw data at all (empty source) — send EOF marker directly */
    uint8_t eof[2] = {0x00, 0x00};
    sram_write(SRAM_COMP_FRAME, eof, 2);
    comp_frame_len = 2;
    comp_frame_pos = 0;
    comp_filling = false;
    comp_eof_sent = true;

    *byte = sram_read_byte(SRAM_COMP_FRAME);
    comp_frame_pos = 1;
    if (comp_frame_pos >= comp_frame_len) {
        *eoi = true;
    }
    return true;
}

bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi) {
    *eoi = false;

    if (compress_proto_enabled() && sa != IEC_SA_COMMAND) {
        return cbm_dos_talk_byte_compressed(sa, byte, eoi);
    }

    if (sa == IEC_SA_LOAD) {
        /* Directory listing */
        if (dir_active) {
            /* Read byte from SRAM dir buffer */
            sram_read(SRAM_DIR_BUF + dir_pos, byte, 1);
            dir_pos++;
            if (dir_pos >= dir_len) {
                *eoi = true;
                dir_active = false;
            }
            return true;
        }

        /* File read */
        if (channel_file.active) {
            uint8_t buf;
            int rc = fat12_read(&channel_file, &buf, 1);
            if (rc == 1) {
                *byte = buf;
                if (channel_file.position >= channel_file.file_size) {
                    *eoi = true;
                }
                return true;
            }
            *eoi = true;
            return false;
        }
    }

    *eoi = true;
    return false;
}

void cbm_dos_listen_byte(uint8_t sa, uint8_t byte) {
    if (sa == IEC_SA_SAVE && channel_file.active) {
        fat12_write(&channel_file, &byte, 1);
    }
}

void cbm_dos_execute_command(const char *cmd, uint8_t len) {
    if (len == 0) return;

    char name8[9], ext3[4];
    char cmd_buf[42];

    uint8_t cmd_len = len < sizeof(cmd_buf) - 1 ? len : sizeof(cmd_buf) - 1;
    memcpy(cmd_buf, cmd, cmd_len);
    cmd_buf[cmd_len] = '\0';

    for (int i = 0; i < cmd_len; i++) {
        cmd_buf[i] = toupper((unsigned char)cmd_buf[i]);
    }

    /* Check for fast-load protocol commands */
    if (fastload_burst_check_command(cmd_buf, cmd_len)) return;
    if (fastload_epyx_check_command((const uint8_t *)cmd_buf, cmd_len)) return;

    /* Check for compression protocol commands */
    if (compress_proto_handle_command(cmd_buf, cmd_len)) {
        char status_buf[8];
        int slen = compress_proto_get_status(status_buf, sizeof(status_buf));
        if (slen > 0) {
            /* XZ:S was queried — respond with status */
            iec_set_error(0, status_buf, 0, 0);
        } else if (compress_proto_enabled()) {
            iec_set_error(0, "OK COMPRESS ON", 0, 0);
        } else {
            iec_set_error(0, "OK", 0, 0);
        }
        return;
    }

    switch (cmd_buf[0]) {
        case 'S': {
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                fat12_parse_filename(&cmd_buf[2], name8, ext3);
                int rc = fat12_delete(name8, ext3);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_FILES_SCRATCHED, "FILES SCRATCHED", 1, 0);
                } else {
                    iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                }
            } else {
                iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            }
            break;
        }

        case 'R': {
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                char *eq = strchr(&cmd_buf[2], '=');
                if (eq) {
                    *eq = '\0';
                    char new_name8[9], new_ext3[4];
                    char old_name8[9], old_ext3[4];
                    fat12_parse_filename(&cmd_buf[2], new_name8, new_ext3);
                    fat12_parse_filename(eq + 1, old_name8, old_ext3);

                    int rc = fat12_rename(old_name8, old_ext3, new_name8, new_ext3);
                    if (rc == FAT12_OK) {
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    } else if (rc == FAT12_ERR_NOT_FOUND) {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_EXISTS, "FILE EXISTS", 0, 0);
                    }
                } else {
                    iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
                }
            }
            break;
        }

        case 'N': {
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                char label[12];
                memset(label, ' ', 11);
                label[11] = '\0';
                int llen = strlen(&cmd_buf[2]);
                if (llen > 11) llen = 11;
                memcpy(label, &cmd_buf[2], llen);

                int rc = fat12_format(label);
                iec_set_error(rc == FAT12_OK ? CBM_ERR_OK : CBM_ERR_WRITE_ERROR,
                              rc == FAT12_OK ? "OK" : "WRITE ERROR", 0, 0);
            } else {
                int rc = fat12_format(NULL);
                iec_set_error(rc == FAT12_OK ? CBM_ERR_OK : CBM_ERR_WRITE_ERROR,
                              rc == FAT12_OK ? "OK" : "WRITE ERROR", 0, 0);
            }
            break;
        }

        case 'I': {
            fat12_unmount();
            int rc = fat12_mount();
            iec_set_error(rc == FAT12_OK ? CBM_ERR_OK : CBM_ERR_DRIVE_NOT_READY,
                          rc == FAT12_OK ? "OK" : "DRIVE NOT READY", 0, 0);
            break;
        }

        default:
            iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            break;
    }
}
