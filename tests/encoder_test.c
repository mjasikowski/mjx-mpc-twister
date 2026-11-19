#include <assert.h>
#include "encoder.h"
#include "qlink_turn.h"

static unsigned stage, slot;
static void touch(void *root, unsigned index, int on)
{ assert(root == &stage && index == slot && on == 1 && stage++ == 0); }
static void turn(void *root, int delta, unsigned index)
{ assert(root == &stage && index == slot && delta == -127 && stage++ == 1); }
static void release(void *receiver, unsigned index, unsigned ms)
{ assert(receiver == &slot && index == slot && ms == 250 && stage++ == 2); }

int main(void)
{
    const unsigned order[] = {12,8,4,0,13,9,5,1,14,10,6,2,15,11,7,3};
    twister_encoder encoders[16] = {0};
    assert(!twister_is_encoder(1, 0, 0, 0));
    assert(!twister_is_encoder(0, 1, 0, 0));
    assert(!twister_is_encoder(0, 0, 16, 0));
    assert(!twister_is_encoder(0, 0, 0, -1));
    assert(!twister_is_encoder(0, 0, 0, 128));
    assert(!twister_is_push(1, 1, 0, 127));
    assert(!twister_is_push(0, 0, 0, 127));
    assert(!twister_is_push(0, 1, 16, 127));
    assert(!twister_is_push(0, 1, 0, 64));
    const unsigned colours[] = {43, 64, 85, 117};
    for (unsigned cc = 0; cc < 16; ++cc) {
        assert(twister_is_encoder(0, 0, cc, 0));
        assert(twister_is_encoder(0, 0, cc, 127));
        assert(twister_is_push(0, 1, cc, 0));
        assert(twister_is_push(0, 1, cc, 127));
        assert(twister_switch_color(cc, 0) == (cc < 12 ? 0 : colours[cc - 12]));
        assert(twister_switch_color(cc, (85u << 8) | 3u) ==
               (cc >= 12 ? colours[cc - 12] : cc == 2 ? 85 : 0));
        slot = twister_qlink_index(cc);
        assert(slot == order[cc]);
        assert(twister_delta(&encoders[cc], 127) == 0);
        encoders[cc] = (twister_encoder){1, 127};
        assert(twister_delta(&encoders[cc], 127) == 0);
        stage = 0;
        assert(!qlink_turn(&stage, &slot, slot, twister_delta(&encoders[cc], 0),
                              touch, turn, release));
        assert(stage == 3);
        encoders[cc] = (twister_encoder){0};
        assert(twister_delta(&encoders[cc], 0) == 0);
    }
    assert(qlink_turn(&stage, &slot, 16, -1, touch, turn, release) < 0);
    assert(qlink_turn(&stage, &slot, slot, 0, touch, turn, release) < 0);
}
