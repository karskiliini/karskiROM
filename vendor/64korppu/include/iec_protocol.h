#ifndef IEC_PROTOCOL_H
#define IEC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/*
 * Commodore IEC serial bus protocol for Arduino Nano.
 *
 * Direct 5V GPIO (no level shifters needed):
 *   D2 = ATN  (INT0 interrupt)
 *   D3 = CLK
 *   D4 = DATA
 *   D5 = RESET
 *
 * Open-collector emulation:
 *   Assert: set as OUTPUT, drive LOW
 *   Release: set as INPUT (pull-up brings HIGH)
 */

typedef enum {
    IEC_STATE_IDLE,
    IEC_STATE_LISTENER,
    IEC_STATE_TALKER,
} iec_state_t;

typedef struct {
    bool     open;
    char     filename[42];
    uint8_t  filename_len;
    uint8_t  buffer[64];    /* Smaller buffer for Nano (saves RAM) */
    uint8_t  buf_len;
    uint8_t  buf_pos;
    bool     eof;
} iec_channel_t;

typedef struct {
    uint8_t       device_number;
    iec_state_t   state;
    uint8_t       current_sa;
    bool          atn_active;
    iec_channel_t channels[2];  /* Only SA 0 and SA 15 to save RAM */
    uint8_t       error_code;
    char          error_msg[48];
} iec_device_t;

void iec_init(uint8_t device_num);
void iec_service(void);
void iec_release_all(void);

bool iec_receive_byte_atn(uint8_t *byte);
bool iec_receive_byte(uint8_t *byte, bool *eoi);
bool iec_send_byte(uint8_t byte, bool eoi);
void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector);

#endif /* IEC_PROTOCOL_H */
