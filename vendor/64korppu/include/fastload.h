#ifndef FASTLOAD_H
#define FASTLOAD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FASTLOAD_NONE = 0,
    FASTLOAD_JIFFYDOS,
    FASTLOAD_BURST,
    FASTLOAD_EPYX,
    FASTLOAD_MAX
} fastload_type_t;

typedef struct {
    fastload_type_t type;
    const char     *name;
    bool (*detect)(void);
    bool (*send_byte)(uint8_t byte, bool eoi);
    bool (*receive_byte)(uint8_t *byte, bool *eoi);
    void (*on_atn_end)(void);
} fastload_protocol_t;

/* Registry */
void fastload_init(void);
void fastload_register(const fastload_protocol_t *proto);
fastload_type_t fastload_detect(void);
const fastload_protocol_t *fastload_active(void);
void fastload_reset(void);

#endif /* FASTLOAD_H */
