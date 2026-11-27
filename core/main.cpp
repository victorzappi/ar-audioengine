/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*ar_audioengine
real-time audioreach-based audio engine

usage (QLI 2.1, 4-frame blocks, audio thread on CPU 7 under EVL, performance governor):
./ar_audioengine --evl -m 7 -g -p 4

configure, build and clean commands in CMakeLists.txt
*/

// next steps:
// _improve doc: general, qnn, onnx and maybe rtneural
// _align code style across projects
// _add option to create audioreach_mappings.h at config time, by passing location of kvh2xml.h of target board
// _re-introduce per-direction hardware endpoint / MFC configuration (the old
//  configure_agm_modules, RX-only) for both RX and TX, if needed for clean audio

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <atomic>

#include "cli.h"
#include "pcm_utils.h"
#include "hw_mixer.h"
#include "agm_mixer.h"
#include "cpu_perf.h"
#include "evl_rt.h"
#include "cpu_buffer.h"
#include "render.h"

// we assume a little-endian CPU, like the Codec DMAs: their 32-bit samples are read and
// written as native words
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "ar_audioengine assumes a little-endian CPU");

// ---------------------------------------------------------------------------
// types & globals
// ---------------------------------------------------------------------------

// stream directions; used to index the per-direction runtime context array
enum { DIR_PLAYBACK = 0, DIR_CAPTURE = 1, NUM_DIRS = 2 };

// EVL route (--evl): the audio thread sleeps this long
// between two reads of the capture DMA's position, out-of-band. it wakes about 1 us late
// on CPU 7 (latmus), so it reads the position about every 2 us
#define EVL_POLL_SLEEP_US 1

// the DSP starts capture first and playback this long after the capture DMA's first word,
// so playback reads the same distance ahead of capture on every start. started together,
// the two DMAs land one frame closer or further apart from one start to the next. 10 us is
// about half a frame at 48 kHz: far from where the two codecs' frame ticks fall (within
// 5 us of each other on the RB3 Gen 2)
#define CPU_BUFFER_PLAYBACK_START_DELAY_US 10

struct pcm_ctx {
    struct pcm *pcm;               // opened and started for the graph, never read or written
    unsigned int num_samples;      // samples per block: period_size * channels
    float *audio_buffer;           // render()'s input (capture) or output (playback)
};

std::atomic_int should_stop(0);

// gates this file's informational printf() output (set once in main(), right after
// CLI parsing); error/warning fprintf(stderr, ...) output always prints regardless.
// file-scope so sig_handler (fixed OS callback signature) can read it too.
static bool g_verbose = false;

// EVL route (--evl): set once in main(), after EVL has been found. file-scope so
// the CPU-buffer loop can read it
static bool g_evl = false;

// ---------------------------------------------------------------------------
// device name resolution
// ---------------------------------------------------------------------------

static int set_frontend_name(const char *xml_path,
                              unsigned int virtual_card, unsigned int virtual_device,
                              char **name_out)
{
    FILE *f = fopen(xml_path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", xml_path);
        return -1;
    }

    char line[256], tmp[256];
    unsigned int id_val;
    int card_ok = 0, dev_id_ok = 0;
    int ret = -1;
    enum { OUTSIDE, IN_CARD, IN_DEV } scope = OUTSIDE;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        switch (scope) {
        case OUTSIDE:
            if (strncmp(p, "<card>", 6) == 0)
                scope = IN_CARD;
            break;
        case IN_CARD:
            if (strncmp(p, "</card>", 7) == 0) {
                scope = OUTSIDE;
                card_ok = 0;
            } else if (card_ok && strncmp(p, "<pcm-device>", 12) == 0) {
                scope = IN_DEV;
                dev_id_ok = 0;
            } else if (sscanf(p, "<id>%u</id>", &id_val) == 1) {
                card_ok = (id_val == virtual_card);
            }
            break;
        case IN_DEV:
            if (strncmp(p, "</pcm-device>", 13) == 0) {
                scope = IN_CARD;
                dev_id_ok = 0;
            } else if (sscanf(p, "<id>%u</id>", &id_val) == 1) {
                dev_id_ok = (id_val == virtual_device);
            } else if (dev_id_ok && sscanf(p, "<name>%255[^<]</name>", tmp) == 1) {
                *name_out = strdup(tmp);
                ret = (*name_out != nullptr) ? 0 : -1;
                goto done;
            }
            break;
        }
    }

