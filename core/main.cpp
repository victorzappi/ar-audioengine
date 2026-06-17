/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*ar_audioengine
real-time audioreach-based audio engine

usage:
./ar_audioengine -c 100 -d 100 -B CODEC_DMA-LPAIF_WSA-RX-0 -C 0 -D 0 -x PCM_LL_PLAYBACK -w DEVICEPP_RX_AUDIO_MBDRC -z SPEAKER -i INSTANCE_1

configure, build and clean commands in CMakeLists.txt
*/

// next steps:
// _make cmd line format preset selection instead of bits

#include <signal.h>
//#include <stdbool.h> //VIC needed only in C
//#include <stdint.h> //VIC needed only in C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <atomic>

#include "pcm_utils.h"
#include "hw_mixer.h"
#include "agm_mixer.h"
#include "audioreach_mappings.h"
#include "render.h"

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

#define DEFAULT_PLAYBACK_MIXER_PATH "speaker"

// ---------------------------------------------------------------------------
// types & globals
// ---------------------------------------------------------------------------

// one audio direction (playback or capture): device, routing names, mixer
// path, graph keys, and pcm config. params shared across both directions live
// in struct settings. CLI flags noted are the playback ones; capture gets its
// own flags when it is wired in.
struct pcm_stream {
    unsigned int virtual_device;   // CLI -d
    unsigned int physical_device;  // CLI -D
    char *frontend_name;           // CLI -F; else auto from (virtual_card, virtual_device)
    char *backend_name;            // CLI -B; else auto from (physical_card, physical_device, dir)
    char *mixer_path;              // CLI -o (hardware mixer path)
    int flags;                     // PCM_OUT / PCM_IN -- also encodes direction

    struct pcm_config config;      // CLI -p/-q/-n/-r; shared params + this stream's channels

    // graph params: CLI args set the kv *values* (-w/-i/-x/-y/-z), the keys are fixed
    struct agm_key_value stream_kv;
    struct agm_key_value instance_kv;
    struct agm_key_value streampp_kv;
    struct agm_key_value devicepp_kv;
    struct agm_key_value device_kv;
};

// engine configuration. shared fields are command-line args (the flag that sets
// each is noted); per-direction fields live in the pcm_stream(s).
struct settings {
    // shared across both directions
    unsigned int virtual_card;     // CLI -c
    unsigned int physical_card;    // CLI -C
    unsigned int bits;             // CLI -b
    bool is_float;                 // CLI -f
    unsigned int frame_size_fcr;   // CLI -s
    bool full_duplex;              // global toggle; gates capture (wired with capture step)

    struct pcm_stream playback;    // PCM_OUT
};

struct pcm_ctx {
    struct pcm *pcm;
    unsigned int phys_bytes_per_sample;
    unsigned int bytes_per_sample;
    unsigned int max_value;
    bool is_big_endian;
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
void byteSplit_littleEndian(struct pcm_ctx *ctx, unsigned char* sampleBytes, int value);
void byteSplit_bigEndian(struct pcm_ctx *ctx, unsigned char *sampleBytes, int value);

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------

// set the defaults; CLI args overwrite the relevant fields in the optparse loop
void init_settings(struct settings *settings)
{
    // shared across both directions
    settings->virtual_card = 100;
    settings->physical_card = 0;
    settings->bits = 16;
    settings->is_float = false;
    settings->frame_size_fcr = 1;
    settings->full_duplex = false;

    // playback stream
    struct pcm_stream *playback = &settings->playback;

    playback->virtual_device = 100;
    playback->physical_device = 0;
    playback->frontend_name = nullptr;
    playback->backend_name = nullptr;
    playback->mixer_path = strdup(DEFAULT_PLAYBACK_MIXER_PATH);
    playback->flags = PCM_OUT;

    playback->config.period_size = 960;
    playback->config.period_count = 4;
    playback->config.channels = 2;
    playback->config.rate = 48000;
    playback->config.format = PCM_FORMAT_INVALID;  // derived from bits/is_float in init_ctx

    // these can be left to default, because ARE does not support aumtomatic pcm start/stop
    playback->config.silence_threshold = 0;
    playback->config.silence_size = 0;
    playback->config.stop_threshold = 0;
    playback->config.start_threshold = 0;

    // we set these to load the graph for the default RB3 use case
    playback->stream_kv.key     = STREAMRX;
    playback->stream_kv.value   = PCM_LL_PLAYBACK;

    playback->streampp_kv.key   = STREAMPP_RX;
    playback->streampp_kv.value = 0;

    playback->instance_kv.key   = INSTANCE;
    playback->instance_kv.value = INSTANCE_1;

    playback->devicepp_kv.key   = DEVICEPP_RX;
    playback->devicepp_kv.value = DEVICEPP_RX_AUDIO_MBDRC;

    playback->device_kv.key     = DEVICERX;
    playback->device_kv.value   = SPEAKER;
}

void cleanup_settings(struct settings *settings)
{
    if (settings->playback.frontend_name != nullptr)
        free(settings->playback.frontend_name);
    if (settings->playback.backend_name != nullptr)
        free(settings->playback.backend_name);
    if (settings->playback.mixer_path != nullptr)
        free(settings->playback.mixer_path);
}

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
                             char **backend_name_out)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/asound/card%u/pcm%up/info", physical_card, physical_device);

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

