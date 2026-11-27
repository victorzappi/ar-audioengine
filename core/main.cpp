/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*ar_audioengine
real-time audioreach-based audio engine

usage:
./ar_audioengine -c 100 -d 100 -k CODEC_DMA-LPAIF_WSA-RX-0 -s 0 -e 0 -w PCM_LL_PLAYBACK -y DEVICEPP_RX_AUDIO_MBDRC -z SPEAKER -i INSTANCE_1

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

// we assume a little-endian CPU: sample conversion copies the host integer's low
// bytes straight to/from the (little-endian) PCM stream via memcpy
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "ar_audioengine assumes a little-endian CPU");

// ---------------------------------------------------------------------------
// types & globals
// ---------------------------------------------------------------------------

// stream directions; used to index the per-direction runtime context array
enum { DIR_PLAYBACK = 0, DIR_CAPTURE = 1, NUM_DIRS = 2 };

// RW mode: periods of silence written to the playback pcm before it starts. with 2,
// the combined capture->playback loop runs at its minimum latency of 2 periods, with
// one period of slack to render and write each block
#define PLAYBACK_PRIME_PERIODS 2

// CPU-buffer route with EVL (--cpu-buffer --evl): the audio thread sleeps this long
// between two reads of the capture DMA's position, out-of-band. it wakes about 1 us late
// on CPU 7 (latmus), so it reads the position about every 2 us
#define EVL_POLL_SLEEP_US 1

struct pcm_ctx {
    struct pcm *pcm;
    unsigned int phys_bytes_per_sample;
    unsigned int bytes_per_sample;
    unsigned int max_value;
    bool is_float;
    unsigned int num_samples;
    float *audio_buffer;
    char *raw_buffer;
};

std::atomic_int should_stop(0);

// gates this file's informational printf() output (set once in main(), right after
// CLI parsing); error/warning fprintf(stderr, ...) output always prints regardless.
// file-scope so sig_handler (fixed OS callback signature) can read it too.
static bool g_verbose = false;

// EVL route (--evl): set once in main(), after EVL has been found. file-scope so
// the CPU-buffer loop can read it
static bool g_evl = false;

// hot-path sample conversion: defined at the very bottom of the file, forward
// declared here because the audio loop (above) calls them
void fromFloatToRaw_int(struct pcm_ctx *ctx, char *raw);
void fromFloatToRaw_float(struct pcm_ctx *ctx, char *raw);
void fromRawToFloat_int(struct pcm_ctx *ctx, const char *raw);
void fromRawToFloat_float(struct pcm_ctx *ctx, const char *raw);

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

// resolve names for every active direction (playback first; capture iff full duplex)
static int resolve_stream_names(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };
    int dirs = settings->full_duplex ? NUM_DIRS : 1;

    for (int d = 0; d < dirs; d++) {
        if (resolve_stream_names_dir(CARDS_CONF_FILE, settings->virtual_card,
                                        settings->physical_card, streams[d]) < 0)
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
    ctx->audio_buffer = nullptr;
    ctx->raw_buffer = nullptr;

    /* prepare configuration to open pcm */
    if (stream->is_float) {
        config->format = PCM_FORMAT_FLOAT_LE;
    }
    else {
        config->format = signed_pcm_bits_to_format(stream->bits);
        if (config->format == -1) {
            fprintf(stderr, "bit count '%u' not supported\n", stream->bits);
            return -1;
        }
    }

    /* prepare context to write to pcm */
    // size in bytes of the format var type used to store sample
    ctx->phys_bytes_per_sample = pcm_format_to_bits(config->format) / 8;
	// different than this, i.e., number of bytes actually used within that format var type!
	if (config->format == PCM_FORMAT_S24_3LE) { // these vars span 32 bits, but only 24 are actually used! -> 3 bytes
		ctx->bytes_per_sample = 3;
    }
    else {
		ctx->bytes_per_sample = ctx->phys_bytes_per_sample;
    }
    if (!stream->is_float)
	    ctx->max_value = (1 << (stream->bits - 1)) - 1; //TODO add min value and improve quantization as in LDSP

    ctx->is_float = is_format_float(config->format);

    ctx->num_samples = config->period_size * config->channels;

    ctx->audio_buffer = (float*)calloc(ctx->num_samples, sizeof(float));
    if ( !(ctx->audio_buffer) ) {
        fprintf(stderr, "unable to allocate %zu bytes\n", ctx->num_samples * sizeof(float));
        return -1;
    }

    ctx->raw_buffer = (char*)calloc(ctx->num_samples, ctx->phys_bytes_per_sample);
    if ( !(ctx->raw_buffer) ) {
        fprintf(stderr, "unable to allocate %u bytes\n", ctx->num_samples * ctx->phys_bytes_per_sample);
        return -1;
    }

    return 0;
}