done:
    fclose(f);
    return ret;
}

static int set_backend_name(unsigned int physical_card, unsigned int physical_device,
                             char pcm_dir, char **backend_name_out)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/asound/card%u/pcm%u%c/info", physical_card, physical_device, pcm_dir);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", path);
        return -1;
    }

    char line[256];
    int ret = -1;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "id:", 3) == 0) {
            char *p = line + 3;
            while (*p == ' ') p++;
            char name[128];
            if (sscanf(p, "%127s", name) == 1) {
                *backend_name_out = strdup(name);
                ret = (*backend_name_out != nullptr) ? 0 : -1;
            }
            break;
        }
    }

    fclose(f);
    return ret;
}

static int resolve_stream_names_dir(const char *cards_xml_path, unsigned int virtual_card,
                                       unsigned int physical_card, struct pcm_stream *stream)
{
    bool auto_retrieve;

    auto_retrieve = (stream->frontend_name == nullptr);
    if (auto_retrieve) {
        if (set_frontend_name(cards_xml_path, virtual_card, stream->virtual_device,
                            &stream->frontend_name) != 0) {
            fprintf(stderr, "Frontend not found for virtual card %u device %u in '%s'\n",
                    virtual_card, stream->virtual_device, cards_xml_path);
            return -1;
        }
    }
    if (g_verbose)
        printf("virtual card: %u, device: %u -> frontend: %s%s\n",
                virtual_card, stream->virtual_device,
                stream->frontend_name, auto_retrieve ? " (auto-retrieved)" : "");

    auto_retrieve = (stream->backend_name == nullptr);
    if (auto_retrieve) {
        char pcm_dir = (stream->flags & PCM_IN) ? 'c' : 'p';
        if (set_backend_name(physical_card, stream->physical_device, pcm_dir, &stream->backend_name) != 0) {
            fprintf(stderr, "Backend not found for physical card %u device %u\n",
                    physical_card, stream->physical_device);
            return -2;
        }
    }
    if (g_verbose)
        printf("physical card: %u, device: %u -> backend: %s%s\n\n",
                physical_card, stream->physical_device,
                stream->backend_name, auto_retrieve ? " (auto-retrieved)" : "");
    return 0;
}

// resolve names for both directions (playback first)
static int resolve_stream_names(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++) {
        if (resolve_stream_names_dir(CARDS_CONF_FILE, settings->virtual_card,
                                        settings->physical_card, streams[d]) < 0)
            return -1;
    }
    return 0;
}

// the Codec DMAs run in the format of the backend config: take each stream's channels and
// rate from there, so that render() and the DMA buffers always agree. both streams must
// run at the same rate, the loop moves one capture and one playback period at a time
static int apply_backend_formats(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++) {
        struct pcm_stream *s = streams[d];
        unsigned int rate = 0, channels = 0, bits = 0;

        if (get_agm_backend_format(BACKEND_CONF_FILE, s->backend_name, &rate, &channels, &bits) != 0 ||
            rate == 0 || channels == 0) {
            fprintf(stderr, "backend %s: no format in %s\n", s->backend_name, BACKEND_CONF_FILE);
            return -1;
        }
        s->config.rate = rate;
        s->config.channels = channels;
        if (g_verbose)
            printf("backend %s: %u Hz, %u channel(s), %u bits (%s)\n", s->backend_name, rate,
                   channels, bits, BACKEND_CONF_FILE);
    }
    if (settings->playback.config.rate != settings->capture.config.rate) {
        fprintf(stderr, "the playback and capture backends run at different rates (%u and %u Hz)\n",
                settings->playback.config.rate, settings->capture.config.rate);
        return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// pcm context & stream lifecycle
// ---------------------------------------------------------------------------

static int init_ctx_dir(struct pcm_ctx* ctx, struct pcm_stream *stream)
{
    struct pcm_config *config = &stream->config;

    ctx->pcm = nullptr;
    ctx->num_samples = config->period_size * config->channels;
    ctx->audio_buffer = (float*)calloc(ctx->num_samples, sizeof(float));
    if ( !(ctx->audio_buffer) ) {
        fprintf(stderr, "unable to allocate %zu bytes\n", ctx->num_samples * sizeof(float));
        return -1;
    }
    return 0;
}

// build a runtime context for both directions
static int init_ctx(struct settings *settings, struct pcm_ctx ctx[])
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++)
        if (init_ctx_dir(&ctx[d], streams[d]) < 0)
            return -1;
    return 0;
}

