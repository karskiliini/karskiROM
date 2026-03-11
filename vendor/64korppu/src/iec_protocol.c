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
        } else if ((cmd & 0xF0) == IEC_CMD_DATA) {
            /* SECOND/DATA ($60-$6F): select data channel */
            uint8_t sa = cmd & 0x0F;
            device.current_sa = sa;

            if (device.state == IEC_STATE_TALKER) {
                IEC_RELEASE(IEC_PIN_DATA);
                _delay_us(IEC_TIMING_TALKER_SETUP);
                IEC_ASSERT(IEC_PIN_CLK);
            }
        } else if ((cmd & 0xF0) == IEC_CMD_OPEN) {
            /* OPEN ($F0-$FF): open named file on channel */
            uint8_t sa = cmd & 0x0F;
            device.current_sa = sa;

            if (device.state == IEC_STATE_LISTENER) {
                iec_channel_t *ch = &device.channels[0];
                ch->buf_len = 0;
                ch->buf_pos = 0;
                ch->filename_len = 0;
                ch->open = false;
            }
        } else if ((cmd & 0xF0) == IEC_CMD_CLOSE) {
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

#else /* HOST_TEST / non-AVR build */

#include <string.h>
#include <stdio.h>

/*
 * Host-compatible IEC protocol implementation — NON-BLOCKING state machine.
 *
 * Unlike the AVR version which uses blocking busy-wait loops, this version
 * is designed for cooperative scheduling with the C64 emulator. Each call
 * to iec_service() performs ONE protocol step and returns, allowing the
 * C64 emulator to continue executing and drive the bus.
 *
 * The state machine tracks where we are in the IEC protocol:
 *   - Receiving command bytes under ATN (bit-by-bit)
 *   - Sending data bytes as talker (bit-by-bit)
 *   - Receiving data bytes as listener (bit-by-bit)
 *
 * GPIO register macros map to mock_hardware.h globals (mock_DDRD,
 * mock_PORTD, mock_PIND) via config.h -> mock_hardware.h.
 */

static iec_device_t device = {0};

/* Pin manipulation macros — same logic, operates on mock registers */
#define IEC_ASSERT(pin)   do { IEC_DDR |= (1 << (pin)); IEC_PORT &= ~(1 << (pin)); } while(0)
#define IEC_RELEASE(pin)  do { IEC_DDR &= ~(1 << (pin)); IEC_PORT |= (1 << (pin)); } while(0)
#define IEC_IS_LOW(pin)   (!(IEC_PINR & (1 << (pin))))

/* ---- State machine for non-blocking IEC protocol ---- */

typedef enum {
    SM_IDLE,                    /* No activity, polling for ATN */
    /* ATN byte reception states */
    SM_ATN_DATA_ASSERTED,       /* DATA asserted as ATN ack, waiting for CLK to go LOW */
    SM_ATN_WAIT_CLK_RELEASE,    /* CLK went LOW, now waiting for CLK to go HIGH (release) */
    SM_ATN_CLK_RELEASED_CONFIRM,/* CLK went HIGH, confirming it stays HIGH before releasing DATA */
    SM_ATN_DATA_RELEASED,       /* DATA released, waiting for CLK low (bit start) */
    SM_ATN_WAIT_CLK_HIGH,       /* Bit sampled on CLK low, waiting for CLK high */
    SM_ATN_BYTE_DONE,           /* All 8 bits received, DATA asserted as byte ack */
    /* Talker states (sending data bytes) */
    SM_TALK_SETUP,              /* CLK asserted, get byte; hold CLK for turnaround */
    SM_TALK_CLK_HOLD,           /* Hold CLK asserted briefly, then release */
    SM_TALK_WAIT_LISTENER,      /* CLK released, waiting for listener to release DATA */
    SM_TALK_EOI_WAIT_DATA_LOW,  /* EOI: CLK released, waiting for listener DATA low */
    SM_TALK_EOI_WAIT_DATA_HIGH, /* EOI: waiting for listener DATA release */
    SM_TALK_SEND_BIT,           /* Set DATA for current bit (CLK stays asserted) */
    SM_TALK_BIT_SETUP_HOLD,     /* Hold CLK asserted with DATA set for debounce */
    SM_TALK_CLK_HIGH,           /* CLK released (bit valid), hold for debounce */
    SM_TALK_WAIT_ACK,           /* All bits sent, DATA released, waiting for listener ack */
    /* Listener states (receiving data bytes without ATN) */
    SM_LISTEN_SYNC,             /* Wait for CLK LOW first (clear leftover HIGH) */
    SM_LISTEN_SYNC2,            /* Then wait for CLK HIGH (C64 "ready to send") → release DATA */
    SM_LISTEN_READY,            /* DATA released, waiting for CLK low (first bit / EOI detect) */
    SM_LISTEN_EOI_ACK,          /* EOI detected, DATA asserted briefly */
    SM_LISTEN_EOI_RELEASED,     /* EOI ack done, DATA released, waiting for CLK low */
    SM_LISTEN_WAIT_CLK_LOW,     /* Mid-byte: waiting for CLK low (next bit), no EOI detect */
    SM_LISTEN_WAIT_CLK_HIGH,    /* Bit sampled, waiting for CLK release */
    SM_LISTEN_BYTE_DONE,        /* All bits received, DATA asserted as ack */
} iec_sm_state_t;

static iec_sm_state_t sm_state = SM_IDLE;
static uint8_t sm_byte;         /* Byte being received/sent */
static uint8_t sm_bit;          /* Current bit position (0-7) */
static bool sm_eoi;             /* EOI flag for current transfer */
static bool sm_prev_atn;        /* Track ATN transitions */
static uint16_t sm_wait_count;  /* EOI detection counter */

int iec_debug_enabled = 0;
static int iec_debug_count = 0;
#define IEC_DBG(fmt, ...) do { if (iec_debug_enabled && iec_debug_count < 1000) { iec_debug_count++; fprintf(stderr, fmt, ##__VA_ARGS__); } } while(0)

/* Process a received ATN command byte */
static void iec_process_atn_command(uint8_t cmd) {
    uint8_t device_num = cmd & 0x1F;

    if (cmd == IEC_CMD_UNLISTEN) {
        if (device.state == IEC_STATE_LISTENER) {
            iec_channel_t *ch = &device.channels[0];
            /* If filename accumulated but channel not opened (EOI may have
             * been missed or not sent), open now before going idle. */
            if (!ch->open && ch->filename_len > 0 &&
                device.current_sa != IEC_SA_COMMAND) {
                ch->filename[ch->filename_len] = '\0';
                IEC_DBG("IEC: UNLISTEN opening file '%s' SA=%d\n", ch->filename, device.current_sa);
                cbm_dos_open(device.current_sa, ch->filename, ch->filename_len);
                ch->open = true;
            }
            if (device.current_sa == IEC_SA_COMMAND && ch->buf_len > 0) {
                ch->buffer[ch->buf_len] = '\0';
                cbm_dos_execute_command((char *)ch->buffer, ch->buf_len);
                ch->buf_len = 0;
            } else if (device.current_sa == IEC_SA_SAVE) {
                cbm_dos_close(device.current_sa);
            }
            IEC_DBG("IEC: UNLISTEN -> IDLE\n");
            device.state = IEC_STATE_IDLE;
            /* Don't release lines here — DATA must stay asserted as the
             * byte ACK so the C64 can see it.  Lines will be released
             * when ATN is released (ATN-release edge handler). */
            fastload_reset();
        }
    } else if (cmd == IEC_CMD_UNTALK) {
        if (device.state == IEC_STATE_TALKER) {
            device.state = IEC_STATE_IDLE;
            /* Same as UNLISTEN: defer line release to ATN-release edge. */
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
    } else if ((cmd & 0xF0) == IEC_CMD_DATA) {
        /* SECOND/DATA ($60-$6F): select data channel */
        uint8_t sa = cmd & 0x0F;
        device.current_sa = sa;
        /* Don't change bus lines here for talker mode — the ATN release
         * edge handler already does the turnaround (release DATA, assert CLK).
         * Changing lines here would undo the byte ACK before the C64 sees it. */
    } else if ((cmd & 0xF0) == IEC_CMD_OPEN) {
        /* OPEN ($F0-$FF): open named file on channel */
        uint8_t sa = cmd & 0x0F;
        device.current_sa = sa;

        if (device.state == IEC_STATE_LISTENER) {
            iec_channel_t *ch = &device.channels[0];
            ch->buf_len = 0;
            ch->buf_pos = 0;
            ch->filename_len = 0;
            ch->open = false;
        }
    } else if ((cmd & 0xF0) == IEC_CMD_CLOSE) {
        uint8_t sa = cmd & 0x0F;
        cbm_dos_close(sa);
    }
}

/* Process a received listener data byte */
static void iec_process_listen_byte(uint8_t byte, bool eoi) {
    iec_channel_t *ch = &device.channels[0];

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
}

void iec_init(uint8_t device_num) {
    device.device_number = device_num;
    device.state = IEC_STATE_IDLE;

    IEC_DDR &= ~((1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                  (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET));
    IEC_PORT |= (1 << IEC_PIN_ATN) | (1 << IEC_PIN_CLK) |
                (1 << IEC_PIN_DATA) | (1 << IEC_PIN_RESET);

    memset(device.channels, 0, sizeof(device.channels));
    sm_state = SM_IDLE;
    sm_prev_atn = false;
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

void iec_release_all(void) {
    IEC_RELEASE(IEC_PIN_CLK);
    IEC_RELEASE(IEC_PIN_DATA);
}

/* These blocking functions are kept for API compatibility but should
 * not be called in normal HOST_TEST operation. The state machine in
 * iec_service() handles all protocol. */
bool iec_receive_byte_atn(uint8_t *byte) {
    (void)byte;
    return false;
}

bool iec_receive_byte(uint8_t *byte, bool *eoi) {
    (void)byte; (void)eoi;
    return false;
}

bool iec_send_byte(uint8_t byte, bool eoi) {
    (void)byte; (void)eoi;
    return false;
}

void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector) {
    device.error_code = code;
    int len = cbm_dos_format_error(code, msg, track, sector,
                                    device.error_msg, sizeof(device.error_msg));

    iec_channel_t *ch = &device.channels[1];
    uint8_t copy_len = (len < (int)sizeof(ch->buffer)) ? (uint8_t)len : sizeof(ch->buffer);
    memcpy(ch->buffer, device.error_msg, copy_len);
    ch->buf_len = copy_len;
    ch->buf_pos = 0;
    ch->eof = false;
}

void iec_service(void) {
    bool atn_now = IEC_IS_LOW(IEC_PIN_ATN);

    /* Check for bus reset */
    if (IEC_IS_LOW(IEC_PIN_RESET)) {
        device.state = IEC_STATE_IDLE;
        sm_state = SM_IDLE;
        iec_release_all();
        return;
    }

    /* Detect ATN assertion edge: if ATN just went low, enter ATN handling
     * regardless of current state machine state. This mirrors how the real
     * firmware immediately responds to ATN. */
    if (atn_now && !sm_prev_atn) {
        /* ATN just asserted -- acknowledge by pulling DATA low */
        IEC_DBG("IEC: ATN asserted, DATA ack, sm=%d\n", sm_state);
        IEC_ASSERT(IEC_PIN_DATA);
        sm_state = SM_ATN_DATA_ASSERTED;
        sm_byte = 0;
        sm_bit = 0;
        sm_wait_count = 0;
        sm_prev_atn = true;
        return;
    }

    /* Detect ATN release edge */
    if (!atn_now && sm_prev_atn) {
        sm_prev_atn = false;

        IEC_DBG("IEC: ATN released, device.state=%d\n", device.state);

        /* ATN released -- transition based on device state */
        if (device.state == IEC_STATE_TALKER) {
            /* Enter talker mode: prepare to send data */
            IEC_DBG("IEC: -> TALK_SETUP\n");
            IEC_RELEASE(IEC_PIN_DATA);
            IEC_ASSERT(IEC_PIN_CLK);
            sm_state = SM_TALK_SETUP;
            sm_eoi = false;
            return;
        }
        if (device.state == IEC_STATE_LISTENER) {
            /* Enter listener sync: keep DATA asserted (from byte ACK).
             * The C64 checks DATA_IN to verify listener presence before
             * sending data.  We release DATA only when CLK goes HIGH
             * (C64 signals "ready to send"). */
            IEC_DBG("IEC: -> LISTEN_SYNC (DATA held)\n");
            /* DATA stays asserted from SM_ATN_BYTE_DONE */
            sm_state = SM_LISTEN_SYNC;
            sm_wait_count = 0;
            sm_eoi = false;
            return;
        }

        /* Not addressed -- go idle */
        IEC_DBG("IEC: -> IDLE (not addressed)\n");
        sm_state = SM_IDLE;
        iec_release_all();
        return;
    }

    sm_prev_atn = atn_now;

    /* ---- State machine ---- */

    switch (sm_state) {

    case SM_IDLE:
        /* Nothing to do unless ATN detected (handled above) */
        if (!atn_now && device.state == IEC_STATE_TALKER) {
            /* We are in talker state but SM is idle -- start talk setup */
            IEC_RELEASE(IEC_PIN_DATA);
            IEC_ASSERT(IEC_PIN_CLK);
            sm_state = SM_TALK_SETUP;
            sm_eoi = false;
        }
        break;

    /* ---- ATN byte reception (receiving command bytes under ATN) ---- */

    case SM_ATN_DATA_ASSERTED:
        /* DATA is asserted as ATN ack. Wait for controller to assert CLK
         * (CLK goes LOW). The C64 asserts ATN first, then CLK in a
         * separate $DD00 write. We must wait for CLK LOW before we can
         * wait for CLK HIGH (release). */
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK is now LOW (asserted by controller). Wait for release. */
            sm_state = SM_ATN_WAIT_CLK_RELEASE;
        }
        break;

    case SM_ATN_WAIT_CLK_RELEASE:
        /* CLK was LOW (controller asserted). Wait for CLK to go HIGH
         * (controller releases CLK = ready to send byte).
         * Don't release DATA immediately -- go to confirm state first.
         * This handles the UNLISTEN case where CLK is released briefly
         * (ED2B) before being re-asserted (ED37). */
        if (!IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK released. Enter confirm state: if CLK stays HIGH,
             * we release DATA. If CLK goes LOW again (UNLISTEN re-assert),
             * we stay with DATA asserted. */
            IEC_DBG("IEC: CLK released, entering confirm (DDRD=%02X PORTD=%02X PIND=%02X)\n",
                    mock_DDRD, mock_PORTD, mock_PIND);
            sm_state = SM_ATN_CLK_RELEASED_CONFIRM;
            sm_wait_count = 0;
        }
        break;

    case SM_ATN_CLK_RELEASED_CONFIRM:
        /* CLK went HIGH. Confirm it stays HIGH before releasing DATA.
         * This handles the UNLISTEN case where the KERNAL briefly releases
         * CLK (at ED2B) before re-asserting it (at ED37). Between these
         * two points there are ~32 firmware steps (4 $DD00 accesses * 8
         * steps each). Using a threshold of 40 ensures the firmware keeps
         * DATA asserted through transient CLK releases while still releasing
         * DATA promptly for the standard "ready to send" CLK release. */
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK went LOW again -- this was a transient release (UNLISTEN).
             * Keep DATA asserted and go back to waiting for CLK HIGH. */
            IEC_DBG("IEC: CLK confirm: CLK went LOW at wc=%d, back to WAIT_CLK_RELEASE\n", sm_wait_count);
            sm_state = SM_ATN_WAIT_CLK_RELEASE;
        } else {
            /* CLK is still HIGH. After enough confirmations, release DATA. */
            sm_wait_count++;
            if (sm_wait_count >= 40) {
                IEC_DBG("IEC: CLK confirm: releasing DATA at wc=%d\n", sm_wait_count);
                IEC_RELEASE(IEC_PIN_DATA);
                sm_state = SM_ATN_DATA_RELEASED;
                sm_byte = 0;
                sm_bit = 0;
            }
        }
        break;

    case SM_ATN_DATA_RELEASED:
        /* Waiting for CLK low (controller starts a bit: asserts CLK,
         * then sets DATA). We do NOT sample DATA here because the
         * talker may not have placed the data bit yet. */
        if (!atn_now) {
            /* ATN released during byte reception -- abort */
            sm_state = SM_IDLE;
            break;
        }
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK went low -- talker is setting up the bit.
             * Wait for CLK to go HIGH to sample the data. */
            sm_state = SM_ATN_WAIT_CLK_HIGH;
        }
        break;

    case SM_ATN_WAIT_CLK_HIGH:
        /* Waiting for CLK to go high (talker releases CLK = bit is valid).
         * Sample DATA now -- the talker has set it before releasing CLK. */
        if (!IEC_IS_LOW(IEC_PIN_CLK)) {
            if (!IEC_IS_LOW(IEC_PIN_DATA)) {
                sm_byte |= (1 << sm_bit);
            }
            sm_bit++;
            if (sm_bit >= 8) {
                /* Full byte received -- acknowledge */
                IEC_ASSERT(IEC_PIN_DATA);
                sm_state = SM_ATN_BYTE_DONE;
            } else {
                /* More bits to receive */
                sm_state = SM_ATN_DATA_RELEASED;
            }
        }
        break;

    case SM_ATN_BYTE_DONE:
        /* Command byte received and acknowledged. Process it. */
        IEC_DBG("IEC: ATN byte=$%02X, state=%d\n", sm_byte, device.state);
        iec_process_atn_command(sm_byte);

        /* Check if ATN is still asserted (more commands coming) */
        if (atn_now) {
            /* Ready for next command byte. DATA is already asserted (byte ACK).
             * CLK may be LOW from the end of the bit-send loop.
             * Go to SM_ATN_DATA_ASSERTED which requires seeing CLK go
             * from HIGH to LOW (a fresh edge) before advancing. */
            sm_state = SM_ATN_DATA_ASSERTED;
            sm_byte = 0;
            sm_bit = 0;
            sm_wait_count = 0;
        } else {
            /* ATN released -- handled by edge detection above on next call */
            sm_state = SM_IDLE;
        }
        break;

    /* ---- Talker states (sending data bytes to listener) ---- */

    case SM_TALK_SETUP: {
        /* CLK is asserted (talker not ready). Get the byte to send.
         * Don't wait for DATA — after turnaround the C64 still has DATA
         * asserted (KERNAL turnaround $EDC7-$EDDC asserts DATA at $EDCD).
         * Hold CLK asserted briefly so the turnaround's CLK_IN=0 check
         * at $EDD6 can see it, then release CLK so ACPTR's CLK_IN=1 check
         * at $EE1B can proceed. */
        if (atn_now) {
            sm_state = SM_IDLE;
            break;
        }
        uint8_t byte;
        bool eoi;
        if (!cbm_dos_talk_byte(device.current_sa, &byte, &eoi)) {
            /* No more data */
            IEC_DBG("IEC: TALK no more data, SA=%d\n", device.current_sa);
            device.state = IEC_STATE_IDLE;
            iec_release_all();
            sm_state = SM_IDLE;
            break;
        }
        sm_byte = byte;
        sm_eoi = eoi;
        sm_bit = 0;
        sm_wait_count = 0;

        IEC_DBG("IEC: TALK send $%02X eoi=%d SA=%d\n", byte, eoi, device.current_sa);

        /* Keep CLK asserted; hold it for a delay so turnaround sees CLK LOW */
        sm_state = SM_TALK_CLK_HOLD;
        break;
    }

    case SM_TALK_CLK_HOLD:
        /* Hold CLK asserted so the turnaround's debounce read ($EEA9)
         * sees stable CLK_IN=0. The turnaround generates ~38 fw steps
         * from ATN release to debounce completion (2 subroutine calls
         * × 2 accesses × 8 steps + 2 debounce reads × 8 steps).
         * Release CLK at 40+ steps so it transitions during ACPTR's
         * JSR $EE85 setup, before ACPTR's debounce reads CLK_IN=1.
         *
         * Always go to WAIT_LISTENER first. For both EOI and non-EOI,
         * we need to wait for the listener to release DATA (= ready).
         * For EOI, WAIT_LISTENER then holds CLK released until the
         * listener sends the EOI ack pulse. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        sm_wait_count++;
        if (sm_wait_count > 40) {
            IEC_RELEASE(IEC_PIN_CLK);
            sm_state = SM_TALK_WAIT_LISTENER;
        }
        break;

    case SM_TALK_WAIT_LISTENER:
        /* CLK released (ready to send). Wait for listener to release DATA
         * (listener ready signal).
         * For non-EOI: assert CLK and send first bit.
         * For EOI: keep CLK released; enter EOI wait for listener ack. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            if (sm_eoi) {
                /* CLK stays released for EOI timeout detection.
                 * Wait for listener to pulse DATA low (EOI ack). */
                sm_state = SM_TALK_EOI_WAIT_DATA_LOW;
            } else {
                IEC_ASSERT(IEC_PIN_CLK);
                sm_state = SM_TALK_SEND_BIT;
            }
        }
        break;

    case SM_TALK_EOI_WAIT_DATA_LOW:
        /* EOI: CLK released, waiting for listener to pulse DATA low */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_DATA)) {
            sm_state = SM_TALK_EOI_WAIT_DATA_HIGH;
        }
        break;

    case SM_TALK_EOI_WAIT_DATA_HIGH:
        /* EOI: waiting for listener to release DATA */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (!IEC_IS_LOW(IEC_PIN_DATA)) {
            /* Listener acknowledged EOI. Assert CLK and start sending. */
            IEC_ASSERT(IEC_PIN_CLK);
            sm_state = SM_TALK_SEND_BIT;
        }
        break;

    case SM_TALK_SEND_BIT:
        /* Set DATA line for current bit. CLK stays asserted (bit setup). */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (sm_byte & (1 << sm_bit)) {
            IEC_RELEASE(IEC_PIN_DATA);
        } else {
            IEC_ASSERT(IEC_PIN_DATA);
        }
        sm_wait_count = 0;
        sm_state = SM_TALK_BIT_SETUP_HOLD;
        break;

    case SM_TALK_BIT_SETUP_HOLD:
        /* Hold CLK asserted (bit setup phase) so the C64's debounce
         * reads see stable CLK_IN=0. Then release CLK = bit valid. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        sm_wait_count++;
        if (sm_wait_count > 24) {
            IEC_RELEASE(IEC_PIN_CLK);
            sm_wait_count = 0;
            sm_state = SM_TALK_CLK_HIGH;
        }
        break;

    case SM_TALK_CLK_HIGH:
        /* CLK released (bit valid). Hold so C64's debounce reads see
         * stable CLK_IN=1. Then re-assert CLK for next bit. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        sm_wait_count++;
        if (sm_wait_count > 20) {
            IEC_ASSERT(IEC_PIN_CLK);
            sm_bit++;
            if (sm_bit >= 8) {
                /* All 8 bits sent -- release DATA, wait for listener ack */
                IEC_RELEASE(IEC_PIN_DATA);
                sm_state = SM_TALK_WAIT_ACK;
            } else {
                sm_state = SM_TALK_SEND_BIT;
            }
        }
        break;

    case SM_TALK_WAIT_ACK:
        /* All bits sent, waiting for listener to pull DATA low (ack) */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_DATA)) {
            /* Listener acknowledged. */
            if (sm_eoi) {
                /* Last byte sent -- go idle */
                device.state = IEC_STATE_IDLE;
                iec_release_all();
                sm_state = SM_IDLE;
            } else {
                /* More bytes to send */
                sm_state = SM_TALK_SETUP;
            }
        }
        break;

    /* ---- Listener states (receiving data bytes without ATN) ---- */

    case SM_LISTEN_SYNC:
        /* Phase 1: Wait for CLK LOW.
         * After ATN release or between bytes, CLK may still be HIGH
         * (e.g., from the last bit's CLK release). We must see CLK go LOW
         * first to clear any leftover state before waiting for the
         * "ready to send" CLK HIGH edge in SYNC2. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            sm_state = SM_LISTEN_SYNC2;
        }
        break;

    case SM_LISTEN_SYNC2:
        /* Phase 2: Wait for CLK HIGH (C64 releases CLK = "ready to send").
         * Then release DATA ("ready to receive") and enter LISTEN_READY. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (!IEC_IS_LOW(IEC_PIN_CLK)) {
            IEC_DBG("IEC: LISTEN_SYNC2 -> LISTEN_READY (CLK HIGH, DATA released)\n");
            IEC_RELEASE(IEC_PIN_DATA);
            sm_state = SM_LISTEN_READY;
            sm_wait_count = 0;
        }
        break;

    case SM_LISTEN_READY:
        /* DATA released, waiting for talker to pull CLK low.
         * Do NOT sample DATA here -- talker sets DATA after CLK low.
         * Sample when CLK goes HIGH (bit valid). */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK low -- bit transfer starting. Wait for CLK release. */
            IEC_DBG("IEC: LISTEN_READY: CLK LOW -> receiving (wc=%u)\n", sm_wait_count);
            sm_byte = 0;
            sm_bit = 0;
            sm_state = SM_LISTEN_WAIT_CLK_HIGH;
        } else {
            /* CLK still high -- increment EOI wait counter */
            sm_wait_count++;
            if (sm_wait_count == 1) {
                IEC_DBG("IEC: LISTEN_READY: starting EOI wait (CLK HIGH)\n");
            }
            if (sm_wait_count > IEC_TIMING_EOI_TIMEOUT) {
                /* EOI detected -- acknowledge with DATA pulse */
                IEC_DBG("IEC: LISTEN_READY: EOI detected (wc=%u)\n", sm_wait_count);
                sm_eoi = true;
                IEC_ASSERT(IEC_PIN_DATA);
                sm_state = SM_LISTEN_EOI_ACK;
                sm_wait_count = 0;
            }
        }
        break;

    case SM_LISTEN_EOI_ACK:
        /* EOI ack: hold DATA asserted long enough for C64 to see it.
         * Need to span at least one callback boundary (8 steps) so the
         * C64 gets a chance to read $DD00 and see DATA_IN=0. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        sm_wait_count++;
        if (sm_wait_count > 16) {
            IEC_DBG("IEC: EOI ACK done, releasing DATA\n");
            IEC_RELEASE(IEC_PIN_DATA);
            sm_state = SM_LISTEN_EOI_RELEASED;
        }
        break;

    case SM_LISTEN_EOI_RELEASED:
        /* EOI ack done, waiting for CLK low to start receiving */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            sm_byte = 0;
            sm_bit = 0;
            sm_state = SM_LISTEN_WAIT_CLK_HIGH;
        }
        break;

    case SM_LISTEN_WAIT_CLK_LOW:
        /* Mid-byte: waiting for CLK to go low for the next bit.
         * Unlike SM_LISTEN_READY, this does NOT reset sm_byte/sm_bit
         * and does NOT perform EOI detection.
         * Do NOT sample DATA here -- sample on CLK HIGH. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (IEC_IS_LOW(IEC_PIN_CLK)) {
            sm_state = SM_LISTEN_WAIT_CLK_HIGH;
        }
        break;

    case SM_LISTEN_WAIT_CLK_HIGH:
        /* Waiting for CLK release = bit is valid. Sample DATA now. */
        if (atn_now) { sm_state = SM_IDLE; break; }
        if (!IEC_IS_LOW(IEC_PIN_CLK)) {
            /* CLK released -- sample DATA (bit is valid) */
            if (!IEC_IS_LOW(IEC_PIN_DATA)) {
                sm_byte |= (1 << sm_bit);
            }
            sm_bit++;
            if (sm_bit >= 8) {
                /* Full byte received -- acknowledge */
                IEC_ASSERT(IEC_PIN_DATA);
                sm_state = SM_LISTEN_BYTE_DONE;
            } else {
                /* More bits to receive -- wait for CLK low (next bit) */
                sm_state = SM_LISTEN_WAIT_CLK_LOW;
            }
        }
        break;

    case SM_LISTEN_BYTE_DONE:
        /* Byte received and acknowledged (DATA asserted). Process it. */
        IEC_DBG("IEC: LISTEN byte=$%02X eoi=%d SA=%d\n", sm_byte, sm_eoi, device.current_sa);
        iec_process_listen_byte(sm_byte, sm_eoi);

        if (sm_eoi) {
            /* Last byte */
            sm_state = SM_IDLE;
        } else {
            /* Ready for next byte. Keep DATA asserted (byte ACK) so
             * C64 can see it. Enter LISTEN_SYNC which waits for CLK
             * HIGH (C64 releases CLK for next byte), then releases
             * DATA ("ready to receive"). */
            sm_state = SM_LISTEN_SYNC;
            sm_wait_count = 0;
            sm_eoi = false;
        }
        break;

    } /* switch */
}

#endif /* __AVR__ */
