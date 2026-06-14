/* rgschan.c: Sound channel objects
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include "glk.h"
#include "remglk.h"

/* RemGlk itself plays no audio (it's a text protocol). For the embedded iOS /
 * Android apps, though, we want sound: so instead of stubbing the schannel
 * calls, we keep lightweight channel objects and RECORD each play/stop/volume
 * op. The ops are flushed into the next update's "schannel" array (see
 * gli_schannel_print_ops, called from data_update_print); the app then pulls
 * the sound's bytes from the blorb and plays them. */

#ifdef GLK_MODULE_SOUND

struct glk_schannel_struct {
    glui32 id;
    glui32 volume;          /* 0..0x10000, current channel volume */
    glui32 rock;
    struct glk_schannel_struct *next;
};

static schanid_t schannel_list = NULL;
static glui32 next_chan_id = 1;

/* A recorded op: 'p' play, 's' stop, 'v' set-volume. */
typedef struct {
    char   op;
    glui32 chan, snd, repeats, vol, dur;
} schan_op_t;

static schan_op_t *ops = NULL;
static int ops_count = 0, ops_cap = 0;

static void record_op(char op, glui32 chan, glui32 snd, glui32 repeats,
                      glui32 vol, glui32 dur)
{
    if (ops_count >= ops_cap) {
        ops_cap = ops_cap ? ops_cap * 2 : 8;
        ops = (schan_op_t *) realloc(ops, ops_cap * sizeof(schan_op_t));
    }
    ops[ops_count].op = op;
    ops[ops_count].chan = chan;
    ops[ops_count].snd = snd;
    ops[ops_count].repeats = repeats;
    ops[ops_count].vol = vol;
    ops[ops_count].dur = dur;
    ops_count++;
}

/* Emit the pending ops as a ",\n \"schannel\":[...]" fragment and clear them.
 * Called from data_update_print just before the closing brace. Volumes are the
 * Glk 0..0x10000 range; repeats is signed (-1 == loop forever). */
void gli_schannel_print_ops(void)
{
    int i;
    if (!ops_count)
        return;
    printf(",\n \"schannel\":[\n");
    for (i = 0; i < ops_count; i++) {
        schan_op_t *o = &ops[i];
        switch (o->op) {
            case 'p':
                printf("  {\"op\":\"play\", \"chan\":%lu, \"snd\":%lu, \"repeats\":%ld, \"vol\":%lu}",
                       (unsigned long) o->chan, (unsigned long) o->snd,
                       (long) (glsi32) o->repeats, (unsigned long) o->vol);
                break;
            case 's':
                printf("  {\"op\":\"stop\", \"chan\":%lu}", (unsigned long) o->chan);
                break;
            case 'v':
                printf("  {\"op\":\"volume\", \"chan\":%lu, \"vol\":%lu, \"dur\":%lu}",
                       (unsigned long) o->chan, (unsigned long) o->vol,
                       (unsigned long) o->dur);
                break;
        }
        if (i + 1 < ops_count)
            printf(",");
        printf("\n");
    }
    printf(" ]");
    ops_count = 0;
}

static schanid_t make_channel(glui32 rock, glui32 volume)
{
    schanid_t chan = (schanid_t) malloc(sizeof(struct glk_schannel_struct));
    chan->id = next_chan_id++;
    chan->volume = volume;
    chan->rock = rock;
    chan->next = schannel_list;
    schannel_list = chan;
    return chan;
}

schanid_t glk_schannel_create(glui32 rock)
{
    return make_channel(rock, 0x10000);
}

void glk_schannel_destroy(schanid_t chan)
{
    schanid_t *pp;
    if (!chan)
        return;
    record_op('s', chan->id, 0, 0, 0, 0);   /* a destroyed channel goes silent */
    for (pp = &schannel_list; *pp; pp = &(*pp)->next) {
        if (*pp == chan) { *pp = chan->next; break; }
    }
    free(chan);
}

schanid_t glk_schannel_iterate(schanid_t chan, glui32 *rockptr)
{
    schanid_t next = chan ? chan->next : schannel_list;
    if (rockptr)
        *rockptr = next ? next->rock : 0;
    return next;
}

glui32 glk_schannel_get_rock(schanid_t chan)
{
    if (!chan) {
        gli_strict_warning("schannel_get_rock: invalid id.");
        return 0;
    }
    return chan->rock;
}

glui32 glk_schannel_play(schanid_t chan, glui32 snd)
{
    return glk_schannel_play_ext(chan, snd, 1, 0);
}

glui32 glk_schannel_play_ext(schanid_t chan, glui32 snd, glui32 repeats,
    glui32 notify)
{
    if (!chan) {
        gli_strict_warning("schannel_play_ext: invalid id.");
        return 0;
    }
    if (repeats == 0) {                      /* 0 repeats == play nothing */
        record_op('s', chan->id, 0, 0, 0, 0);
        return 1;
    }
    record_op('p', chan->id, snd, repeats, chan->volume, 0);
    return 1;
}

void glk_schannel_stop(schanid_t chan)
{
    if (!chan) {
        gli_strict_warning("schannel_stop: invalid id.");
        return;
    }
    record_op('s', chan->id, 0, 0, 0, 0);
}

void glk_schannel_set_volume(schanid_t chan, glui32 vol)
{
    glk_schannel_set_volume_ext(chan, vol, 0, 0);
}

void glk_sound_load_hint(glui32 snd, glui32 flag)
{
    /* Nothing to preload; the app reads sounds from the blorb on demand. */
}

#ifdef GLK_MODULE_SOUND2

schanid_t glk_schannel_create_ext(glui32 rock, glui32 volume)
{
    return make_channel(rock, volume);
}

glui32 glk_schannel_play_multi(schanid_t *chanarray, glui32 chancount,
  glui32 *sndarray, glui32 soundcount, glui32 notify)
{
    glui32 ix, played = 0;
    if (chancount != soundcount) {
        gli_strict_warning("schannel_play_multi: channel count does not match sound count.");
        return 0;
    }
    for (ix = 0; ix < chancount; ix++) {
        if (glk_schannel_play_ext(chanarray[ix], sndarray[ix], 1, 0))
            played++;
    }
    return played;
}

void glk_schannel_pause(schanid_t chan)
{
    if (chan)
        record_op('v', chan->id, 0, 0, 0, 0);   /* approximate: drop to silence */
}

void glk_schannel_unpause(schanid_t chan)
{
    if (chan)
        record_op('v', chan->id, 0, 0, chan->volume, 0);
}

void glk_schannel_set_volume_ext(schanid_t chan, glui32 vol,
  glui32 duration, glui32 notify)
{
    if (!chan) {
        gli_strict_warning("schannel_set_volume_ext: invalid id.");
        return;
    }
    chan->volume = vol;
    record_op('v', chan->id, 0, 0, vol, duration);
}

#endif /* GLK_MODULE_SOUND2 */

#endif /* GLK_MODULE_SOUND */