// free buffers for both contexts (safe on a zero-initialized / partially-built array)
void cleanup_ctx(struct pcm_ctx ctx[])
{
    for (int d = 0; d < NUM_DIRS; d++) {
        if (ctx[d].audio_buffer != nullptr)
            free(ctx[d].audio_buffer);
    }
}

static int init_pcm_dir(struct pcm_ctx* ctx, struct settings *settings, struct pcm_stream *stream)
{
    // we cannot check the param ranges on the frontend, because it's a virtual pcm!

    /* open pcm */
    ctx->pcm = pcm_open(settings->virtual_card,
                        stream->virtual_device,
                        stream->flags,
                        &stream->config);

    if (!ctx->pcm || !pcm_is_ready(ctx->pcm)) {
        fprintf(stderr, "failed to open for pcm %u,%u. %s\n",
                settings->virtual_card, stream->virtual_device,
                pcm_get_error(ctx->pcm));
        pcm_close(ctx->pcm);
        ctx->pcm = nullptr;
        return -1;
    }

    // the loop moves whole DMA periods, so the DMA block must be the period asked for
    // (-p). the PCM is never read or written, so the period it opened with does not matter
    // (the stock AGM plugin rounds it up to 48 frames). set the block on the hardware
    // endpoint before prepare (whole ms on any firmware, anything else needs the custom
    // Codec DMA build)
    if (configure_agm_hw_ep_block(stream->frontend_name, stream->backend_name,
                                  stream->config.period_size, stream->config.rate,
                                  (stream->flags & PCM_IN) != 0) != 0) {
        pcm_close(ctx->pcm);
        ctx->pcm = nullptr;
        return -1;
    }

    if (g_verbose) {
        printf("\nPCM (frontend) config:\n");
        printf("  direction   %s\n",          (stream->flags & PCM_IN) ? "capture" : "playback");
        printf("  rate        %u Hz\n",       stream->config.rate);
        printf("  channels    %u\n",          stream->config.channels);
        printf("  block       %u frames\n\n", stream->config.period_size);
    }

    return 0;
}

// open a pcm for both directions
static int init_pcm(struct settings *settings, struct pcm_ctx ctx[])
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++)
        if (init_pcm_dir(&ctx[d], settings, streams[d]) < 0)
            return -1;
    return 0;
}

