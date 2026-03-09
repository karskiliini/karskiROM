#ifndef CBM_DOS_H
#define CBM_DOS_H

#include <stdint.h>
#include <stdbool.h>

/*
 * CBM-DOS emulation for Arduino Nano.
 *
 * Simplified version (no D64 support - image doesn't fit in 128KB SRAM).
 * Supports FAT12 operations only:
 *   LOAD "FILENAME",8   -> Open file for reading
 *   SAVE "FILENAME",8   -> Create/write file
 *   LOAD "$",8           -> Directory listing
 *   S:filename            -> Scratch (delete)
 *   R:new=old             -> Rename
 *   N:label               -> Format disk
 *   I                     -> Initialize (re-read disk)
 */

/* Error codes */
#define CBM_ERR_OK               0
#define CBM_ERR_FILES_SCRATCHED  1
#define CBM_ERR_READ_ERROR      20
#define CBM_ERR_WRITE_ERROR     25
#define CBM_ERR_WRITE_PROTECT   26
#define CBM_ERR_SYNTAX_ERROR    30
#define CBM_ERR_FILE_NOT_FOUND  62
#define CBM_ERR_FILE_EXISTS     63
#define CBM_ERR_DISK_FULL       72
#define CBM_ERR_DOS_MISMATCH    73
#define CBM_ERR_DRIVE_NOT_READY 74

#define CBM_DOS_ID  "64KORPPU V1.0"

void cbm_dos_init(void);
void cbm_dos_open(uint8_t sa, const char *filename, uint8_t len);
void cbm_dos_close(uint8_t sa);
bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi);
void cbm_dos_listen_byte(uint8_t sa, uint8_t byte);
void cbm_dos_execute_command(const char *cmd, uint8_t len);
int cbm_dos_format_error(uint8_t code, const char *msg,
                          uint8_t track, uint8_t sector,
                          char *buf, uint8_t buflen);

#endif /* CBM_DOS_H */
