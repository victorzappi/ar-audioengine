/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// command-line parsing and engine defaults, split out of main.cpp to keep the
// entry point focused on orchestration.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include <kvh2xml.h>            // graph key/value macros (STREAMRX, PCM_LL_PLAYBACK, ...)
#include "audioreach_mappings.h" // get_*_value string->key lookups

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

#define DEFAULT_PLAYBACK_MIXER_PATH "speaker"
#define DEFAULT_CAPTURE_MIXER_PATH "speaker-mic"

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------

// set the defaults; CLI args overwrite the relevant fields in parse_cli
void init_settings(struct settings *settings)
{
    // shared across both directions
    settings->virtual_card = 100;
    settings->physical_card = 0;
    settings->full_duplex = true;

    // playback stream
    struct pcm_stream *playback = &settings->playback;

    playback->virtual_device = 100;
    playback->physical_device = 0;
    playback->frontend_name = nullptr;
    playback->backend_name = nullptr;
    playback->mixer_path = strdup(DEFAULT_PLAYBACK_MIXER_PATH);
    playback->flags = PCM_OUT;

    playback->bits = 16;
    playback->is_float = false;

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

    // capture stream (mirrors playback structure with capture identity)
    struct pcm_stream *capture = &settings->capture;

    capture->virtual_device = 101;
    capture->physical_device = 1;
    capture->frontend_name = nullptr;
    capture->backend_name = nullptr;
    capture->mixer_path = strdup(DEFAULT_CAPTURE_MIXER_PATH);
    capture->flags = PCM_IN;

    capture->bits = 16;
    capture->is_float = false;

    capture->config.period_size = 960;
    capture->config.period_count = 4;
    capture->config.channels = 2;
    capture->config.rate = 48000;
    capture->config.format = PCM_FORMAT_INVALID;  // derived from bits/is_float in init_ctx

    capture->config.silence_threshold = 0;
    capture->config.silence_size = 0;
    capture->config.stop_threshold = 0;
    capture->config.start_threshold = 0;

    // capture graph keys; values default to 0 (override via CLI; TX names accepted)
    capture->stream_kv.key     = STREAMTX;
    capture->stream_kv.value   = PCM_RECORD;

    capture->streampp_kv.key   = STREAMPP_TX;
    capture->streampp_kv.value = 0;

    capture->instance_kv.key   = INSTANCE;
    capture->instance_kv.value = INSTANCE_1;

    capture->devicepp_kv.key   = DEVICEPP_TX;
    capture->devicepp_kv.value = 0;

    capture->device_kv.key     = DEVICETX;
    capture->device_kv.value   = SPEAKER_MIC;
}

static void cleanup_stream(struct pcm_stream *stream)
{
    if (stream->frontend_name != nullptr)
        free(stream->frontend_name);
    if (stream->backend_name != nullptr)
        free(stream->backend_name);
    if (stream->mixer_path != nullptr)
        free(stream->mixer_path);
}

void cleanup_settings(struct settings *settings)
{
    cleanup_stream(&settings->playback);
    cleanup_stream(&settings->capture);
}

// ---------------------------------------------------------------------------
// usage
// ---------------------------------------------------------------------------

