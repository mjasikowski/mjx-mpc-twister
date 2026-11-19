#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include "aarch64_hook.h"
#include "qlink_turn.h"
#include "encoder.h"

enum { COUNT = 16, QUEUE_SIZE = 64 };
static uintptr_t image;
static int enabled;
static a64_hook hooks[7];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void *adapter, *root, *audio_queue, *app, *ui_queue;
static int audio_ready, ui_ready, source = -1;
static atomic_int stopping;
static atomic_uint dispatched, changed, pushed, track_led;
static pthread_t listener;
static int listening;
static unsigned head, pending;
static twister_encoder encoders[COUNT];
static uint64_t activity[COUNT];
static int rings[COUNT];
static unsigned push_head, push_pending;
static unsigned char pushes[QUEUE_SIZE];
static struct { unsigned cc; int delta; } turns[QUEUE_SIZE];

static uintptr_t field(const void *object, size_t offset)
{
    uintptr_t value;
    memcpy(&value, (const unsigned char *)object + offset, sizeof value);
    return value;
}

static void construct(void *self, void *controls, void *model, void *queues)
{
    ((void (*)(void *, void *, void *, void *))hooks[0].trampoline)(self, controls, model, queues);
    int saved = errno;
    if (model && queues && field(self, 0) == image + 0x7b74b70 &&
        field(self, 0x38) == (uintptr_t)model && field(self, 0x28) == (uintptr_t)queues) {
        unsigned count;
        memcpy(&count, (unsigned char *)model + 0x50, sizeof count);
        if (count == COUNT) {
            pthread_mutex_lock(&lock);
            adapter = self;
            root = model;
            audio_queue = (unsigned char *)queues + 0x1e0;
            pending = head = 0;
            audio_ready = 0;
            memset(encoders, 0, sizeof encoders);
            pthread_mutex_unlock(&lock);
            fputs("twister-control: 16-slot adapter captured\n", stderr);
        }
    }
    errno = saved;
}

static void destroy(void *self, unsigned hook)
{
    int saved = errno;
    pthread_mutex_lock(&lock);
    if (adapter == self) {
        adapter = root = audio_queue = NULL;
        pending = head = 0;
        audio_ready = 0;
    }
    pthread_mutex_unlock(&lock);
    errno = saved;
    ((void (*)(void *))hooks[hook].trampoline)(self);
}

static void destruct(void *self) { destroy(self, 1); }
static void delete(void *self) { destroy(self, 2); }

static void app_ctor(void *self, void *a, void *b, void *c, void *d, unsigned e)
{
    ((void (*)(void *, void *, void *, void *, void *, unsigned))hooks[4].trampoline)(self, a, b, c, d, e);
    int saved = errno;
    pthread_mutex_lock(&lock);
    if (adapter && field(self, 0) == image + 0x7beac38 &&
        field(self, 0x810) == (uintptr_t)adapter &&
        field(self, 0x1d8) == field(adapter, 0x28)) {
        app = self;
        ui_queue = (void *)(field(self, 0x1d8) + 0x38);
        push_head = push_pending = 0;
        ui_ready = 0;
        fputs("twister-control: native UI captured\n", stderr);
    }
    pthread_mutex_unlock(&lock);
    errno = saved;
}

static void retire_app(void *self, unsigned hook)
{
    int saved = errno;
    pthread_mutex_lock(&lock);
    if (app == self) {
        app = ui_queue = NULL;
        ui_ready = 0;
        push_head = push_pending = 0;
        atomic_store(&track_led, 0);
    }
    pthread_mutex_unlock(&lock);
    errno = saved;
    ((void (*)(void *))hooks[hook].trampoline)(self);
}

static void app_dtor(void *self) { retire_app(self, 5); }
static void app_delete(void *self) { retire_app(self, 6); }