// build a runtime context for every active direction
static int init_ctx(struct settings *settings, struct pcm_ctx ctx[])
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };
    int dirs = settings->full_duplex ? NUM_DIRS : 1;

    for (int d = 0; d < dirs; d++)
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
        if (ctx[d].raw_buffer != nullptr)
            free(ctx[d].raw_buffer);
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

    // tinyalsa may round the period up to the plugin's minimum (the stock AGM plugin's
    // 96-byte minimum means 48 frames), leaving the buffers out of step with the DSP. the
    // CPU-buffer route never reads or writes the PCM, so there its period does not matter
    const struct pcm_config *open_config = pcm_get_config(ctx->pcm);
    if (!settings->cpu_buffer &&
        (open_config->period_size != stream->config.period_size ||
         open_config->period_count != stream->config.period_count))
        fprintf(stderr, "warning: asked for %u x %u frames, the PCM opened with %u x %u (buffer %u)\n",
                stream->config.period_size, stream->config.period_count,
                open_config->period_size, open_config->period_count, pcm_get_buffer_size(ctx->pcm));

    // CPU-buffer route: the loop moves whole DMA periods, so the DMA block must be the
    // period asked for (-p), whatever period the PCM opened with. set it on the playback
    // and the capture hardware endpoint, before prepare (whole ms on any firmware,
    // anything else needs the custom Codec DMA build)
    if (settings->cpu_buffer &&
        configure_agm_hw_ep_block(stream->frontend_name, stream->backend_name,
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
        printf("  format      %u-bit %s\n",   stream->bits, stream->is_float ? "float" : "signed int");
        printf("  period size %u frames\n",   stream->config.period_size);
        printf("  periods     %u\n\n",        stream->config.period_count);
    }

    return 0;
}

// open a pcm for every active direction
static int init_pcm(struct settings *settings, struct pcm_ctx ctx[])
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };
    int dirs = settings->full_duplex ? NUM_DIRS : 1;

    for (int d = 0; d < dirs; d++)
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

// apply the hardware mixer path for every active direction (set_hw_mixer_path is
// re-entrant on the single shared mixer handle, so this works as-is)
static int setup_hw_mixer_paths(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };
    int dirs = settings->full_duplex ? NUM_DIRS : 1;

    for (int d = 0; d < dirs; d++)
        if (set_hw_mixer_path(streams[d]->mixer_path) < 0)
            return -1;
    return 0;
}