static void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [options]\n", argv0);
    fprintf(stderr, "\nShared options (apply to the whole engine):\n");
    fprintf(stderr, "-c | --virtual-card <card num>         The virtual card number that contains the frontend device\n");
    fprintf(stderr, "-s | --physical-card <card num>        The physical card number that contains the backend device\n");
    fprintf(stderr, "-p | --period-size <size>              The PCM period size (copied to both playback and capture)\n");
    fprintf(stderr, "-q | --period-count <count>            The number of PCM periods (copied to both playback and capture)\n");
    fprintf(stderr, "-r | --rate <rate>                     The audio sample rate (copied to both playback and capture)\n");
    fprintf(stderr, "-u | --no-capture                      Disable full-duplex (playback only)\n");
    fprintf(stderr, "-h | --help                            Print this help and exit\n");

    fprintf(stderr, "\nPlayback options:\n");
    fprintf(stderr, "-d | --playback-virtual-device <num>   The virtual device number that represents the frontend\n");
    fprintf(stderr, "-e | --playback-physical-device <num>  The physical device number that represents the backend\n");
    fprintf(stderr, "-t | --playback-frontend-name <name>   The frontend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-k | --playback-backend-name <name>    The backend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-n | --playback-channels <count>       The number of channels\n");
    fprintf(stderr, "-b | --playback-bits <bit-count>       The number of bits in one sample\n");
    fprintf(stderr, "-f | --playback-float                  The samples are in floating-point PCM\n");
    fprintf(stderr, "-o | --playback-path <path>            The hardware mixer playback path\n");
    fprintf(stderr, "-w | --streamrx <value>                The stream key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-x | --streampp-rx <value>             The streampp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-y | --devicepp-rx <value>             The devicepp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-z | --devicerx <value>                The device key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-i | --instancerx <value>              The instance key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "     --playback-period-size <size>     Override the playback period size only\n");
    fprintf(stderr, "     --playback-period-count <count>   Override the playback period count only\n");
    fprintf(stderr, "     --playback-rate <rate>            Override the playback sample rate only\n");

    fprintf(stderr, "\nCapture options (full duplex):\n");
    fprintf(stderr, "-D | --capture-virtual-device <num>    The virtual device number that represents the frontend\n");
    fprintf(stderr, "-E | --capture-physical-device <num>   The physical device number that represents the backend\n");
    fprintf(stderr, "-T | --capture-frontend-name <name>    The frontend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-K | --capture-backend-name <name>     The backend device name (parsed automatically if not set)\n");
    fprintf(stderr, "-N | --capture-channels <count>        The number of channels\n");
    fprintf(stderr, "-B | --capture-bits <bit-count>        The number of bits in one sample\n");
    fprintf(stderr, "-F | --capture-float                   The samples are in floating-point PCM\n");
    fprintf(stderr, "-O | --capture-path <path>             The hardware mixer capture path\n");
    fprintf(stderr, "-W | --streamtx <value>                The stream key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-X | --streampp-tx <value>             The streampp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-Y | --devicepp-tx <value>             The devicepp key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-Z | --devicetx <value>                The device key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "-I | --instancetx <value>              The instance key value as a string or number, both decimal and hex (0 if not present)\n");
    fprintf(stderr, "     --capture-period-size <size>      Override the capture period size only\n");
    fprintf(stderr, "     --capture-period-count <count>    Override the capture period count only\n");
    fprintf(stderr, "     --capture-rate <rate>             Override the capture sample rate only\n");
}

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

int parse_cli(int argc, char **argv, struct settings *settings)
{
    (void)argc;
    int c;

    // long-only option ids (no short flag); start past the ASCII range so they
    // never collide with the single-char options
    enum {
        OPT_PB_PERIOD_SIZE = 256,
        OPT_PB_PERIOD_COUNT,
        OPT_PB_RATE,
        OPT_CAP_PERIOD_SIZE,
        OPT_CAP_PERIOD_COUNT,
        OPT_CAP_RATE,
    };

    struct optparse opts;
    struct optparse_long long_options[] = {
        // shared (lowercase, single)
        { "virtual-card",            'c', OPTPARSE_REQUIRED },
        { "physical-card",           's', OPTPARSE_REQUIRED },
        { "period-size",             'p', OPTPARSE_REQUIRED },
        { "period-count",            'q', OPTPARSE_REQUIRED },
        { "rate",                    'r', OPTPARSE_REQUIRED },
        { "no-capture",              'u', OPTPARSE_NONE     },
        { "help",                    'h', OPTPARSE_NONE     },
        // playback (lowercase; capture is the same letter upper-cased)
        { "playback-virtual-device", 'd', OPTPARSE_REQUIRED },
        { "playback-physical-device",'e', OPTPARSE_REQUIRED },
        { "playback-frontend-name",  't', OPTPARSE_REQUIRED },
        { "playback-backend-name",   'k', OPTPARSE_REQUIRED },
        { "playback-channels",       'n', OPTPARSE_REQUIRED },
        { "playback-bits",           'b', OPTPARSE_REQUIRED },
        { "playback-float",          'f', OPTPARSE_NONE     },
        { "playback-path",           'o', OPTPARSE_REQUIRED },
        { "streamrx",                'w', OPTPARSE_REQUIRED },
        { "streampp-rx",             'x', OPTPARSE_REQUIRED },
        { "devicepp-rx",             'y', OPTPARSE_REQUIRED },
        { "devicerx",                'z', OPTPARSE_REQUIRED },
        { "instancerx",              'i', OPTPARSE_REQUIRED },
        // capture (UPPERCASE mirror of the playback letter)
        { "capture-virtual-device",  'D', OPTPARSE_REQUIRED },
        { "capture-physical-device", 'E', OPTPARSE_REQUIRED },
        { "capture-frontend-name",   'T', OPTPARSE_REQUIRED },
        { "capture-backend-name",    'K', OPTPARSE_REQUIRED },
        { "capture-channels",        'N', OPTPARSE_REQUIRED },
        { "capture-bits",            'B', OPTPARSE_REQUIRED },
        { "capture-float",           'F', OPTPARSE_NONE     },
        { "capture-path",            'O', OPTPARSE_REQUIRED },
        { "streamtx",                'W', OPTPARSE_REQUIRED },
        { "streampp-tx",             'X', OPTPARSE_REQUIRED },
        { "devicepp-tx",             'Y', OPTPARSE_REQUIRED },
        { "devicetx",                'Z', OPTPARSE_REQUIRED },
        { "instancetx",              'I', OPTPARSE_REQUIRED },
        // long-only overrides (no short flag). MUST come last: optparse builds its
        // short-option string by casting each shortname to char, so these >255 ids
        // would otherwise emit a stray NUL and hide the short flags listed after them
        { "playback-period-size",    OPT_PB_PERIOD_SIZE,   OPTPARSE_REQUIRED },
        { "playback-period-count",   OPT_PB_PERIOD_COUNT,  OPTPARSE_REQUIRED },
        { "playback-rate",           OPT_PB_RATE,          OPTPARSE_REQUIRED },
        { "capture-period-size",     OPT_CAP_PERIOD_SIZE,  OPTPARSE_REQUIRED },
        { "capture-period-count",    OPT_CAP_PERIOD_COUNT, OPTPARSE_REQUIRED },
        { "capture-rate",            OPT_CAP_RATE,         OPTPARSE_REQUIRED },
        { 0, 0, OPTPARSE_NONE }
    };

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, long_options, nullptr)) != -1) {
        switch (c) {
        // ----- shared -----
        case 'c':
            if (sscanf(opts.optarg, "%u", &settings->virtual_card) != 1) {
                fprintf(stderr, "failed parsing virtual card number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 's':
            if (sscanf(opts.optarg, "%u", &settings->physical_card) != 1) {
                fprintf(stderr, "failed parsing physical card number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'p': {
            // shared: set once, copied to both streams
            unsigned int v;
            if (sscanf(opts.optarg, "%u", &v) != 1) {
                fprintf(stderr, "failed parsing period size '%s'\n", opts.optarg);
                return -1;
            }
            settings->playback.config.period_size = v;
            settings->capture.config.period_size = v;
            break;
        }
        case 'q': {
            unsigned int v;
            if (sscanf(opts.optarg, "%u", &v) != 1) {
                fprintf(stderr, "failed parsing period count '%s'\n", opts.optarg);
                return -1;
            }
            settings->playback.config.period_count = v;
            settings->capture.config.period_count = v;
            break;
        }
        case 'r': {
            unsigned int v;
            if (sscanf(opts.optarg, "%u", &v) != 1) {
                fprintf(stderr, "failed parsing rate '%s'\n", opts.optarg);
                return -1;
            }
            settings->playback.config.rate = v;
            settings->capture.config.rate = v;
            break;
        }
        case 'u':
            settings->full_duplex = false;
            break;
        case 'h':
            print_usage(argv[0]);
            return 1;

        // ----- playback -----
        case 'd':
            if (sscanf(opts.optarg, "%u", &settings->playback.virtual_device) != 1) {
                fprintf(stderr, "failed parsing playback virtual device number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'e':
            if (sscanf(opts.optarg, "%u", &settings->playback.physical_device) != 1) {
                fprintf(stderr, "failed parsing playback physical device number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 't':
            settings->playback.frontend_name = strdup(opts.optarg);
            if (settings->playback.frontend_name == nullptr) {
                fprintf(stderr, "failed parsing playback frontend name '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'k':
            settings->playback.backend_name = strdup(opts.optarg);
            if (settings->playback.backend_name == nullptr) {
                fprintf(stderr, "failed parsing playback backend name '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'n':
            if (sscanf(opts.optarg, "%u", &settings->playback.config.channels) != 1) {
                fprintf(stderr, "failed parsing playback channel count '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'b':
            if (sscanf(opts.optarg, "%u", &settings->playback.bits) != 1) {
                fprintf(stderr, "failed parsing playback bits per one sample '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'f':
            settings->playback.is_float = true;
            break;
        case 'o':
            free(settings->playback.mixer_path);
            settings->playback.mixer_path = strdup(opts.optarg);
            if (settings->playback.mixer_path == nullptr) {
                fprintf(stderr, "failed parsing playback path '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'w':
            /* Try to parse as number first, as both decimal and hex */
            if (sscanf(opts.optarg, "%i", &settings->playback.stream_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                settings->playback.stream_kv.value = get_streamrx_value(opts.optarg);
                if (settings->playback.stream_kv.value == 0) {
                    fprintf(stderr, "failed parsing streamrx key value '%s' (not a valid number or stream name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'x':
            if (sscanf(opts.optarg, "%i", &settings->playback.streampp_kv.value) != 1) {
                settings->playback.streampp_kv.value = get_streampp_rx_value(opts.optarg);
                if (settings->playback.streampp_kv.value == 0) {
                    fprintf(stderr, "failed parsing streampp-rx key value '%s' (not a valid number or streampp name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'y':
            if (sscanf(opts.optarg, "%i", &settings->playback.devicepp_kv.value) != 1) {
                settings->playback.devicepp_kv.value = get_device_pp_rx_value(opts.optarg);
                if (settings->playback.devicepp_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicepp-rx key value '%s' (not a valid number or devicepp name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'z':
            if (sscanf(opts.optarg, "%i", &settings->playback.device_kv.value) != 1) {
                settings->playback.device_kv.value = get_device_rx_value(opts.optarg);
                if (settings->playback.device_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicerx key value '%s' (not a valid number or device name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'i':
            if (sscanf(opts.optarg, "%i", &settings->playback.instance_kv.value) != 1) {
                settings->playback.instance_kv.value = get_instance_value(opts.optarg);
                if (settings->playback.instance_kv.value == 0) {
                    fprintf(stderr, "failed parsing instancerx key value '%s' (not a valid number or instance name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case OPT_PB_PERIOD_SIZE:
            if (sscanf(opts.optarg, "%u", &settings->playback.config.period_size) != 1) {
                fprintf(stderr, "failed parsing playback period size '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case OPT_PB_PERIOD_COUNT:
            if (sscanf(opts.optarg, "%u", &settings->playback.config.period_count) != 1) {
                fprintf(stderr, "failed parsing playback period count '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case OPT_PB_RATE:
            if (sscanf(opts.optarg, "%u", &settings->playback.config.rate) != 1) {
                fprintf(stderr, "failed parsing playback rate '%s'\n", opts.optarg);
                return -1;
            }
            break;

        // ----- capture -----
        case 'D':
            if (sscanf(opts.optarg, "%u", &settings->capture.virtual_device) != 1) {
                fprintf(stderr, "failed parsing capture virtual device number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'E':
            if (sscanf(opts.optarg, "%u", &settings->capture.physical_device) != 1) {
                fprintf(stderr, "failed parsing capture physical device number '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'T':
            settings->capture.frontend_name = strdup(opts.optarg);
            if (settings->capture.frontend_name == nullptr) {
                fprintf(stderr, "failed parsing capture frontend name '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'K':
            settings->capture.backend_name = strdup(opts.optarg);
            if (settings->capture.backend_name == nullptr) {
                fprintf(stderr, "failed parsing capture backend name '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'N':
            if (sscanf(opts.optarg, "%u", &settings->capture.config.channels) != 1) {
                fprintf(stderr, "failed parsing capture channel count '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'B':
            if (sscanf(opts.optarg, "%u", &settings->capture.bits) != 1) {
                fprintf(stderr, "failed parsing capture bits per one sample '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'F':
            settings->capture.is_float = true;
            break;
        case 'O':
            free(settings->capture.mixer_path);
            settings->capture.mixer_path = strdup(opts.optarg);
            if (settings->capture.mixer_path == nullptr) {
                fprintf(stderr, "failed parsing capture path '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case 'W':
            if (sscanf(opts.optarg, "%i", &settings->capture.stream_kv.value) != 1) {
                settings->capture.stream_kv.value = get_streamtx_value(opts.optarg);
                if (settings->capture.stream_kv.value == 0) {
                    fprintf(stderr, "failed parsing streamtx key value '%s' (not a valid number or stream name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'X':
            if (sscanf(opts.optarg, "%i", &settings->capture.streampp_kv.value) != 1) {
                settings->capture.streampp_kv.value = get_streampp_tx_value(opts.optarg);
                if (settings->capture.streampp_kv.value == 0) {
                    fprintf(stderr, "failed parsing streampp-tx key value '%s' (not a valid number or streampp name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'Y':
            if (sscanf(opts.optarg, "%i", &settings->capture.devicepp_kv.value) != 1) {
                settings->capture.devicepp_kv.value = get_device_pp_tx_value(opts.optarg);
                if (settings->capture.devicepp_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicepp-tx key value '%s' (not a valid number or devicepp name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'Z':
            if (sscanf(opts.optarg, "%i", &settings->capture.device_kv.value) != 1) {
                settings->capture.device_kv.value = get_device_tx_value(opts.optarg);
                if (settings->capture.device_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicetx key value '%s' (not a valid number or device name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case 'I':
            /* instance is direction-agnostic: numbers, or the shared instance map */
            if (sscanf(opts.optarg, "%i", &settings->capture.instance_kv.value) != 1) {
                settings->capture.instance_kv.value = get_instance_value(opts.optarg);
                if (settings->capture.instance_kv.value == 0) {
                    fprintf(stderr, "failed parsing instancetx key value '%s' (not a valid number or instance name)\n", opts.optarg);
                    return -1;
                }
            }
            break;
        case OPT_CAP_PERIOD_SIZE:
            if (sscanf(opts.optarg, "%u", &settings->capture.config.period_size) != 1) {
                fprintf(stderr, "failed parsing capture period size '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case OPT_CAP_PERIOD_COUNT:
            if (sscanf(opts.optarg, "%u", &settings->capture.config.period_count) != 1) {
                fprintf(stderr, "failed parsing capture period count '%s'\n", opts.optarg);
                return -1;
            }
            break;
        case OPT_CAP_RATE:
            if (sscanf(opts.optarg, "%u", &settings->capture.config.rate) != 1) {
                fprintf(stderr, "failed parsing capture rate '%s'\n", opts.optarg);
                return -1;
            }
            break;

        case '?':
            fprintf(stderr, "%s\n", opts.errmsg);
            return -1;
        }
    }

    return 0;
}
