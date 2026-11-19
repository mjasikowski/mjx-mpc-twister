#ifndef TWISTER_FEEDBACK_H
#define TWISTER_FEEDBACK_H
#include <stdint.h>

enum { TWISTER_ENCODERS=16, TWISTER_FEEDBACK_IDLE_MS=250 };
typedef struct {
    int value[TWISTER_ENCODERS];
    uint64_t activity[TWISTER_ENCODERS];
} twister_feedback_state;

static inline unsigned twister_qlink_index(unsigned cc) {
    return (3-cc%4)*4+cc/4;
}
/* MPC's native rotary-light property is already normalized to 0..1. */
static inline int twister_midi_value(float normalized) {
    return (int)(normalized*127.0f+0.5f);
}
static inline unsigned twister_track_color(uint32_t argb) {
    int r=(argb>>16)&255,g=(argb>>8)&255,b=argb&255;
    int max=r>g ? r : g;max=max>b ? max : b;
    int min=r<g ? r : g;min=min<b ? min : b;
    int delta=max-min;
    /* The switch palette contains hues, not arbitrary RGB or grayscale.
     * Keep its configured active color for achromatic track colors. */
    if(!delta)return 127;
    int hue;
    if(max==r)hue=60*(g-b)/delta;
    else if(max==g)hue=120+60*(b-r)/delta;
    else hue=240+60*(r-g)/delta;
    if(hue<0)hue+=360;
    /* Classic Twister scale: blue 1, cyan 22, green 43, yellow 64,
     * red 85, magenta 106. 126/127 are reserved by the switch protocol. */
    unsigned value=1+(((240-hue+360)%360)*126+180)/360;
    return value>125 ? 125 : value;
}
static inline void twister_feedback_reset(twister_feedback_state *state) {
    for(unsigned cc=0;cc<TWISTER_ENCODERS;cc++) {
        state->value[cc]=-1;state->activity[cc]=0;
    }
}
static inline int twister_feedback_due(const twister_feedback_state *state,
        unsigned cc,int value,uint64_t now,uint64_t activity) {
    /* Twister ignores absolute feedback during its 128 ms activity window.
     * Wait beyond it, then refresh even if a turn ended at the old value. */
    return now-activity>=TWISTER_FEEDBACK_IDLE_MS &&
        (state->value[cc]!=value || state->activity[cc]!=activity);
}
static inline void twister_feedback_sent(twister_feedback_state *state,
        unsigned cc,int value,uint64_t activity) {
    state->value[cc]=value;state->activity[cc]=activity;
}
#endif