// build the AGM graph for every active direction. The mixer must already be open
// (init_agm_mixer); setup_agm_mixer_graph records each direction's endpoints so
// cleanup_agm_mixer can tear both down.
static int set_agm_mixer_graphs(struct settings *settings)
{
    struct pcm_stream *streams[NUM_DIRS] = { &settings->playback, &settings->capture };
    int dirs = settings->full_duplex ? NUM_DIRS : 1;

    for (int d = 0; d < dirs; d++) {
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
// output. cap is nullptr in playback-only mode (audio_in left null). config: the
// playback stream's
static struct audio_ctx create_audio_ctx(struct pcm_ctx *pb, struct pcm_ctx *cap,
                                         const struct pcm_config *config)
{
    struct audio_ctx actx = {
        .audio_in     = cap ? cap->audio_buffer : nullptr,
        .audio_out    = pb->audio_buffer,
        .period_size  = config->period_size,
        .channels     = config->channels,
        .sample_rate  = config->rate
    };

    return actx;
}

// RW mode: the next playback period is converted into raw_buffer, then written
static int write_playback_period(struct pcm_ctx *ctx, const struct pcm_config *config)
{
    return pcm_writei(ctx->pcm, ctx->raw_buffer, config->period_size);
}

// RW mode: the next captured period, read into raw_buffer. nullptr on error
static const char *read_capture_period(struct pcm_ctx *ctx, const struct pcm_config *config)
{
    if (pcm_readi(ctx->pcm, ctx->raw_buffer, config->period_size) < 0)
        return nullptr;
    return ctx->raw_buffer;
}

// start the streams back to back, playback first, and enable the echo reference if
// asked. any delay before the capture start lowers latency and playback slack by the
// same amount. < 0 on error (playback is stopped again if capture fails to start)
static int start_streams(struct settings *settings, struct pcm_ctx *pb, struct pcm_ctx *cap)
{
    if (pcm_start(pb->pcm) < 0) {
        fprintf(stderr, "playback PCM start error: %s (errno=%d)\n", pcm_get_error(pb->pcm), errno);
        return -1;
    }
    if (cap && pcm_start(cap->pcm) < 0) {
        fprintf(stderr, "capture PCM start error: %s (errno=%d)\n", pcm_get_error(cap->pcm), errno);
        pcm_stop(pb->pcm);
        return -1;
    }

    if (cap && settings->echo_reference) {
        if (g_verbose)
            printf("Enabling echo reference path from playback to capture\n");
        if (set_agm_ecref_path(settings->capture.frontend_name, settings->playback.backend_name, true)) {
            if (g_verbose)
                printf("Could not enable echo reference path\n");
        }
    }
    return 0;
}

static void print_hw_ep_start_status(const char *name, const struct hw_ep_start_status *st)
{
    printf("start status: %-8s group %u, interface enabled at %llu us", name, st->sync_group_id,
           (unsigned long long)st->intf_enable_ts_us);
    if (st->sync_group_id)
        printf(" (group writes took %u us)", st->group_enable_spread_us);
    if (st->first_intr_ts_valid)
        printf(", first DMA interrupt at %llu us", (unsigned long long)st->first_intr_ts_us);
    else
        printf(", first DMA interrupt not timestamped");
    printf(", FIFO errors %u, bus errors %u\n", st->fifo_err_cnt, st->bus_err_cnt);
}

// prints the last start of the Codec DMA endpoints (custom Codec DMA build), and with
// capture how far apart the two interfaces started
static void report_hw_ep_start_status(struct settings *settings, bool full_duplex)
{
    struct hw_ep_start_status pb_st = {}, cap_st = {};

    if (get_agm_hw_ep_start_status(settings->playback.frontend_name, settings->playback.backend_name,
                                   false, &pb_st) != 0)
        return;
    print_hw_ep_start_status("playback", &pb_st);
    if (!full_duplex)
        return;
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
// CPU-buffer route (--cpu-buffer)
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

// before the start: CPU-buffer mode on both endpoints, and both DMAs start together
// (the loop relies on their periods lining up). < 0 on error (printed)
static int cpu_buffer_enable(struct settings *settings)
{
    if (set_agm_hw_ep_cpu_buffer(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, true, 0) != 0 ||
        set_agm_hw_ep_cpu_buffer(settings->capture.frontend_name, settings->capture.backend_name,
                                 true, true, 0) != 0)
        return -1;
    if (set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, 1, 2) != 0)
        return -1;
    if (set_agm_hw_ep_sync_start(settings->capture.frontend_name, settings->capture.backend_name,
                                 true, 1, 2) != 0) {
        // don't leave playback waiting for a capture endpoint that never joins
        set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                 false, 0, 0);
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

        // the playback period after the one the DMA reads now
        unsigned int reading = cpu_buffer_period(pb_dma);
        unsigned int next = (reading + 1) % pb_dma->num_periods;
        if (last_written >= 0 && next != (unsigned int)(last_written + 1) % pb_dma->num_periods) {
            if (stats->late + stats->skipped == 0)
                stats->first_bad = stats->blocks;
            stats->skipped++;
        }
        cpu_buffer_write(pb_dma, next, pb->audio_buffer);
        memset(pb->audio_buffer, 0, pb->num_samples * sizeof(float));
        cpu_buffer_write_barrier();
        // still reading the period before: the block was in time
        if (cpu_buffer_period(pb_dma) != reading) {
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
    int ret = 0;
    struct pcm_ctx *pb = &ctx[DIR_PLAYBACK];
    struct pcm_ctx *cap = settings->full_duplex ? &ctx[DIR_CAPTURE] : nullptr;

    // the CPU-buffer route never reads or writes the PCMs: its periods are the ones asked
    // for (-p), whatever period the PCMs opened with
    const struct pcm_config *pb_config = settings->cpu_buffer ? &settings->playback.config
                                                              : pcm_get_config(pb->pcm);
    if (pb_config == nullptr) {
        fprintf(stderr, "unable to get playback pcm config\n");
        return -1;
    }
    const struct pcm_config *cap_config = nullptr;
    if (cap)
        cap_config = settings->cpu_buffer ? &settings->capture.config : pcm_get_config(cap->pcm);
    if (cap && cap_config == nullptr) {
        fprintf(stderr, "unable to get capture pcm config\n");
        return -1;
    }
    if (cap && cap_config->period_size != pb_config->period_size)
        fprintf(stderr, "warning: capture/playback period sizes differ (%u vs %u); "
                        "the combined loop assumes they match\n",
                cap_config->period_size, pb_config->period_size);
    if (!settings->cpu_buffer && pb_config->period_count < PLAYBACK_PRIME_PERIODS) {
        fprintf(stderr, "playback period count %u too small, the pcm is primed with %d periods\n",
                pb_config->period_count, PLAYBACK_PRIME_PERIODS);
        return -1;
    }

    struct audio_ctx actx = create_audio_ctx(pb, cap, pb_config);

    // user API function
    if (setup(&actx, settings->user_argv)) {
        fprintf(stderr, "setup function failed\n");
        cleanup(&actx, settings->user_argv);
        pcm_stop(pb->pcm);
        if (cap) pcm_stop(cap->pcm);
        return -2;
    }

    // prepare both streams before starting either, so that pcm_start() does not
    // prepare the capture graph in between the two starts
    if (pcm_prepare(pb->pcm) < 0) {
        fprintf(stderr, "playback PCM prepare error: %s\n", pcm_get_error(pb->pcm));
        cleanup(&actx, settings->user_argv);
        return -1;
    }
    if (cap && pcm_prepare(cap->pcm) < 0) {
        fprintf(stderr, "capture PCM prepare error: %s\n", pcm_get_error(cap->pcm));
        cleanup(&actx, settings->user_argv);
        return -1;
    }

    // RW mode: prime playback with silence, so the DSP has data from its first frame on.
    // the CPU-buffer route writes the DMA buffer itself, which is silent at the start
    for (int i = 0; !settings->cpu_buffer && i < PLAYBACK_PRIME_PERIODS; i++) {
        memset(pb->raw_buffer, 0, pb->num_samples * pb->phys_bytes_per_sample);
        if (write_playback_period(pb, pb_config) < 0) {
            fprintf(stderr, "playback PCM priming error: %s\n", pcm_get_error(pb->pcm));
            cleanup(&actx, settings->user_argv);
            return -1;
        }
    }

    // AE_SYNC_START=1 (environment): start the two Codec DMA interfaces together (custom
    // Codec DMA build). both endpoints join sync-start group 1 for this start only, and
    // the DSP enables both interfaces when the second one starts
    // the CPU-buffer route always starts them together
    const char *sync_env = getenv("AE_SYNC_START");
    if (settings->cpu_buffer) {
        if (cpu_buffer_enable(settings) != 0) {
            fprintf(stderr, "CPU buffer: the Codec DMA endpoints refused CPU-buffer mode or sync start\n");
            cleanup(&actx, settings->user_argv);
            return -1;
        }
    }
    else if (cap && sync_env && atoi(sync_env) != 0) {
        if (set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                     false, 1, 2) != 0) {
            fprintf(stderr, "sync start: off, the streams start one after the other\n");
        }
        else if (set_agm_hw_ep_sync_start(settings->capture.frontend_name, settings->capture.backend_name,
                                          true, 1, 2) != 0) {
            // don't leave playback waiting for a capture endpoint that never joins
            set_agm_hw_ep_sync_start(settings->playback.frontend_name, settings->playback.backend_name,
                                     false, 0, 0);
            fprintf(stderr, "sync start: off, the streams start one after the other\n");
        }
    }

    // start the streams; the CPU-buffer route then finds and maps the two DMAs
    struct cpu_buffer_dma pb_dma = {}, cap_dma = {};
    struct cpu_buffer_stats cpu_stats = {};
    if (start_streams(settings, pb, cap) < 0) {
        cleanup(&actx, settings->user_argv);
        return -1;
    }
    if (settings->cpu_buffer &&
        cpu_buffer_map_dmas(settings, pb_config, cap_config, &pb_dma, &cap_dma) != 0) {
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
    if (settings->cpu_buffer) {
        cpu_buffer_loop(settings, pb, cap, &actx, &pb_dma, &cap_dma, &cpu_stats);
    }
    else {
        // RW mode: capture read -> render -> playback write, in lockstep. in playback-only
        // mode the capture half is skipped
        while (!should_stop.load()) {
            if (cap) {
                const char *in = read_capture_period(cap, cap_config);
                if (in == nullptr) {
                    if (should_stop.load())
                        break;
                    fprintf(stderr, "error capturing sample. %s\n", pcm_get_error(cap->pcm));
                    ret = -3;
                    break;
                }
                if (!cap->is_float)
                    fromRawToFloat_int(cap, in);
                else
                    fromRawToFloat_float(cap, in);
            }

            // user API function
            render(&actx, settings->user_argv);

            if (!pb->is_float)
                fromFloatToRaw_int(pb, pb->raw_buffer);
            else
                fromFloatToRaw_float(pb, pb->raw_buffer);
            if (write_playback_period(pb, pb_config) < 0) {
                if (should_stop.load())
                    break;
                fprintf(stderr, "error playing sample. %s\n", pcm_get_error(pb->pcm));
                ret = -3;
                break;
            }
        }
    }
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
        report_hw_ep_start_status(settings, cap != nullptr);

    // the DMA registers must not be read once the streams stop
    if (settings->cpu_buffer) {
        cpu_buffer_unmap(&pb_dma);
        cpu_buffer_unmap(&cap_dma);
    }

    if (cap) {
        if (settings->echo_reference)
            set_agm_ecref_path(settings->capture.frontend_name, settings->playback.backend_name, false);
        pcm_stop(cap->pcm);
    }
    pcm_stop(pb->pcm);

    if (settings->cpu_buffer) {
        if (cpu_stats.late || cpu_stats.skipped)
            fprintf(stderr, "CPU buffer: %u late and %u skipped playback periods (first at block %llu)\n",
                    cpu_stats.late, cpu_stats.skipped, (unsigned long long)cpu_stats.first_bad);
        if (g_verbose)
            printf("CPU buffer: %llu blocks\n", (unsigned long long)cpu_stats.blocks);
    }
    if (settings->evl) {
        if (evl_isw_start < 0 || evl_isw_end < 0)
            fprintf(stderr, "EVL: could not read the audio thread's in-band switches\n");
        else
            printf("EVL: audio thread switched to in-band %ld times during the loop\n",
                   evl_isw_end - evl_isw_start);
    }

    return ret;
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

// ---------------------------------------------------------------------------
// sample-format conversion (hot path)
// ---------------------------------------------------------------------------

// float -> signed integer samples of type T, scaled to full scale with one extra step
// on the negative side (asymmetric signed integer quantization). a plain typed loop,
// so the compiler can vectorize it. out-of-range values wrap as before, through int32
template <typename T>
static void float_to_int(const float *in, T *out, unsigned int n, float max_value)
{
    // negatives scale by max_value + 1. adding the comparison as a number, not
    // selecting with ?:, keeps the loop branch-free so GCC vectorizes it
    for (unsigned int i = 0; i < n; i++)
        out[i] = (T)(int32_t)(in[i] * (max_value + (float)(in[i] < 0.0f)));
}

// inverse of float_to_int: signed integer samples of type T -> normalized float
template <typename T>
static void int_to_float(const T *in, float *out, unsigned int n, float max_value)
{
    for (unsigned int i = 0; i < n; i++)
        out[i] = (float)in[i] / max_value;
}

// audio_buffer -> integer samples in raw (raw_buffer)
void fromFloatToRaw_int(struct pcm_ctx *ctx, char *raw)
{
    const float max_value = (float)ctx->max_value;

    switch (ctx->bytes_per_sample) {
    case 1:
        float_to_int(ctx->audio_buffer, (int8_t *)raw, ctx->num_samples, max_value);
        break;
    case 2:
        float_to_int(ctx->audio_buffer, (int16_t *)raw, ctx->num_samples, max_value);
        break;
    case 4:
        float_to_int(ctx->audio_buffer, (int32_t *)raw, ctx->num_samples, max_value);
        break;
    default: {
        // packed formats (S24_3LE): byte by byte. CPU and PCM stream are both
        // little-endian (see static_assert), so the low bytes_per_sample bytes of
        // res are already the output bytes
        unsigned char *sampleBytes = (unsigned char *)raw;
        for (unsigned int n = 0; n < ctx->num_samples; n++) {
            unsigned int asymmMaxVal = ctx->max_value + (ctx->audio_buffer[n] < 0);
            int res = asymmMaxVal * ctx->audio_buffer[n];
            memcpy(sampleBytes, &res, ctx->bytes_per_sample);
            sampleBytes += ctx->bytes_per_sample;
        }
        break;
    }
    }
    // clean up buffer for next period
    memset(ctx->audio_buffer, 0, ctx->num_samples*sizeof(float));
}

void fromFloatToRaw_float(struct pcm_ctx *ctx, char *raw)
{
    memcpy(raw, ctx->audio_buffer, ctx->num_samples * sizeof(float));
    // clean up buffer for next period
    memset(ctx->audio_buffer, 0, ctx->num_samples*sizeof(float));
}

// inverse of fromFloatToRaw_int: integer samples in raw -> normalized float
void fromRawToFloat_int(struct pcm_ctx *ctx, const char *raw)
{
    const float max_value = (float)ctx->max_value;

    switch (ctx->bytes_per_sample) {
    case 1:
        int_to_float((const int8_t *)raw, ctx->audio_buffer, ctx->num_samples, max_value);
        break;
    case 2:
        int_to_float((const int16_t *)raw, ctx->audio_buffer, ctx->num_samples, max_value);
        break;
    case 4:
        int_to_float((const int32_t *)raw, ctx->audio_buffer, ctx->num_samples, max_value);
        break;
    default: {
        // packed formats (S24_3LE): the sample bytes land in the low end of res
        // (little-endian, see static_assert), then the arithmetic shift sign-extends
        const unsigned char *sampleBytes = (const unsigned char *)raw;
        const int shift = (int)(sizeof(int) - ctx->bytes_per_sample) * 8;
        for (unsigned int n = 0; n < ctx->num_samples; n++) {
            int res = 0;
            memcpy(&res, sampleBytes, ctx->bytes_per_sample);
            res = (res << shift) >> shift;
            ctx->audio_buffer[n] = (float)res / max_value;
            sampleBytes += ctx->bytes_per_sample;
        }
        break;
    }
    }
}

// inverse of fromFloatToRaw_float
void fromRawToFloat_float(struct pcm_ctx *ctx, const char *raw)
{
    memcpy(ctx->audio_buffer, raw, ctx->num_samples * sizeof(float));
}
