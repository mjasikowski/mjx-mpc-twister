#ifndef QLINK_TURN_H
#define QLINK_TURN_H
typedef void (*qlink_touch_fn)(void*,unsigned,int);
typedef void (*qlink_delta_fn)(void*,int,unsigned);
typedef void (*qlink_release_fn)(void*,unsigned,unsigned);

/* Shared owner-confirmed Twister sequence. Caller owns exact-build receivers,
 * lifetime and native audio-thread dispatch. No current+step approximation. */
static inline int qlink_turn(void *root,void *receiver,unsigned index,int delta,
    qlink_touch_fn touch,qlink_delta_fn turn,qlink_release_fn release)
{
    if (!root || !receiver || index>=16 || !delta || delta < -127 || delta>127 || !touch || !turn || !release) return -1;
    touch(root,index,1);turn(root,delta,index);release(receiver,index,250);return 0;
}
#endif
