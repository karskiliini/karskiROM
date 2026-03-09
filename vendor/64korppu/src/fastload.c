#include "fastload.h"
#include <stddef.h>

#define FASTLOAD_MAX_PROTOCOLS 4

static const fastload_protocol_t *protocols[FASTLOAD_MAX_PROTOCOLS];
static uint8_t protocol_count = 0;
static const fastload_protocol_t *active_protocol = NULL;

void fastload_init(void) {
    protocol_count = 0;
    active_protocol = NULL;
}

void fastload_register(const fastload_protocol_t *proto) {
    if (proto && protocol_count < FASTLOAD_MAX_PROTOCOLS) {
        protocols[protocol_count++] = proto;
    }
}

fastload_type_t fastload_detect(void) {
    for (uint8_t i = 0; i < protocol_count; i++) {
        if (protocols[i]->detect && protocols[i]->detect()) {
            active_protocol = protocols[i];
            return protocols[i]->type;
        }
    }
    active_protocol = NULL;
    return FASTLOAD_NONE;
}

const fastload_protocol_t *fastload_active(void) {
    return active_protocol;
}

void fastload_reset(void) {
    active_protocol = NULL;
}
