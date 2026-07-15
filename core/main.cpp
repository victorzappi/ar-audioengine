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
// _QNN:
// __improve doc
// _rename audio_ctx and in all projects + align code style across projects: 
// __audio_buffer -> audio_out
// __input_buffer -> audio_in
// _re-introduce per-direction hardware endpoint / MFC configuration (the old
//  configure_agm_modules, RX-only) for both RX and TX, if needed for clean audio

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <atomic>

#include "cli.h"
#include "pcm_utils.h"
#include "hw_mixer.h"
#include "agm_mixer.h"
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

// hot-path sample conversion: defined at the very bottom of the file, forward
// declared here because the audio loop (above) calls them
void fromFloatToRaw_int(struct pcm_ctx *ctx);
void fromFloatToRaw_float(struct pcm_ctx *ctx);
void fromRawToFloat_int(struct pcm_ctx *ctx);
void fromRawToFloat_float(struct pcm_ctx *ctx);

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
            fprintf(stderr, "Frontend not found for virtual card %u device %u in \"%s\"\n",
                    virtual_card, stream->virtual_device, cards_xml_path);
            return -1;
        }
    }
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

    printf("\nPCM (frontend) config:\n");
    printf("  direction   %s\n",          (stream->flags & PCM_IN) ? "capture" : "playback");
    printf("  rate        %u Hz\n",       stream->config.rate);
    printf("  channels    %u\n",          stream->config.channels);
    printf("  format      %u-bit %s\n",   stream->bits, stream->is_float ? "float" : "signed int");
    printf("  period size %u frames\n",   stream->config.period_size);
    printf("  periods     %u\n\n",        stream->config.period_count);

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
    printf("\nStopping PCM stream...\n");
    stream_close();

}

// ---------------------------------------------------------------------------
// audio thread & render loop
// ---------------------------------------------------------------------------

// build the render context: capture samples are the input, playback samples the
// output. cap is nullptr in playback-only mode (input_buffer left null).
static struct audio_ctx create_audio_ctx(struct pcm_ctx *pb, struct pcm_ctx *cap)
{
    const struct pcm_config *config = pcm_get_config(pb->pcm);

    struct audio_ctx actx = {
        .input_buffer = cap ? cap->audio_buffer : nullptr,
        .audio_buffer = pb->audio_buffer,
        .period_size  = config->period_size,
        .channels     = config->channels,
        .sample_rate  = config->rate
    };

    return actx;
}

// single combined real-time loop: capture read -> render -> playback write,
// in lockstep. In playback-only mode the capture half is skipped.
int audio_loop(struct settings *settings, struct pcm_ctx ctx[])
{
    int ret = 0;
    struct pcm_ctx *pb = &ctx[DIR_PLAYBACK];
    struct pcm_ctx *cap = settings->full_duplex ? &ctx[DIR_CAPTURE] : nullptr;

    const struct pcm_config *pb_config = pcm_get_config(pb->pcm);
    if (pb_config == nullptr) {
        fprintf(stderr, "unable to get playback pcm config\n");
        return -1;
    }
    const struct pcm_config *cap_config = cap ? pcm_get_config(cap->pcm) : nullptr;
    if (cap && cap_config == nullptr) {
        fprintf(stderr, "unable to get capture pcm config\n");
        return -1;
    }
    if (cap && cap_config->period_size != pb_config->period_size)
        fprintf(stderr, "warning: capture/playback period sizes differ (%u vs %u); "
                        "the combined loop assumes they match\n",
                cap_config->period_size, pb_config->period_size);

    struct audio_ctx actx = create_audio_ctx(pb, cap);

    // user API function
    if (setup(&actx, settings->user_argv)) {
        fprintf(stderr, "setup function failed\n");
        cleanup(&actx, settings->user_argv);
        pcm_stop(pb->pcm);
        if (cap) pcm_stop(cap->pcm);
        return -2;
    }

    // start streams
    if (cap && pcm_start(cap->pcm) < 0) {
        fprintf(stderr, "capture PCM start error: %s (errno=%d)\n", pcm_get_error(cap->pcm), errno);
        pcm_stop(pb->pcm);
        return -1;
    }
    if (pcm_start(pb->pcm) < 0) {
        fprintf(stderr, "playback PCM start error: %s (errno=%d)\n", pcm_get_error(cap->pcm), errno);
        return -1;
    }

    // catch ctrl-c to shutdown cleanly
    signal(SIGINT, sig_handler);

    //------------------------
    // actual audio loop
    while (!should_stop.load()) {
        if (cap) {
            int read_frames = pcm_readi(cap->pcm, cap->raw_buffer, cap_config->period_size);
            if (read_frames < 0) {
                fprintf(stderr, "error capturing sample. %s\n", pcm_get_error(cap->pcm));
                ret = -3;
                break;
            }
            if (!cap->is_float)
                fromRawToFloat_int(cap);
            else
                fromRawToFloat_float(cap);
        }

        // user API function
        render(&actx, settings->user_argv);

        if (!pb->is_float)
            fromFloatToRaw_int(pb);
        else
            fromFloatToRaw_float(pb);

        int written_frames = pcm_writei(pb->pcm, pb->raw_buffer, pb_config->period_size);
        if (written_frames < 0) {
            fprintf(stderr, "error playing sample. %s\n", pcm_get_error(pb->pcm));
            ret = -3;
            break;
        }
    }
    //------------------------

    // user API function
    cleanup(&actx, settings->user_argv);
    // don't call pcm_drain(), it will seg-fault!
    pcm_stop(pb->pcm);
    if (cap) pcm_stop(cap->pcm);

    return ret;
}