static int handle_push(unsigned cc, void *project, void *pool, uintptr_t *tracks, unsigned count)
{
    if (cc < 12) {
        if (cc >= count || !tracks[cc] || field((void *)tracks[cc], 0) != image + 0x7cc2dc8)
            return 0;
        ((void (*)(void *, unsigned))(image + 0x2cfc1a0))(project, cc);
        return field(project, 0x140) == tracks[cc];
    }
    void *mode = (void *)field(app, 0x210);
    if (!mode || field(mode, 0) != image + 0x7b69a38) return 0;
    if (cc == 12) {
        uint64_t completion[4] = {0};
        ((int (*)(void *, unsigned, const void *))(image + 0x1bebfb0))(mode, 0x30f4, completion);
    } else if (cc == 14) {
        void *questions = (void *)field(app, 0x220), *factory = (void *)field(app, 0x670);
        void *midi = (void *)field(app, 0x310), *track = (void *)field(project, 0x140);
        if (!questions || field(questions, 0) != image + 0x7b6c718 ||
            !factory || !midi || !track || field(track, 0) != image + 0x7cc2dc8)
            return 0;
        /* Native track-menu action: asks for confirmation before deletion. */
        ((void (*)(void *, void *, void *, void *, void *))(image + 0x2ded6c0))(
            questions, factory, midi, pool, track);
    } else {
        ((void (*)(void *, unsigned))(image + 0x1beb830))(mode, cc == 13 ? 0x2081 : 0x209c);
    }
    return 1;
}

static void service_ui(void)
{
    void *project = (void *)field(app, 0x4b8);
    if (!project || field(project, 0) != image + 0x7cc08a0) goto unavailable;
    void *pool = (void *)field(project, 0x13f0);
    if (!pool) goto unavailable;
    uintptr_t begin = field(pool, 0x110), end = field(pool, 0x118);
    if ((!begin && end) || end < begin || begin % 8 || (end - begin) % 8 || end - begin > 4096 * 8)
        goto unavailable;
    uintptr_t *tracks = (void *)begin;
    unsigned count = (unsigned)((end - begin) / 8);
    ui_ready = 1;
    if (push_pending) {
        unsigned cc = pushes[push_head++ % QUEUE_SIZE];
        --push_pending;
        if (handle_push(cc, project, pool, tracks, count)) atomic_fetch_or(&pushed, 1u << cc);
        return; /* Native actions can change the track vector and selection. */
    }
    uintptr_t current = field(project, 0x140);
    unsigned selected = 0;
    for (unsigned i = 0; current && i < count && i < 12; ++i) {
        if (current != tracks[i] || field((void *)current, 0) != image + 0x7cc2dc8) continue;
        uint32_t colour;
        memcpy(&colour, (void *)(current + 0xba8), sizeof colour);
        selected = (i + 1) | (twister_track_color(colour) << 8);
        break;
    }
    atomic_store(&track_led, selected);
    return;
unavailable:
    ui_ready = 0;
    push_head = push_pending = 0;
    atomic_store(&track_led, 0);
}

static void emit_turn(void *model, int delta, unsigned index)
{
    /* Same per-slot signed rotary signal that ACV1QLinks emits on hardware
     * movement (3.10.1.12, 0x14d9160), including its native listeners. */
    unsigned char *states = (void *)field(model, 0x58);
    void (*emit)(void *, int) = (void *)(image + 0x14db180);
    emit(states + index * 0x100 + 0x58, delta);
}

static uintptr_t drain(void *queue)
{
    uintptr_t result = ((uintptr_t (*)(void *))hooks[3].trampoline)(queue);
    int saved = errno;
    if (!pthread_mutex_trylock(&lock)) {
        char name[16];
        if (adapter && queue == audio_queue &&
            !prctl(PR_GET_NAME, name, 0, 0, 0) && !strcmp(name, "Audio Processin")) {
            audio_ready = 1;
            for (unsigned i = 0; i < 16 && pending; ++i) {
                unsigned at = head++ % QUEUE_SIZE;
                --pending;
                unsigned cc = turns[at].cc;
                unsigned index = twister_qlink_index(cc);
                unsigned char *states = (void *)field(root, 0x58);
                if (!states) continue;
                uint32_t before, after;
                unsigned char *value = states + index * 0x100 + 0xfc;
                memcpy(&before, value, sizeof before);
                if (!qlink_turn(root, adapter, index, turns[at].delta,
                    (qlink_touch_fn)(image + 0x1bc4dc4),
                    emit_turn, (qlink_release_fn)(image + 0x1136860))) {
                    atomic_fetch_or(&dispatched, 1u << cc);
                    memcpy(&after, value, sizeof after);
                    if (after != before) atomic_fetch_or(&changed, 1u << cc);
                }
            }
            unsigned char *states = (void *)field(root, 0x58);
            for (unsigned cc = 0; states && cc < COUNT; ++cc) {
                float value;
                memcpy(&value, states + twister_qlink_index(cc) * 0x100 + 0xfc, sizeof value);
                rings[cc] = value >= 0 && value <= 1 ? twister_midi_value(value) : 0;
            }
        } else if (app && queue == ui_queue) {
            service_ui();
        }
        pthread_mutex_unlock(&lock);
    }
    errno = saved;
    return result;
}

