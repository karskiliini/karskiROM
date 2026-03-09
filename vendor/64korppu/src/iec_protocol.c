#include "iec_protocol.h"
#include "cbm_dos.h"
#include "config.h"
#include "fastload.h"
#include "fastload_jiffydos.h"

#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/*
 * IEC serial bus protocol for ATmega328P.
 *
 * Direct 5V GPIO — no level shifters needed.
 * Open-collector emulation:
 *   Assert: set as OUTPUT, drive LOW
 *   Release: set as INPUT (external/internal pull-up → HIGH)
 */

static iec_device_t device = {0};

/* Pin manipulation macros */
#define IEC_ASSERT(pin)   do { IEC_DDR |= (1 << (pin)); IEC_PORT &= ~(1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

/* Timing uses _delay_us busy loops in protocol functions */

void iec_init(uint8_t device_num) {
    device.device_number = device_num;
    device.state = IEC_STATE_IDLE;

    /* All IEC pins as inputs with pull-ups */
    IEC_DDR &= ~((1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                  (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET));
    IEC_PORT |= (1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET);

    memset(device.channels, 0, sizeof(device.channels));
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

void iec_release_all(void) {
    IEC_RELEASE(IEC_PIN_CLK);
    IEC_RELEASE(IEC_PIN_DATA);
}

bool iec_receive_byte_atn(uint8_t *byte) {
    *byte = 0;

    /* Signal ready by asserting DATA */
    IEC_ASSERT(IEC_PIN_DATA);
    _delay_us(IEC_TIMING_LISTENER_HOLD);

    /* Wait for controller to release CLK */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    /* Release DATA */
    IEC_RELEASE(IEC_PIN_DATA);

    /* Receive 8 bits, LSB first */
    for (int bit = 0; bit < 8; bit++) {
        /* Wait for CLK asserted */
        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }

        /* Read data bit */
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }

        /* Wait for CLK released */
        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
    }

    /* Acknowledge */
    IEC_ASSERT(IEC_PIN_DATA);
    _delay_us(IEC_TIMING_BETWEEN_BYTES);

    return true;
}

bool iec_receive_byte(uint8_t *byte, bool *eoi) {
    *byte = 0;
    *eoi = false;

    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for CLK asserted, with EOI detection */
    uint16_t wait = 0;
    while (!IEC_IS_LOW(IEC_PIN_CLK)) {
        _delay_us(1);
        if (++wait > IEC_TIMING_EOI_TIMEOUT) {
            *eoi = true;
            IEC_ASSERT(IEC_PIN_DATA);
            _delay_us(IEC_TIMING_EOI_ACK);
            IEC_RELEASE(IEC_PIN_DATA);

            uint16_t timeout = 0;
            while (!IEC_IS_LOW(IEC_PIN_CLK)) {
                _delay_us(1);
                if (++timeout > IEC_TIMEOUT_US) return false;
            }
            break;
        }
        if (IEC_IS_LOW(IEC_PIN_ATN)) return false;
    }

    /* Receive 8 bits */
    for (int bit = 0; bit < 8; bit++) {
        uint16_t timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }

        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            *byte |= (1 << bit);
        }

        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_CLK)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
    }

    IEC_ASSERT(IEC_PIN_DATA);
    return true;
}

bool iec_send_byte(uint8_t byte, bool eoi) {
    IEC_ASSERT(IEC_PIN_CLK);
    IEC_RELEASE(IEC_PIN_DATA);
    _delay_us(IEC_TIMING_TALKER_SETUP);

    /* Wait for listener ready */
    uint16_t timeout = 0;
    while (IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
        if (IEC_IS_LOW(IEC_PIN_ATN)) return false;
    }

    /* EOI signaling */
    if (eoi) {
        IEC_RELEASE(IEC_PIN_CLK);

        timeout = 0;
        while (!IEC_IS_LOW(IEC_PIN_DATA)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }
        timeout = 0;
        while (IEC_IS_LOW(IEC_PIN_DATA)) {
            _delay_us(1);
            if (++timeout > IEC_TIMEOUT_US) return false;
        }

        IEC_ASSERT(IEC_PIN_CLK);
        _delay_us(IEC_TIMING_TALKER_SETUP);
    }

    /* Send 8 bits */
    for (int bit = 0; bit < 8; bit++) {
        if (byte & (1 << bit)) {
            IEC_RELEASE(IEC_PIN_DATA);
        } else {
            IEC_ASSERT(IEC_PIN_DATA);
        }
        _delay_us(IEC_TIMING_CLK_LOW);

        IEC_RELEASE(IEC_PIN_CLK);
        _delay_us(IEC_TIMING_CLK_HIGH);

        IEC_ASSERT(IEC_PIN_CLK);
    }

    IEC_RELEASE(IEC_PIN_DATA);

    /* Wait for listener acknowledge */
    timeout = 0;
    while (!IEC_IS_LOW(IEC_PIN_DATA)) {
        _delay_us(1);
        if (++timeout > IEC_TIMEOUT_US) return false;
    }

    _delay_us(IEC_TIMING_BETWEEN_BYTES);
    return true;
}