static int resolve_stream_names(const char *cards_xml_path, unsigned int virtual_card,
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
        if (set_backend_name(physical_card, stream->physical_device, &stream->backend_name) != 0) {
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

// ---------------------------------------------------------------------------
// pcm context & stream lifecycle
// ---------------------------------------------------------------------------

static int init_ctx(struct pcm_ctx* ctx, struct settings *settings, struct pcm_stream *stream)
{
    struct pcm_config *config = &stream->config;

    ctx->pcm = nullptr;
    ctx->audio_buffer = nullptr;
    ctx->raw_buffer = nullptr;

    /* prepare configuration to open pcm */
    if (settings->is_float) {
        config->format = PCM_FORMAT_FLOAT_LE;
    }
    else {
        config->format = signed_pcm_bits_to_format(settings->bits);
        if (config->format == -1) {
            fprintf(stderr, "bit count '%u' not supported\n", settings->bits);
            return -1;
        }
    }

    /* prepare context to write to pcm */
    // size in bytes of the format var type used to store sample
    ctx->phys_bytes_per_sample = pcm_format_to_bits(config->format) / 8;
	// different than this, i.e., number of bytes actually used within that format var type!
	if (config->format == PCM_FORMAT_S24_3LE || config->format == PCM_FORMAT_S24_3BE) { // these vars span 32 bits, but only 24 are actually used! -> 3 bytes
		ctx->bytes_per_sample = 3;
    }
    else {
		ctx->bytes_per_sample = ctx->phys_bytes_per_sample;
    }
    if (!settings->is_float)
	    ctx->max_value = (1 << (settings->bits - 1)) - 1; //TODO add min value and improve quantization as in LDSP

    ctx->is_big_endian = is_format_big_endian(config->format);
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

void cleanup_ctx(struct pcm_ctx *ctx)
{
    if (ctx == nullptr) {
        return;
    }
    if (ctx->audio_buffer != nullptr) {
        free(ctx->audio_buffer);
    }
    if (ctx->raw_buffer != nullptr) {
        free(ctx->raw_buffer);
    }
}

static int init_pcm(struct pcm_ctx* ctx, struct settings *settings, struct pcm_stream *stream)
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
        return -1;
    }

    printf("\nPCM (frontend) config:\n");
    printf("  rate        %u Hz\n",     stream->config.rate);
    printf("  channels    %u\n",        stream->config.channels);
    printf("  format      %u-bit %s\n", settings->bits, settings->is_float ? "float" : "signed int");
    printf("  period size %u frames\n", stream->config.period_size);
    printf("  periods     %u\n\n",      stream->config.period_count);

    return 0;
}

void cleanup_pcm(struct pcm *pcm)
{
    printf("pcm_cleanup\n");
    if (pcm != nullptr) {
        pcm_close(pcm);
    }
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

// Function to create audio context from PCM context
struct audio_ctx create_audio_ctx(struct pcm_ctx *ctx) {
    const struct pcm_config *config = pcm_get_config(ctx->pcm);

    struct audio_ctx actx = {
        .audio_buffer = ctx->audio_buffer,
        .period_size = config->period_size,
        .channels = config->channels,
        .sample_rate = config->rate
    };

    return actx;
}

int audio_loop(struct pcm_ctx *ctx)
{
    const struct pcm_config *config;
    int ret = 0;
    int written_frames;

    config = pcm_get_config(ctx->pcm);
    if (config == nullptr) {
        fprintf(stderr, "unable to get pcm config\n");
        return -1;
    }

    struct audio_ctx actx = create_audio_ctx(ctx);  // Must declare AND initialize together

    if (pcm_start(ctx->pcm) < 0) {
        fprintf(stderr, "PCM start error\n");
        return -1;
    }

    if (setup(&actx, nullptr)) {
        fprintf(stderr, "setup function failed\n");
        cleanup(&actx, nullptr);
        return -2;
    }

    // catch ctrl-c to shutdown cleanly
    signal(SIGINT, sig_handler);

    //------------------------
    // actual audio loop
    while (!should_stop.load()) {
        render(&actx, nullptr);

        if (!ctx->is_float) {
            fromFloatToRaw_int(ctx);
        }
        else {
            fromFloatToRaw_float(ctx);
        }

        written_frames = pcm_writei(ctx->pcm, ctx->raw_buffer, config->period_size);
        if (written_frames < 0) {
            fprintf(stderr, "error playing sample. %s\n", pcm_get_error(ctx->pcm));
            ret = -3;
            break;
        }
    }
    //------------------------

    cleanup(&actx, nullptr);
    // don't call pcm_drain(ctx->pcm), it will seg-fault!
    pcm_stop(ctx->pcm);

    return ret;
}

static void *audio_thread_func(void *arg)
{
    struct pcm_ctx *ctx = (struct pcm_ctx *)arg;
    audio_loop(ctx);
    return nullptr;
}

int start_audio(struct pcm_ctx *ctx)
{
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;

    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    if (pthread_create(&thread, &attr, audio_thread_func, ctx) != 0) {
        fprintf(stderr, "RT thread failed, falling back to normal priority\n");
        if (pthread_create(&thread, nullptr, audio_thread_func, ctx) != 0) {
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
// cli & entry point
// ---------------------------------------------------------------------------

void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [options]\n", argv0);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "-c | --virtual-card <card num>         The virtual card number that contains the frontend device\n");
    fprintf(stderr, "-d | --virtual-device <device num>     The virtual device number that represents the frontend\n");
    fprintf(stderr, "-C | --physical-card <card num>        The physical card number that contains the backend device\n");
    fprintf(stderr, "-D | --physical-device <device num>    The physical device number that represents the backend\n");
    fprintf(stderr, "-F | --frontend-name <name>            The frontend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-B | --backend-name <name>             The backend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-p | --period-size <size>              The size of the frontend PCM period\n");
    fprintf(stderr, "-q | --period-count <count>            The number of frontend PCM periods\n");
    fprintf(stderr, "-n | --channels <count>                The number of channels\n");
    fprintf(stderr, "-r | --rate <rate>                     The audio sample rate\n");
    fprintf(stderr, "-b | --bits <bit-count>                The number of bits in one sample\n");
    fprintf(stderr, "-f | --float                           The samples are in floating-point PCM\n");
    fprintf(stderr, "-s | --framesize-factor <factor>       The factor that determines the size of the backend period, as 48 samples x factor\n");
    fprintf(stderr, "-w | --stream <value>                  The stream key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-x | --streampp <value>                The streampp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-y | --devicepp <value>                The devicepp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-z | --device <value>                  The device key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-i | --instance <value>                The intance key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-o | --playback-path <path>            The hardware mixer playback path\n");
}

int main(int argc, char **argv)
{
    int c;
    struct settings settings;
    struct pcm_ctx ctx;

    struct optparse opts;
    struct optparse_long long_options[] = {
        { "virtual-card",       'c', OPTPARSE_REQUIRED },
        { "virtual-device",     'd', OPTPARSE_REQUIRED },
        { "physical-card",      'C', OPTPARSE_REQUIRED },
        { "physical-device",    'D', OPTPARSE_REQUIRED },
        { "frontend-name",      'F', OPTPARSE_REQUIRED },
        { "backend-name",       'B', OPTPARSE_REQUIRED },
        { "period-size",        'p', OPTPARSE_REQUIRED },
        { "period-count",       'q', OPTPARSE_REQUIRED },
        { "channels",           'n', OPTPARSE_REQUIRED },
        { "rate",               'r', OPTPARSE_REQUIRED },
        { "bits",               'b', OPTPARSE_REQUIRED },
        { "float",              'f', OPTPARSE_NONE     },
        { "framesize-factor",   's', OPTPARSE_REQUIRED },
        { "stream",             'w', OPTPARSE_REQUIRED },
        { "streampp",           'x', OPTPARSE_REQUIRED },
        { "devicepp",           'y', OPTPARSE_REQUIRED },
        { "device",             'z', OPTPARSE_REQUIRED },
        { "instance",           'i', OPTPARSE_REQUIRED },
        { "playback-path",      'o', OPTPARSE_REQUIRED },
        { "help",               'h', OPTPARSE_NONE     },
        { 0, 0, OPTPARSE_NONE }
    };

    printf("\nAudioReach Audioengine | project: %s\n\n", PROJECT_NAME);

    init_settings(&settings);

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, long_options, nullptr)) != -1) {
        switch (c) {
        case 'c':
            if (sscanf(opts.optarg, "%u", &settings.virtual_card) != 1) {
                fprintf(stderr, "failed parsing virtual card number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'd':
            if (sscanf(opts.optarg, "%u", &settings.playback.virtual_device) != 1) {
                fprintf(stderr, "failed parsing virtual device number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'F':
            settings.playback.frontend_name = strdup(opts.optarg);
            if (settings.playback.frontend_name == nullptr) {
                fprintf(stderr, "failed parsing frontend name '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'B':
            settings.playback.backend_name = strdup(opts.optarg);
            if (settings.playback.backend_name == nullptr) {
                fprintf(stderr, "failed parsing backend name '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'C':
            if (sscanf(opts.optarg, "%u", &settings.physical_card) != 1) {
                fprintf(stderr, "failed parsing physical card number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'D':
            if (sscanf(opts.optarg, "%u", &settings.playback.physical_device) != 1) {
                fprintf(stderr, "failed parsing physical device number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'p':
            if (sscanf(opts.optarg, "%u", &settings.playback.config.period_size) != 1) {
                fprintf(stderr, "failed parsing period size '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'q':
            if (sscanf(opts.optarg, "%u", &settings.playback.config.period_count) != 1) {
                fprintf(stderr, "failed parsing period count '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            if (sscanf(opts.optarg, "%u", &settings.playback.config.channels) != 1) {
                fprintf(stderr, "failed parsing channel count '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'r':
            if (sscanf(opts.optarg, "%u", &settings.playback.config.rate) != 1) {
                fprintf(stderr, "failed parsing rate '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'b':
            if (sscanf(opts.optarg, "%u", &settings.bits) != 1) {
                fprintf(stderr, "failed parsing bits per one sample '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            settings.is_float = true;
            break;
        case 's':
            if (sscanf(opts.optarg, "%u", &settings.frame_size_fcr) != 1) {
                fprintf(stderr, "failed parsing framesize factor '%s'\n", opts.optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'w':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings.playback.stream_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings.playback.stream_kv.value = get_streamrx_value(opts.optarg);
                if (settings.playback.stream_kv.value == 0) {
                    fprintf(stderr, "failed parsing stream key value '%s' (not a valid number or stream name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'x':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings.playback.streampp_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings.playback.streampp_kv.value = get_streampp_rx_value(opts.optarg);
                if (settings.playback.streampp_kv.value == 0) {
                    fprintf(stderr, "failed parsing streampp key value '%s' (not a valid number or streampp name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'y':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings.playback.devicepp_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings.playback.devicepp_kv.value = get_device_pp_rx_value(opts.optarg);
                if (settings.playback.devicepp_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicepp key value '%s' (not a valid number or devicepp name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'z':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings.playback.device_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings.playback.device_kv.value = get_device_rx_value(opts.optarg);
                if (settings.playback.device_kv.value == 0) {
                    fprintf(stderr, "failed parsing device key value '%s' (not a valid number or device name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'i':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings.playback.instance_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings.playback.instance_kv.value = get_instance_value(opts.optarg);
                if (settings.playback.instance_kv.value == 0) {
                    fprintf(stderr, "failed parsing instance key value '%s' (not a valid number or instance name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'o':
            free(settings.playback.mixer_path);
            settings.playback.mixer_path = strdup(opts.optarg);
            if (settings.playback.mixer_path == nullptr) {
                fprintf(stderr, "failed parsing playback path '%s'\n", opts.optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        case '?':
            fprintf(stderr, "%s\n", opts.errmsg);
            return EXIT_FAILURE;
        }
    }

    if (resolve_stream_names(CARDS_CONF_FILE, settings.virtual_card, settings.physical_card,
                             &settings.playback) < 0) {
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_ctx(&ctx, &settings, &settings.playback) < 0) {
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_hw_mixer(MIXER_PATHS, settings.physical_card) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (set_hw_mixer_path(settings.playback.mixer_path) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (init_agm_mixer(settings.virtual_card, settings.playback.frontend_name, settings.playback.backend_name,
        (char *)BACKEND_CONF_FILE, settings.playback.stream_kv, settings.playback.instance_kv,
        settings.playback.streampp_kv, settings.playback.devicepp_kv, settings.playback.device_kv ) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (configure_agm_modules(settings.physical_card, settings.playback.physical_device, settings.playback.config.period_count,
        settings.frame_size_fcr) < 0) {
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

//    inspect_agm_modules();

    if (init_pcm(&ctx, &settings, &settings.playback) < 0) {
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    if (start_audio(&ctx) < 0) {
        cleanup_pcm(ctx.pcm);
        cleanup_agm_mixer();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_settings(&settings);
        return EXIT_FAILURE;
    }

    cleanup_pcm(ctx.pcm);
    cleanup_agm_mixer();
    cleanup_hw_mixer();
    cleanup_ctx(&ctx);
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

        // split int into consecutive bytes
        if (!ctx->is_big_endian) {
            byteSplit_littleEndian(ctx, sampleBytes, res);
        }
        else {
            byteSplit_bigEndian(ctx, sampleBytes, res);
        }

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

        // split int into consecutive bytes
        if (!ctx->is_big_endian) {
            byteSplit_littleEndian(ctx, sampleBytes, res);
        }
        else {
            byteSplit_bigEndian(ctx, sampleBytes, res);
        }

        sampleBytes += ctx->bytes_per_sample; // jump to next sample
    }
    // clean up buffer for next period
    memset(ctx->audio_buffer, 0, ctx->num_samples*sizeof(float));
}

// split an integer sample over more bytes [1 or more, according to format]
// little endian -> byte_0, byte_1, ..., byte_n-1 [more common format]
void byteSplit_littleEndian(struct pcm_ctx *ctx, unsigned char* sampleBytes, int value)
{
    for (unsigned int i = 0; i <ctx->bytes_per_sample; i++)
        *(sampleBytes + i) = (value >>  i * 8) & 0xff;
}
// big endian -> byte_n-1, byte_n-2, ..., byte_0
void byteSplit_bigEndian(struct pcm_ctx *ctx, unsigned char *sampleBytes, int value)
{
    for (unsigned int i = 0; i <ctx->bytes_per_sample; i++)
        *(sampleBytes + ctx->phys_bytes_per_sample - 1 - i) = (value >> i * 8) & 0xff;
}