static int (*seq_input)(snd_seq_t *, snd_seq_event_t **);
static int (*client_id)(snd_seq_t *);
static size_t (*info_size)(void);
static int (*get_info)(snd_seq_t *, int, snd_seq_client_info_t *);
static const char *(*info_name)(const snd_seq_client_info_t *);
static __typeof__(&snd_seq_open) seq_open;
static __typeof__(&snd_seq_close) seq_close;
static __typeof__(&snd_seq_set_client_name) set_name;
static __typeof__(&snd_seq_create_simple_port) create_port;
static __typeof__(&snd_seq_poll_descriptors) descriptors;
static __typeof__(&snd_seq_connect_from) connect_from;
static __typeof__(&snd_seq_query_next_client) next_client;
static __typeof__(&snd_seq_client_info_set_client) set_client;
static __typeof__(&snd_seq_client_info_get_client) get_client;
static __typeof__(&snd_seq_client_info_get_type) get_type;
static __typeof__(&snd_seq_port_info_sizeof) port_size;
static __typeof__(&snd_seq_get_any_port_info) get_port;
static __typeof__(&snd_seq_event_output_direct) seq_output;

static uint64_t ms_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int named(snd_seq_t *seq, int id, const char *name)
{
    size_t size = info_size();
    if (!size || size > 4096 || id < 0) return 0;
    snd_seq_client_info_t *info = __builtin_alloca(size);
    memset(info, 0, size);
    if (get_info(seq, id, info) < 0) return 0;
    const char *actual = info_name(info);
    return actual && !strcmp(actual, name);
}

__attribute__((visibility("default")))
int snd_seq_event_input(snd_seq_t *seq, snd_seq_event_t **event)
{
    if (!seq_input) return -ENOSYS;
    for (;;) {
        int result = seq_input(seq, event);
        if (result < 0 || !enabled || !*event) return result;
        const snd_seq_event_t *msg = *event;
        if (msg->type != SND_SEQ_EVENT_CONTROLLER) return result;
        int rotary = twister_is_encoder(msg->source.port, msg->data.control.channel,
                                       msg->data.control.param, msg->data.control.value);
        int push = twister_is_push(msg->source.port, msg->data.control.channel,
                                  msg->data.control.param, msg->data.control.value);
        if (!rotary && !push) return result;
        /* The independent subscriber delivers the turn. Suppress the duplicate
         * copy on MPC's own route only while that subscriber owns the source. */
        pthread_mutex_lock(&lock);
        int owned = (rotary ? audio_ready : ui_ready) && source == msg->source.client;
        pthread_mutex_unlock(&lock);
        if (!owned || !named(seq, client_id(seq), "MPC 3") ||
            !named(seq, msg->source.client, "Midi Fighter Twister"))
            return result;
    }
}

static void change_source(int next)
{
    pthread_mutex_lock(&lock);
    source = next;
    pending = head = 0;
    push_pending = push_head = 0;
    memset(encoders, 0, sizeof encoders);
    memset(activity, 0, sizeof activity);
    atomic_store(&dispatched, 0);
    atomic_store(&changed, 0);
    atomic_store(&pushed, 0);
    pthread_mutex_unlock(&lock);
}

