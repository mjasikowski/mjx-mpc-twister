#ifndef TWISTER_ENCODER_H
#define TWISTER_ENCODER_H
#include "twister_feedback.h"

typedef struct { int seen, value; } twister_encoder;

static inline int twister_delta(twister_encoder *encoder, int value)
{
    return encoder->seen ? value - encoder->value : 0;
}

static inline int twister_is_encoder(unsigned port, unsigned channel, unsigned cc, int value)
{
    return port == 0 && channel == 0 && cc < TWISTER_ENCODERS && value >= 0 && value <= 127;
}

static inline int twister_is_push(unsigned port, unsigned channel, unsigned cc, int value)
{
    return port == 0 && channel == 1 && cc < TWISTER_ENCODERS && (value == 0 || value == 127);
}

static inline unsigned twister_switch_color(unsigned cc, unsigned selected)
{
    static const unsigned actions[] = {43, 64, 85, 117};
    if (cc >= 16) return 0;
    if (cc >= 12) return actions[cc - 12];
    return cc + 1 == (selected & 255) ? selected >> 8 : 0;
}
#endif