struct audio_thread_arg {
    struct settings *settings;
    struct pcm_ctx *ctx;
};

static void *audio_thread_func(void *arg)
{
    struct audio_thread_arg *a = (struct audio_thread_arg *)arg;
    audio_loop(a->settings, a->ctx);
    return nullptr;
}

int start_audio(struct settings *settings, struct pcm_ctx ctx[])
{
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;
    struct audio_thread_arg arg = { settings, ctx };

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    if (pthread_create(&thread, &attr, audio_thread_func, &arg) != 0) {
        fprintf(stderr, "RT thread failed, falling back to normal priority\n");
        if (pthread_create(&thread, nullptr, audio_thread_func, &arg) != 0) {
            fprintf(stderr, "failed to create audio thread\n");
            pthread_attr_destroy(&attr);
            return -1;
        }
    }
    pthread_attr_destroy(&attr);
    pthread_join(thread, nullptr);

    return 0;
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

    if (start_audio(&settings, ctx) < 0) {
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

    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// sample-format conversion (hot path)
// ---------------------------------------------------------------------------

void fromFloatToRaw_int(struct pcm_ctx *ctx)
{
    unsigned char *sampleBytes = (unsigned char *)ctx->raw_buffer;
    unsigned int asymmMaxVal;
    for (unsigned int n=0; n<ctx->num_samples; n++) {
        asymmMaxVal = ctx->max_value + (ctx->audio_buffer[n] < 0); // this takes care of asymmetric signed integer quantization [negative values have one extra step]
        int res = asymmMaxVal * ctx->audio_buffer[n]; // get actual int sample out of normalized full scale float

        // CPU and PCM stream are both little-endian (see static_assert), so the
        // low bytes_per_sample bytes of res are already the output bytes
        memcpy(sampleBytes, &res, ctx->bytes_per_sample);

        sampleBytes += ctx->bytes_per_sample; // jump to next sample
    }
    // clean up buffer for next period
    memset(ctx->audio_buffer, 0, ctx->num_samples*sizeof(float));
}

void fromFloatToRaw_float(struct pcm_ctx *ctx)
{
    union {
        float f;
        int i;
    } fval;

    unsigned char *sampleBytes = (unsigned char *)ctx->raw_buffer;

    for (unsigned int n=0; n<ctx->num_samples; n++)
    {
        fval.f = ctx->audio_buffer[n]; // safe, cos float is at least 32 bits
        int res = fval.i; // interpret as int

        // CPU and PCM stream are both little-endian (see static_assert), so the
        // low bytes_per_sample bytes of res are already the output bytes
        memcpy(sampleBytes, &res, ctx->bytes_per_sample);

        sampleBytes += ctx->bytes_per_sample; // jump to next sample
    }
    // clean up buffer for next period
    memset(ctx->audio_buffer, 0, ctx->num_samples*sizeof(float));
}

// inverse of fromFloatToRaw_int: raw little-endian integer samples -> normalized float
void fromRawToFloat_int(struct pcm_ctx *ctx)
{
    unsigned char *sampleBytes = (unsigned char *)ctx->raw_buffer;
    const int shift = (int)(sizeof(int) - ctx->bytes_per_sample) * 8; // for sign extension
    for (unsigned int n=0; n<ctx->num_samples; n++) {
        int res = 0;
        // CPU and PCM stream are both little-endian (see static_assert): the sample
        // bytes land in the low end of res, then the arithmetic shift sign-extends
        memcpy(&res, sampleBytes, ctx->bytes_per_sample);
        res = (res << shift) >> shift;
        ctx->audio_buffer[n] = (float)res / (float)ctx->max_value; // normalize to [-1, 1]

        sampleBytes += ctx->bytes_per_sample; // jump to next sample
    }
}

// inverse of fromFloatToRaw_float: raw little-endian float samples -> float
void fromRawToFloat_float(struct pcm_ctx *ctx)
{
    union {
        float f;
        int i;
    } fval;

    unsigned char *sampleBytes = (unsigned char *)ctx->raw_buffer;

    for (unsigned int n=0; n<ctx->num_samples; n++) {
        int res = 0;
        memcpy(&res, sampleBytes, ctx->bytes_per_sample); // 4 bytes for float
        fval.i = res;
        ctx->audio_buffer[n] = fval.f;

        sampleBytes += ctx->bytes_per_sample; // jump to next sample
    }
}