static void enqueue(unsigned cc, int value)
{
    pthread_mutex_lock(&lock);
    activity[cc] = ms_now();
    int delta = twister_delta(&encoders[cc], value);
    if (audio_ready && delta && pending < QUEUE_SIZE) {
        unsigned at = (head + pending++) % QUEUE_SIZE;
        turns[at].cc = cc;
        turns[at].delta = delta;
    }
    encoders[cc] = (twister_encoder){1, value};
    pthread_mutex_unlock(&lock);
}

static int send_cc(snd_seq_t *seq, int port, int destination, unsigned channel, unsigned cc, unsigned value)
{
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_seq_ev_set_source(&event, port);
    snd_seq_ev_set_dest(&event, destination, 0);
    snd_seq_ev_set_direct(&event);
    snd_seq_ev_set_controller(&event, channel, cc, value);
    return seq_output(seq, &event);
}

static unsigned feedback(snd_seq_t *seq, int port, int destination, twister_feedback_state *state, uint64_t now)
{
    unsigned sent = 0;
    unsigned selected = atomic_load(&track_led);
    for (unsigned cc = 0; cc < COUNT; ++cc)
        if (send_cc(seq, port, destination, 1, cc, twister_switch_color(cc, selected)) >= 0) sent |= 1;
    for (unsigned cc = 0; cc < COUNT; ++cc) {
        pthread_mutex_lock(&lock);
        int value = rings[cc];
        uint64_t last = activity[cc];
        int due = audio_ready && twister_feedback_due(state, cc, value, now, last);
        pthread_mutex_unlock(&lock);
        if (due && send_cc(seq, port, destination, 0, cc, value) >= 0) {
            pthread_mutex_lock(&lock);
            encoders[cc] = (twister_encoder){1, value};
            pthread_mutex_unlock(&lock);
            twister_feedback_sent(state, cc, value, last);
            sent |= 2;
        }
    }
    return sent;
}

static int find_twister(snd_seq_t *seq, int port)
{
    size_t size = info_size();
    if (!size || size > 4096) return -1;
    snd_seq_client_info_t *info = __builtin_alloca(size);
    memset(info, 0, size);
    set_client(info, -1);
    while (next_client(seq, info) >= 0) {
        const char *name = info_name(info);
        if (get_type(info) != SND_SEQ_KERNEL_CLIENT || !name || strcmp(name, "Midi Fighter Twister"))
            continue;
        int id = get_client(info);
        if (!connect_from(seq, port, id, 0)) return id;
    }
    return -1;
}