void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector) {
    device.error_code = code;
    int len = cbm_dos_format_error(code, msg, track, sector,
                                    device.error_msg, sizeof(device.error_msg));

    /* Store in channel buffer for SA 15 */
    iec_channel_t *ch = &device.channels[1];  /* Index 1 = command channel */
    uint8_t copy_len = (len < (int)sizeof(ch->buffer)) ? (uint8_t)len : sizeof(ch->buffer);
    memcpy(ch->buffer, device.error_msg, copy_len);
    ch->buf_len = copy_len;
    ch->buf_pos = 0;
    ch->eof = false;
}

void iec_service(void) {
    /* Check for bus reset */
    if (IEC_IS_LOW(IEC_PIN_RESET)) {
        device.state = IEC_STATE_IDLE;
        iec_release_all();
        while (IEC_IS_LOW(IEC_PIN_RESET));
        _delay_ms(100);
        return;
    }

    /* Check for ATN */
    if (!IEC_IS_LOW(IEC_PIN_ATN)) {
        if (device.state == IEC_STATE_TALKER) {
            uint8_t byte;
            bool eoi;
            if (cbm_dos_talk_byte(device.current_sa, &byte, &eoi)) {
                const fastload_protocol_t *fl = fastload_active();
                bool ok = fl && fl->send_byte ? fl->send_byte(byte, eoi)
                                               : iec_send_byte(byte, eoi);
                if (!ok) {
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
                if (eoi) {
                    device.state = IEC_STATE_IDLE;
                    iec_release_all();
                }
            }
        }
        return;
    }

    /* ATN asserted - receive commands */
    IEC_ASSERT(IEC_PIN_DATA);

    uint8_t cmd;
    while (IEC_IS_LOW(IEC_PIN_ATN)) {
        if (!iec_receive_byte_atn(&cmd)) break;

        uint8_t device_num = cmd & 0x1F;

        if (cmd == IEC_CMD_UNLISTEN) {
            if (device.state == IEC_STATE_LISTENER) {
                iec_channel_t *ch = &device.channels[0];
                if (device.current_sa == IEC_SA_COMMAND && ch->buf_len > 0) {
                    ch->buffer[ch->buf_len] = '\0';
                    cbm_dos_execute_command((char *)ch->buffer, ch->buf_len);
                    ch->buf_len = 0;
                } else if (device.current_sa == IEC_SA_SAVE) {
                    cbm_dos_close(device.current_sa);
                }
                device.state = IEC_STATE_IDLE;
                iec_release_all();
                fastload_reset();
            }
        } else if (cmd == IEC_CMD_UNTALK) {
            if (device.state == IEC_STATE_TALKER) {
                device.state = IEC_STATE_IDLE;
                iec_release_all();
                fastload_reset();
            }
        } else if ((cmd & 0xE0) == IEC_CMD_LISTEN) {
            if (device_num == device.device_number) {
                device.state = IEC_STATE_LISTENER;
            }
        } else if ((cmd & 0xE0) == IEC_CMD_TALK) {
            if (device_num == device.device_number) {
                device.state = IEC_STATE_TALKER;
            }
        } else if ((cmd & 0xF0) == IEC_CMD_OPEN) {
            uint8_t sa = cmd & 0x0F;
            device.current_sa = sa;

            if (device.state == IEC_STATE_LISTENER) {
                iec_channel_t *ch = &device.channels[0];
                ch->buf_len = 0;
                ch->buf_pos = 0;
            } else if (device.state == IEC_STATE_TALKER) {
                IEC_RELEASE(IEC_PIN_DATA);
                _delay_us(IEC_TIMING_TALKER_SETUP);
                IEC_ASSERT(IEC_PIN_CLK);
            }
        } else if ((cmd & 0xF0) == (IEC_CMD_CLOSE & 0xF0)) {
            uint8_t sa = cmd & 0x0F;
            cbm_dos_close(sa);
        }
    }

    /* ATN released — detect fast-load protocol for talker mode */
    if (device.state == IEC_STATE_TALKER) {
        fastload_detect();
        const fastload_protocol_t *fl = fastload_active();
        if (fl && fl->on_atn_end) fl->on_atn_end();
    }

    if (device.state == IEC_STATE_LISTENER) {
        uint8_t byte;
        bool eoi;
        iec_channel_t *ch = &device.channels[0];

        while (1) {
            if (IEC_IS_LOW(IEC_PIN_ATN)) break;
            if (!iec_receive_byte(&byte, &eoi)) break;

            if (!ch->open && device.current_sa != IEC_SA_COMMAND) {
                if (ch->filename_len < sizeof(ch->filename) - 1) {
                    ch->filename[ch->filename_len++] = byte;
                }
                if (eoi) {
                    ch->filename[ch->filename_len] = '\0';
                    cbm_dos_open(device.current_sa, ch->filename, ch->filename_len);
                    ch->open = true;
                }
            } else {
                if (ch->buf_len < sizeof(ch->buffer)) {
                    ch->buffer[ch->buf_len++] = byte;
                }
                cbm_dos_listen_byte(device.current_sa, byte);
            }

            if (eoi) break;
        }
    }
}

#endif /* __AVR__ */