// close both pcms (safe on a zero-initialized / partially-opened array)
void cleanup_pcm(struct pcm_ctx ctx[])
{
    if (g_verbose)
        printf("pcm_cleanup\n");
    for (int d = 0; d < NUM_DIRS; d++) {
        if (ctx[d].pcm != nullptr) {
            pcm_close(ctx[d].pcm);
            ctx[d].pcm = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// hardware & graph mixer setup (per-direction encapsulation)
// ---------------------------------------------------------------------------

// apply the hardware mixer path for both directions (set_hw_mixer_path is re-entrant on
// the single shared mixer handle, so this works as-is)
static int setup_hw_mixer_paths(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++)
        if (set_hw_mixer_path(streams[d]->mixer_path) < 0)
            return -1;
    return 0;
}

// build the AGM graph for both directions. The mixer must already be open
// (init_agm_mixer); setup_agm_mixer_graph records each direction's endpoints so
// cleanup_agm_mixer can tear both down.
static int set_agm_mixer_graphs(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };

    for (int d = 0; d < NUM_DIRS; d++) {
        struct pcm_stream *s = streams[d];

        if (setup_agm_mixer_graph(s->frontend_name, s->backend_name, (char *)BACKEND_CONF_FILE,
                                  s->stream_kv, s->instance_kv, s->streampp_kv,
                                  s->devicepp_kv, s->device_kv) < 0)
            return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// signal handling
// ---------------------------------------------------------------------------

void stream_close()
{
    should_stop.store(1);
}

void sig_handler(int sig)
{
    /* allow the stream to be closed gracefully */
    signal(sig, SIG_IGN);
    if (g_verbose)
        printf("\nStopping PCM stream...\n");
    stream_close();

}

// ---------------------------------------------------------------------------
// audio thread & render loop
// ---------------------------------------------------------------------------

// build the render context: capture samples are the input, playback samples the
// output
static struct audio_ctx create_audio_ctx(struct pcm_ctx *pb, struct pcm_ctx *cap,
                                         const struct pcm_config *pb_config,
                                         const struct pcm_config *cap_config)
{
    struct audio_ctx actx = {
        .audio_in     = cap->audio_buffer,
        .audio_out    = pb->audio_buffer,
        .period_size  = pb_config->period_size,
        .channels     = pb_config->channels,
        .sample_rate  = pb_config->rate,
        .in_channels  = cap_config->channels
    };

    return actx;
}

// start the streams, playback first. the DSP enables both Codec DMA interfaces together,
// when the second one starts (sync start). < 0 on error (playback is stopped again if
// capture fails to start)
static int start_streams(struct pcm_ctx *pb, struct pcm_ctx *cap)
{
    if (pcm_start(pb->pcm) < 0) {
        fprintf(stderr, "playback PCM start error: %s (errno=%d)\n", pcm_get_error(pb->pcm), errno);
        return -1;
    }
    if (pcm_start(cap->pcm) < 0) {
        fprintf(stderr, "capture PCM start error: %s (errno=%d)\n", pcm_get_error(cap->pcm), errno);
        pcm_stop(pb->pcm);
        return -1;
    }
    return 0;
}

static void print_hw_ep_start_status(const char *name, const struct hw_ep_start_status *st)
{
    printf("start status: %-8s group %u, interface enabled at %llu us", name, st->sync_group_id,
           (unsigned long long)st->intf_enable_ts_us);
    if (st->sync_group_id)
        printf(" (group: first to last enable %u us)", st->group_enable_spread_us);
    if (st->first_intr_ts_valid)
        printf(", first DMA interrupt at %llu us", (unsigned long long)st->first_intr_ts_us);
    else
        printf(", first DMA interrupt not timestamped");
    printf(", FIFO errors %u, bus errors %u\n", st->fifo_err_cnt, st->bus_err_cnt);
}

// prints the last start of the Codec DMA endpoints (custom Codec DMA build), and how far
// apart the two interfaces started
static void report_hw_ep_start_status(struct settings *settings)
{
    struct hw_ep_start_status pb_st = {}, cap_st = {};

    if (get_agm_hw_ep_start_status(settings->playback.frontend_name, settings->playback.backend_name,
                                   false, &pb_st) != 0)
        return;
    print_hw_ep_start_status("playback", &pb_st);
    if (get_agm_hw_ep_start_status(settings->capture.frontend_name, settings->capture.backend_name,
                                   true, &cap_st) != 0)
        return;
    print_hw_ep_start_status("capture", &cap_st);

    printf("start status: capture - playback: interface enable %+lld us",
           (long long)(cap_st.intf_enable_ts_us - pb_st.intf_enable_ts_us));
    if (pb_st.first_intr_ts_valid && cap_st.first_intr_ts_valid)
        printf(", first DMA interrupt %+lld us",
               (long long)(cap_st.first_intr_ts_us - pb_st.first_intr_ts_us));
    printf("\n");
}

// ---------------------------------------------------------------------------
// the loop: CPU-buffer route
// ---------------------------------------------------------------------------
// the engine works on the Codec DMAs' own double buffers, like Bela: when the capture
// DMA finishes a period, the loop reads it, renders, and writes the playback period
// after the one the playback DMA reads now. the DSP does nothing per block. latency:
// 2 periods from capture to playback. the time for a block, from the end of the capture
// period to the playback DMA reading the period written, is one period less the playback
// DMA's read-ahead (its FIFO watermark, 1 frame by default)

// what the CPU-buffer loop counted
struct cpu_buffer_stats {
    uint64_t blocks;
    unsigned int late;       // the playback DMA reached the period while it was written
    unsigned int skipped;    // a playback period was not written in its turn
    uint64_t first_bad;      // block of the first late or skipped period
};

// before the start: CPU-buffer mode on both endpoints, with their FIFO watermarks, and
// both DMAs start together, playback a fixed time after capture (the loop relies on their
// periods lining up). < 0 on error (printed)
static int cpu_buffer_enable(struct settings *settings)
{
    if (set_agm_hw_ep_cpu_buffer(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, true, settings->playback.dma_watermark) != 0 ||
        set_agm_hw_ep_cpu_buffer(settings->capture.frontend_name, settings->capture.backend_name,
                                 true, true, settings->capture.dma_watermark) != 0)
        return -1;
    if (set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, 1, 2, HW_EP_SYNC_ALIGN_SOURCE,
                                 CPU_BUFFER_PLAYBACK_START_DELAY_US) != 0)
        return -1;
    if (set_agm_hw_ep_sync_start(settings->capture.frontend_name, settings->capture.backend_name,
                                 true, 1, 2, HW_EP_SYNC_ALIGN_SOURCE,
                                 CPU_BUFFER_PLAYBACK_START_DELAY_US) != 0) {
        // don't leave playback waiting for a capture endpoint that never joins
        set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, 0, 0, HW_EP_SYNC_ALIGN_NONE, 0);
        return -1;
    }
    return 0;
}

// after the start: find and map both DMAs. < 0 on error (printed)
static int cpu_buffer_map_dmas(struct settings *settings, const struct pcm_config *pb_config,
                               const struct pcm_config *cap_config, struct cpu_buffer_dma *pb_dma,
                               struct cpu_buffer_dma *cap_dma)
{
    struct hw_ep_cpu_buffer pb_info = {}, cap_info = {};

    if (get_agm_hw_ep_cpu_buffer(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, &pb_info) != 0 ||
        get_agm_hw_ep_cpu_buffer(settings->capture.frontend_name, settings->capture.backend_name,
                                 true, &cap_info) != 0)
        return -1;
    if (cpu_buffer_map(pb_dma, &pb_info, true, pb_config->period_size, pb_config->channels) != 0)
        return -1;
    if (cpu_buffer_map(cap_dma, &cap_info, false, cap_config->period_size, cap_config->channels) != 0) {
        cpu_buffer_unmap(pb_dma);
        return -1;
    }
    if (g_verbose) {
        printf("CPU buffer: playback DMA buffer 0x%08x, %u x %u frames x %u ch, registers 0x%08x, "
               "watermark %u words\n", pb_info.buffer_addr, pb_info.num_periods, pb_config->period_size,
               pb_info.num_channels, pb_info.regs_addr, pb_info.watermark);
        printf("CPU buffer: capture DMA buffer 0x%08x, %u x %u frames x %u ch, registers 0x%08x, "
               "watermark %u words\n", cap_info.buffer_addr, cap_info.num_periods, cap_config->period_size,
               cap_info.num_channels, cap_info.regs_addr, cap_info.watermark);
    }
    return 0;
}

// the loop, until asked to stop. the poll between two reads of the capture DMA's position
// is an out-of-band sleep with EVL, else a busy wait (give the audio thread its own CPU)
static void cpu_buffer_loop(struct settings *settings, struct pcm_ctx *pb, struct pcm_ctx *cap,
                            struct audio_ctx *actx, struct cpu_buffer_dma *pb_dma,
                            struct cpu_buffer_dma *cap_dma, struct cpu_buffer_stats *stats)
{
    unsigned int cap_period = cpu_buffer_period(cap_dma);
    int last_written = -1;

    while (!should_stop.load()) {
        // wait for the capture DMA to finish its period
        unsigned int now = cpu_buffer_period(cap_dma);
        if (now == cap_period) {
            if (g_evl)
                evl_rt_usleep(EVL_POLL_SLEEP_US);
            continue;
        }
        unsigned int done = cap_period;
        cap_period = now;

        cpu_buffer_read_barrier();
        cpu_buffer_read(cap_dma, done, cap->audio_buffer);

        // user API function
        render(actx, settings->user_argv);

        // the playback period after the one the DMA read its last word from: the first
        // one it has not started reading
        unsigned int reading = cpu_buffer_fetched_period(pb_dma);
        unsigned int next = (reading + 1) % pb_dma->num_periods;
        if (last_written >= 0 && next != (unsigned int)(last_written + 1) % pb_dma->num_periods) {
            if (stats->late + stats->skipped == 0)
                stats->first_bad = stats->blocks;
            stats->skipped++;
        }
        cpu_buffer_write(pb_dma, next, pb->audio_buffer);
        memset(pb->audio_buffer, 0, pb->num_samples * sizeof(float));
        cpu_buffer_write_barrier();
        // no word of the block read yet: it was in time
        if (cpu_buffer_fetched_period(pb_dma) != reading) {
            if (stats->late + stats->skipped == 0)
                stats->first_bad = stats->blocks;
            stats->late++;
        }
        last_written = (int)next;
        stats->blocks++;
    }
}

int audio_loop(struct settings *settings, struct pcm_ctx ctx[])
{
    struct pcm_ctx *pb = &ctx[DIR_PLAYBACK];
    struct pcm_ctx *cap = &ctx[DIR_CAPTURE];

    // the PCMs are never read or written: the periods are the ones asked for (-p),
    // whatever period the PCMs opened with
    const struct pcm_config *pb_config = &settings->playback.config;
    const struct pcm_config *cap_config = &settings->capture.config;

    struct audio_ctx actx = create_audio_ctx(pb, cap, pb_config, cap_config);

    // user API function
    if (setup(&actx, settings->user_argv)) {
        fprintf(stderr, "setup function failed\n");
        cleanup(&actx, settings->user_argv);
        return -2;
    }

    // prepare both streams before starting either, so that pcm_start() does not
    // prepare the capture graph in between the two starts
    if (pcm_prepare(pb->pcm) < 0) {
        fprintf(stderr, "playback PCM prepare error: %s\n", pcm_get_error(pb->pcm));
        cleanup(&actx, settings->user_argv);
        return -1;
    }
    if (pcm_prepare(cap->pcm) < 0) {
        fprintf(stderr, "capture PCM prepare error: %s\n", pcm_get_error(cap->pcm));
        cleanup(&actx, settings->user_argv);
        return -1;
    }

    // CPU-buffer mode on both Codec DMA endpoints, and sync start
    if (cpu_buffer_enable(settings) != 0) {
        fprintf(stderr, "CPU buffer: the Codec DMA endpoints refused CPU-buffer mode or sync start\n");
        cleanup(&actx, settings->user_argv);
        return -1;
    }

    // start the streams, then find and map the two DMAs
    struct cpu_buffer_dma pb_dma = {}, cap_dma = {};
    struct cpu_buffer_stats cpu_stats = {};
    if (start_streams(pb, cap) < 0) {
        cleanup(&actx, settings->user_argv);
        return -1;
    }
    if (cpu_buffer_map_dmas(settings, pb_config, cap_config, &pb_dma, &cap_dma) != 0) {
        cleanup(&actx, settings->user_argv);
        pcm_stop(cap->pcm);
        pcm_stop(pb->pcm);
        return -1;
    }

    // catch ctrl-c to shutdown cleanly
    signal(SIGINT, sig_handler);

    // EVL route: all the Linux calls are done, so switch out-of-band before the loop:
    // the first render() already runs out-of-band. switches back to in-band are
    // counted from here on
    long evl_isw_start = 0;
    if (settings->evl) {
        if (evl_rt_switch_oob() < 0)
            fprintf(stderr, "EVL: could not switch the audio thread out-of-band\n");
        evl_isw_start = evl_rt_inband_switches();
    }

    //------------------------
    // actual audio loop
    cpu_buffer_loop(settings, pb, cap, &actx, &pb_dma, &cap_dma, &cpu_stats);
    //------------------------

    // read before anything below makes Linux calls
    long evl_isw_end = settings->evl ? evl_rt_inband_switches() : 0;

    // user API function
    cleanup(&actx, settings->user_argv);
    // don't call pcm_drain(), it will seg-fault!

    // AE_START_STATUS=1 (environment): how the Codec DMA interfaces started (custom Codec
    // DMA build), read while the streams still run
    const char *status_env = getenv("AE_START_STATUS");
    if (status_env && atoi(status_env) != 0)
        report_hw_ep_start_status(settings);

    // the DMA registers must not be read once the streams stop
    cpu_buffer_unmap(&pb_dma);
    cpu_buffer_unmap(&cap_dma);

    pcm_stop(cap->pcm);
    pcm_stop(pb->pcm);

    if (cpu_stats.late || cpu_stats.skipped)
        fprintf(stderr, "CPU buffer: %u late and %u skipped playback periods (first at block %llu)\n",
                cpu_stats.late, cpu_stats.skipped, (unsigned long long)cpu_stats.first_bad);
    if (g_verbose)
        printf("CPU buffer: %llu blocks\n", (unsigned long long)cpu_stats.blocks);
    if (settings->evl) {
        if (evl_isw_start < 0 || evl_isw_end < 0)
            fprintf(stderr, "EVL: could not read the audio thread's in-band switches\n");
        else
            printf("EVL: audio thread switched to in-band %ld times during the loop\n",
                   evl_isw_end - evl_isw_start);
    }

    return 0;
}

struct audio_thread_arg {
    struct settings *settings;
    struct pcm_ctx *ctx;
    int ret;  // < 0 if the thread could not run the audio loop
};

static void *audio_thread_func(void *arg)
{
    struct audio_thread_arg *a = (struct audio_thread_arg *)arg;

    // EVL route: attach before audio_loop, so that opening and starting the streams
    // happens in-band, before the thread first goes out-of-band. SIGINT is blocked
    // here so that the main thread handles it and this one is not pulled in-band
    if (a->settings->evl) {
        sigset_t sigint;
        sigemptyset(&sigint);
        sigaddset(&sigint, SIGINT);
        pthread_sigmask(SIG_BLOCK, &sigint, nullptr);
        if (evl_rt_attach() < 0) {
            a->ret = -1;
            return nullptr;
        }
    }

    audio_loop(a->settings, a->ctx);

    if (a->settings->evl)
        evl_rt_detach();
    return nullptr;
}

int start_audio(struct settings *settings, struct pcm_ctx ctx[])
{
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;
    struct audio_thread_arg arg = { settings, ctx, 0 };
    int cpu = settings->cpu_affinity;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    set_cpu_affinity_attr(&attr, cpu);  // no-op if cpu == CPU_AFFINITY_UNSET

    if (pthread_create(&thread, &attr, audio_thread_func, &arg) != 0) {
        fprintf(stderr, "RT thread failed, falling back to normal priority\n");

        pthread_attr_t attr_np;
        pthread_attr_init(&attr_np);
        set_cpu_affinity_attr(&attr_np, cpu);  // still honor affinity without SCHED_FIFO

        if (pthread_create(&thread, &attr_np, audio_thread_func, &arg) != 0) {
            fprintf(stderr, "failed to create audio thread\n");
            pthread_attr_destroy(&attr_np);
            pthread_attr_destroy(&attr);
            return -1;
        }
        pthread_attr_destroy(&attr_np);
    }
    pthread_attr_destroy(&attr);
    pthread_join(thread, nullptr);

    return arg.ret;
}

// ---------------------------------------------------------------------------
// entry point
// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    struct settings settings;
    struct pcm_ctx ctx[NUM_DIRS] = {};  // zero-init so cleanup is safe on partial setup

    printf("\nAudioReach Audioengine | project: %s\n\n", PROJECT_NAME);

    init_settings(&settings);

    int rc = parse_cli(argc, argv, &settings);
    if (rc != 0) {
        cleanup_settings(&settings);
        return rc > 0 ? EXIT_SUCCESS : EXIT_FAILURE;  // >0: help shown
    }

    // propagate --verbose to every module that prints
    g_verbose = settings.verbose;
    set_hw_mixer_verbose(settings.verbose);
    set_agm_mixer_verbose(settings.verbose);
    set_cpu_perf_verbose(settings.verbose);

    // EVL route: check for EVL before opening anything, so that --evl fails cleanly
    // on boards without it
    if (settings.evl) {
        if (evl_rt_init() < 0) {
            cleanup_settings(&settings);
            return EXIT_FAILURE;
        }
        g_evl = true;
        if (g_verbose)
            printf("EVL route: the audio thread runs out-of-band, polling every %d us\n",
                   EVL_POLL_SLEEP_US);
    }

    if (validate_cpu_affinity(settings.cpu_affinity) < 0) {
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (resolve_stream_names(&settings) < 0) {
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (apply_backend_formats(&settings) < 0) {
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_ctx(&settings, ctx) < 0) {
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_hw_mixer(MIXER_PATHS, settings.physical_card) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (setup_hw_mixer_paths(&settings) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_agm_mixer(settings.virtual_card) < 0) {
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (set_agm_mixer_graphs(&settings) < 0) {
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_pcm(&settings, ctx) < 0) {
        cleanup_pcm(ctx);
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    struct governor_snapshot gov_snap = {};  // zero-init: restore is a no-op if never forced

    if (settings.force_performance_governor)
        force_performance_governor(&gov_snap);

    int rc_audio = start_audio(&settings, ctx);

    if (settings.force_performance_governor)
        restore_governors(&gov_snap);

    if (rc_audio < 0) {
        cleanup_pcm(ctx);
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    cleanup_pcm(ctx);
    cleanup_agm_mixer();
    cleanup_hw_mixer();
    cleanup_ctx(ctx);
    cleanup_settings(&settings);

    printf("\nAudioReach Audioengine | project: %s exited successfully\n\n", PROJECT_NAME);

    return EXIT_SUCCESS;
}