static void *listen_twister(void *unused)
{
    (void)unused;
    snd_seq_t *seq = NULL;
    if (seq_open(&seq, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) goto failed;
    if (set_name(seq, "MJX MPC Twister") < 0) goto failed;
    int port = create_port(seq, "Twister input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                           SND_SEQ_PORT_TYPE_APPLICATION);
    int output = create_port(seq, "Twister LEDs", SND_SEQ_PORT_CAP_READ, SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0 || output < 0 || connect_from(seq, port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE) < 0)
        goto failed;
    struct pollfd fd;
    if (descriptors(seq, &fd, 1, POLLIN) != 1) goto failed;
    size_t size = port_size();
    if (!size || size > 4096) goto failed;
    snd_seq_port_info_t *info = __builtin_alloca(size);
    memset(info, 0, size);
    int attached = -1, own = client_id(seq);
    unsigned reported = 0, reported_change = 0, reported_push = 0, reported_led = 0;
    uint64_t next_feedback = 0;
    twister_feedback_state led;
    fputs("twister-control: hotplug listener ready\n", stderr);
    while (!atomic_load(&stopping)) {
        if (attached < 0) {
            attached = find_twister(seq, port);
            if (attached >= 0) {
                change_source(attached);
                reported = 0;
                reported_change = 0;
                reported_push = 0;
                reported_led = 0;
                next_feedback = 0;
                twister_feedback_reset(&led);
                fprintf(stderr, "twister-control: attached to ALSA %d:0\n", attached);
            }
        }
        unsigned routed = atomic_load(&dispatched), moved = atomic_load(&changed), acted = atomic_load(&pushed);
        for (unsigned cc = 0; cc < COUNT; ++cc) {
            unsigned bit = 1u << cc;
            if (routed & ~reported & bit) fprintf(stderr, "twister-control: encoder %u dispatched\n", cc + 1);
            if (moved & ~reported_change & bit)
                fprintf(stderr, "twister-control: encoder %u changed native slot %u\n", cc + 1, twister_qlink_index(cc));
            if (acted & ~reported_push & bit) fprintf(stderr, "twister-control: push %u native action invoked\n", cc + 1);
        }
        reported |= routed; reported_change |= moved; reported_push |= acted;
        uint64_t now = ms_now();
        if (attached >= 0 && now >= next_feedback) {
            unsigned sent = feedback(seq, output, attached, &led, now);
            if (sent & ~reported_led & 1) fputs("twister-control: switch LED feedback sent\n", stderr);
            if (sent & ~reported_led & 2) fputs("twister-control: rotary LED feedback sent\n", stderr);
            reported_led |= sent;
            next_feedback = now + 250;
        }
        int result = poll(&fd, 1, 150);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || (result && (fd.revents & (POLLERR | POLLHUP | POLLNVAL)))) break;
        if (!result) continue;
        snd_seq_event_t *msg;
        for (unsigned i = 0; i < 128 && seq_input(seq, &msg) >= 0; ++i) {
            if (msg->source.client == SND_SEQ_CLIENT_SYSTEM &&
                msg->source.port == SND_SEQ_PORT_SYSTEM_ANNOUNCE && attached >= 0) {
                int exit = (msg->type == SND_SEQ_EVENT_CLIENT_EXIT ||
                           (msg->type == SND_SEQ_EVENT_PORT_EXIT && msg->data.addr.port == 0)) &&
                           msg->data.addr.client == attached;
                int unsubscribed = msg->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED &&
                    msg->data.connect.sender.client == attached && msg->data.connect.sender.port == 0 &&
                    msg->data.connect.dest.client == own && msg->data.connect.dest.port == port;
                /* Ignore a queued notification for an earlier incarnation of
                 * the same ALSA address, unless our route was actually lost. */
                if (unsubscribed || (exit && get_port(seq, attached, 0, info) < 0)) {
                    attached = -1;
                    change_source(-1);
                    fputs("twister-control: detached\n", stderr);
                }
            } else if (attached >= 0 && msg->source.client == attached && msg->type == SND_SEQ_EVENT_CONTROLLER) {
                unsigned cc = msg->data.control.param;
                int value = msg->data.control.value;
                if (twister_is_encoder(msg->source.port, msg->data.control.channel, cc, value)) {
                    enqueue(cc, value);
                } else if (twister_is_push(msg->source.port, msg->data.control.channel, cc, value)) {
                    pthread_mutex_lock(&lock);
                    if (ui_ready && push_pending < QUEUE_SIZE)
                        pushes[(push_head + push_pending++) % QUEUE_SIZE] = cc;
                    pthread_mutex_unlock(&lock);
                }
            }
        }
    }
    if (attached >= 0) for (unsigned cc = 0; cc < COUNT; ++cc) {
        send_cc(seq, output, attached, 0, cc, 0);
        send_cc(seq, output, attached, 1, cc, 0);
    }
    change_source(-1);
    seq_close(seq);
    return NULL;
failed:
    if (seq) seq_close(seq);
    fputs("twister-control: hotplug listener failed\n", stderr);
    return NULL;
}

static int identify(struct dl_phdr_info *info, size_t size, void *unused)
{
    (void)size; (void)unused;
    if (info->dlpi_name[0]) return 0;
    const unsigned char id[] = {
        0x8b,0x28,0x17,0x9d,0x89,0x27,0x7a,0x5f,0x73,0xc3,
        0x0b,0xe9,0x07,0x40,0x76,0x6e,0x61,0x66,0xb8,0x37
    };
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *h = &info->dlpi_phdr[i];
        if (h->p_type != PT_NOTE) continue;
        const unsigned char *p = (const void *)(info->dlpi_addr + h->p_vaddr);
        size_t remaining = h->p_filesz;
        while (remaining >= sizeof(ElfW(Nhdr))) {
            ElfW(Nhdr) n;
            memcpy(&n, p, sizeof n);
            p += sizeof n; remaining -= sizeof n;
            size_t names = ((size_t)n.n_namesz + 3) & ~(size_t)3;
            size_t desc = ((size_t)n.n_descsz + 3) & ~(size_t)3;
            if (names > remaining || desc > remaining - names) break;
            if (n.n_type == NT_GNU_BUILD_ID && n.n_namesz == 4 &&
                n.n_descsz == sizeof id && !memcmp(p, "GNU", 4) && !memcmp(p + names, id, sizeof id))
                image = info->dlpi_addr;
            p += names + desc; remaining -= names + desc;
        }
    }
    return 1;
}

__attribute__((constructor)) static void start(void)
{
    seq_input = dlsym(RTLD_NEXT, "snd_seq_event_input");
    char exe[128];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe);
    if (n != 12 || memcmp(exe, "/usr/bin/MPC", 12)) return;
    client_id = dlsym(RTLD_NEXT, "snd_seq_client_id");
    info_size = dlsym(RTLD_NEXT, "snd_seq_client_info_sizeof");
    get_info = dlsym(RTLD_NEXT, "snd_seq_get_any_client_info");
    info_name = dlsym(RTLD_NEXT, "snd_seq_client_info_get_name");
#define LOAD(var, sym) if (!(var = dlsym(RTLD_NEXT, #sym))) return
    LOAD(seq_open, snd_seq_open);
    LOAD(seq_close, snd_seq_close);
    LOAD(set_name, snd_seq_set_client_name);
    LOAD(create_port, snd_seq_create_simple_port);
    LOAD(descriptors, snd_seq_poll_descriptors);
    LOAD(connect_from, snd_seq_connect_from);
    LOAD(next_client, snd_seq_query_next_client);
    LOAD(set_client, snd_seq_client_info_set_client);
    LOAD(get_client, snd_seq_client_info_get_client);
    LOAD(get_type, snd_seq_client_info_get_type);
    LOAD(port_size, snd_seq_port_info_sizeof);
    LOAD(get_port, snd_seq_get_any_port_info);
    LOAD(seq_output, snd_seq_event_output_direct);
#undef LOAD
    dl_iterate_phdr(identify, NULL);
    if (!image || !seq_input || !client_id || !info_size || !get_info || !info_name) return;
    const uintptr_t offsets[] = {0x2322ea0,0x2323360,0x2323440,0x30b61a0,0x16326a0,0x163ffc0,0x1643620};
    const uint32_t words[][4] = {
        {0xd503233f,0xd102c3ff,0xb002e184,0xf9445c84},
        {0xd503233f,0xa9bd7bfd,0xb002c281,0x912dc021},
        {0xd503233f,0xa9bd7bfd,0xb002c281,0x912dc021},
        {0xd503233f,0xa9bd7bfd,0x910003fd,0xa90153f3},
        {0xd503233f,0xa9ba7bfd,0x910003fd,0xa90153f3},
        {0xd503233f,0xd10203ff,0xf0032d43,0xf0032d42},
        {0xd503233f,0xa9be7bfd,0x910003fd,0xf9000bf3}
    };
    const uint32_t signal_words[] = {0xd503233f,0xd101c3ff,0x900353c2,0xf9445c42};
    if (memcmp((void *)(image + 0x14db180), signal_words, sizeof signal_words)) return;
    void *const fn[] = {construct, destruct, delete, drain, app_ctor, app_dtor, app_delete};
    for (unsigned i = 0; i < 7; ++i)
        if (memcmp((void *)(image + offsets[i]), words[i], sizeof words[i])) return;
    for (unsigned i = 0; i < 7; ++i) {
        if (!a64_install(&hooks[i], (void *)(image + offsets[i]), words[i], fn[i])) continue;
        while (i) if (a64_remove(&hooks[--i])) _exit(125);
        return;
    }
    enabled = 1;
    listening = !pthread_create(&listener, NULL, listen_twister, NULL);
    fputs("twister-control: 3.10.1.12 hooks ready\n", stderr);
}

__attribute__((destructor)) static void stop(void)
{
    if (listening) {
        atomic_store(&stopping, 1);
        pthread_join(listener, NULL);
    }
}
