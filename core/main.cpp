/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* audioengine
real-time audio engine

build for Yocto RPi4:
make

usage on Yocto RPi4:
./audioengine -c 100 -d 100 -B CODEC_DMA-LPAIF_WSA-RX-0 -C 0 -D 0 -x PCM_LL_PLAYBACK -w DEVICEPP_RX_AUDIO_MBDRC -z SPEAKER -i INSTANCE_1
*/

// next steps:
// [?]_simplify formats:
// __remove support for float format
// __remove support for BE formats [we use signed int LE only]
// __and make cmd line format preset selection instead of bits
// __assume always LE arch
// _at this point can use a simplified fromFloatToRaw_littleEndian() for all, with a single memcpy per sample [byte size of transfer depends on format]
// _but make also an optmized version ---> 8, 16, 24/32 can be done seamlessly as in LDSP standard case [by casting rawBUffer as uint_8, uint_16 and uint_32 ---> 3 cases], while 24_3 can stick to single memcpy [3 bytes] per sample 

#include <signal.h>
#include <stdbool.h> //VIC needed only in C
#include <stdint.h> //VIC needed only in C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include "pcm_utils.h"
#include "hw_mixer.h"
#include "agm_mixer.h"
#include "audioreach_mappings.h"
#include "render.h"

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

#define MIXER_PATHS "/etc/mixer_paths_qcs6490_rb3gen2.xml"
#define BACKEND_CONF_FILE "/etc/backend_conf.xml"

const char* hw_mixer_path = "speaker";

struct cmd {
    unsigned int virtual_card;
    unsigned int frontend_device;
    char frontend_name[7];
    char *backend_name;
    unsigned int physical_card;
    unsigned int physical_device;

    unsigned int frame_size_fcr;

    // pcm/frontend params
    int flags;
    struct pcm_config config;
    unsigned int bits;
    bool is_float;
    // graph params
    struct agm_key_value stream_kv;
    struct agm_key_value instance_kv;
    struct agm_key_value streampp_kv;
    struct agm_key_value devicepp_kv;
    struct agm_key_value device_kv;
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



void init_cmd(struct cmd *cmd)
{
    cmd->virtual_card = 100;
    cmd->frontend_device = 100;
    snprintf(cmd->frontend_name, 7, "PCM%d", cmd->frontend_device);
    cmd->backend_name = strdup("CODEC_DMA-LPAIF_WSA-RX-0");
    cmd->physical_card = 0;
    cmd->physical_device = 0;

    cmd->frame_size_fcr = 10; //VIC for now not used, because it breaks multi stream with more instances in parallel

    cmd->flags = PCM_OUT;
    cmd->config.period_size = 960;
    cmd->config.period_count = 4;
    cmd->config.channels = 2;
    cmd->config.rate = 48000;
    cmd->config.format = PCM_FORMAT_S16_LE;
    
    // these can be left to default, because ARE does not support aumtomatic pcm start/stop
    cmd->config.silence_threshold = 0;
    cmd->config.silence_size = 0;
    cmd->config.stop_threshold = 0;
    cmd->config.start_threshold = 0;

    cmd->bits = 16;
    cmd->is_float = false;

    // we set these to load the graph for the default RPi use case
    cmd->stream_kv.key     = STREAMRX;
    cmd->stream_kv.value   = PCM_LL_PLAYBACK;

    cmd->streampp_kv.key   = STREAMPP_RX;
    cmd->streampp_kv.value = 0;
    
    cmd->instance_kv.key   = INSTANCE;
    cmd->instance_kv.value = INSTANCE_1;
    
    cmd->devicepp_kv.key   = DEVICEPP_RX;
    cmd->devicepp_kv.value = DEVICEPP_RX_AUDIO_MBDRC;
    
    cmd->device_kv.key     = DEVICERX;
    cmd->device_kv.value   = SPEAKER;
}

static int init_ctx(struct pcm_ctx* ctx, struct cmd *cmd)
{
    struct pcm_config *config = &cmd->config;

    size_t raw_buffer_size = 0;   
    size_t audio_buffer_size = 0;   

    ctx->pcm = NULL;
    ctx->audio_buffer = NULL;
    ctx->raw_buffer = NULL;

    /* prepare configuration to open pcm */
    if (cmd->is_float) {
        config->format = PCM_FORMAT_FLOAT_LE;
    } 
    else {
        config->format = signed_pcm_bits_to_format(cmd->bits);
        if (config->format == -1) {
            fprintf(stderr, "bit count '%u' not supported\n", cmd->bits);
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
    if (!cmd->is_float)
	    ctx->max_value = (1 << (cmd->bits - 1)) - 1; //TODO add min value and improve quantization as in LDSP

    ctx->is_big_endian = is_format_big_endian(config->format);
    ctx->is_float = is_format_float(config->format);

    ctx->num_samples = config->period_size * config->channels;

    audio_buffer_size = ctx->num_samples*sizeof(float);
    ctx->audio_buffer = (float*)malloc(audio_buffer_size); //TODO calloc
    if ( !(ctx->audio_buffer) ) {
        fprintf(stderr, "unable to allocate %zu bytes\n", audio_buffer_size);
        return -1;
    }
    memset(ctx->audio_buffer, 0, audio_buffer_size);

    raw_buffer_size = ctx->num_samples * ctx->phys_bytes_per_sample;
    ctx->raw_buffer = (char*)malloc(raw_buffer_size); //TODO calloc
    if ( !(ctx->raw_buffer) ) {
        fprintf(stderr, "unable to allocate %zu bytes\n", raw_buffer_size);
        return -1;
    }
    memset(ctx->raw_buffer, 0, raw_buffer_size);

    return 0;
}

static int init_pcm(struct pcm_ctx* ctx, struct cmd *cmd)
{
    // we cannot check the param ranges on the frontend, because it's a virtual pcm!

    /* open pcm */
    ctx->pcm = pcm_open(cmd->virtual_card,
                        cmd->frontend_device,
                        cmd->flags,
                        &cmd->config);
    
    if (!ctx->pcm) {
        fprintf(stderr, "failed to open frontend pcm %u,%u\n",
                cmd->virtual_card, cmd->frontend_device);
        return -1;
    }
    
    if (!ctx->pcm || !pcm_is_ready(ctx->pcm)) {
        fprintf(stderr, "failed to open for pcm %u,%u. %s\n",
                cmd->virtual_card, cmd->frontend_device,
                pcm_get_error(ctx->pcm));
        pcm_close(ctx->pcm);
        return -1;
    }

    return 0;
}

void cleanup_cmd(struct cmd *cmd) 
{
    if (cmd->backend_name != NULL)
        free(cmd->backend_name);
}

void cleanup_ctx(struct pcm_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->audio_buffer != NULL) {
        free(ctx->audio_buffer);
    }
    if (ctx->raw_buffer != NULL) {
        free(ctx->raw_buffer);
    }
}

void cleanup_pcm(struct pcm *pcm)
{
    printf("pcm_cleanup\n");
    if (pcm != NULL) {
        pcm_close(pcm);
    }
}


static int should_stop = 0;

int audio_loop(struct pcm_ctx *ctx);
struct audio_ctx create_audio_ctx(struct pcm_ctx *ctx);
void fromFloatToRaw_int(struct pcm_ctx *ctx);
void fromFloatToRaw_float(struct pcm_ctx *ctx);
void byteSplit_littleEndian(struct pcm_ctx *ctx, unsigned char* sampleBytes, int value);
void byteSplit_bigEndian(struct pcm_ctx *ctx, unsigned char *sampleBytes, int value);


void stream_close(int sig)
{
    /* allow the stream to be closed gracefully */
    signal(sig, SIG_IGN);
    should_stop = 1;
}

static void *audio_thread_func(void *arg)
{
    struct pcm_ctx *ctx = (struct pcm_ctx *)arg;
    audio_loop(ctx);
    return NULL;
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
        if (pthread_create(&thread, NULL, audio_thread_func, ctx) != 0) {
            fprintf(stderr, "failed to create audio thread\n");
            pthread_attr_destroy(&attr);
            return -1;
        }
    }
    pthread_attr_destroy(&attr);
    pthread_join(thread, NULL);

    return 0;
}

void print_usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [options]\n", argv0);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "-c | --virtual-card <card num>         The virtual card number that contains frontend and backend devices\n");
    fprintf(stderr, "-d | --frontend-device <device num>    The frontend device number\n");
    fprintf(stderr, "-B | --backend-name <name>             The backend device name\n");
    fprintf(stderr, "-C | --physical-card <card num>        The physical card number (Alsa Sink card)\n");
    fprintf(stderr, "-D | --physical-device <device num>    The physical device number (Alsa Sink device)\n");
    fprintf(stderr, "-p | --period-size <size>              The size of the frontend PCM period\n");
    fprintf(stderr, "-q | --period-count <count>            The number of frontend PCM periods\n");
    fprintf(stderr, "-n | --channels <count>                The number of channels\n");
    fprintf(stderr, "-r | --rate <rate>                     The audio sample rate\n");
    fprintf(stderr, "-b | --bits <bit-count>                The number of bits in one sample\n");
    fprintf(stderr, "-f | --float                           The samples are in floating-point PCM\n");
    fprintf(stderr, "-x | --stream <value>                  The stream key value as a string or number (0 if not present)\n");
    fprintf(stderr, "-y | --streampp <value>                The streampp key value as a string or number (0 if not present)\n");
    fprintf(stderr, "-w | --devicepp <value>                The devicepp key value as a string or number (0 if not present)\n");
    fprintf(stderr, "-z | --device <value>                  The device key value as a string or number (0 if not present)\n");
    fprintf(stderr, "-i | --instance <value>                The intance key value as a string or number (0 if not present)\n");
}

int main(int argc, char **argv)
{
    int c;
    struct cmd cmd;
    struct pcm_ctx ctx;

    struct optparse opts;
    struct optparse_long long_options[] = {
        { "frontend-card",      'c', OPTPARSE_REQUIRED },
        { "frontend-device",    'd', OPTPARSE_REQUIRED },
        { "backend-name",       'B', OPTPARSE_REQUIRED },
        { "backend-card",       'C', OPTPARSE_REQUIRED },
        { "backend-device",     'D', OPTPARSE_REQUIRED },
        { "period-size",        'p', OPTPARSE_REQUIRED },
        { "period-count",       'q', OPTPARSE_REQUIRED },
        { "channels",           'n', OPTPARSE_REQUIRED },
        { "rate",               'r', OPTPARSE_REQUIRED },
        { "bits",               'b', OPTPARSE_REQUIRED },
        { "float",              'f', OPTPARSE_NONE     },
        { "stream",             'x', OPTPARSE_REQUIRED },
        { "streampp",           'y', OPTPARSE_REQUIRED },
        { "devicepp",           'w', OPTPARSE_REQUIRED },
        { "device",             'z', OPTPARSE_REQUIRED },
        { "instance",           'i', OPTPARSE_REQUIRED },
        { "help",               'h', OPTPARSE_NONE     },
        { 0, 0, OPTPARSE_NONE }
    };

    printf("\nAudioReach Audioengine | project: %s\n\n", PROJECT_NAME);

    init_cmd(&cmd);

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, long_options, NULL)) != -1) {
        switch (c) {
        case 'c':
            if (sscanf(opts.optarg, "%u", &cmd.virtual_card) != 1) {
                fprintf(stderr, "failed parsing virtual card number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'd':
            if (sscanf(opts.optarg, "%u", &cmd.frontend_device) != 1) {
                fprintf(stderr, "failed parsing frontend device number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            if (snprintf(cmd.frontend_name, 7, "PCM%d", cmd.frontend_device) <= 0) {
                fprintf(stderr, "failed parsing frontend device number '%s', must be 3 digits\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'B':
            cmd.backend_name = strdup(opts.optarg);
            if (cmd.backend_name == NULL) {
                fprintf(stderr, "failed parsing backend name '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'C':
            if (sscanf(opts.optarg, "%u", &cmd.physical_card) != 1) {
                fprintf(stderr, "failed parsing physical card number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'D':
            if (sscanf(opts.optarg, "%u", &cmd.physical_device) != 1) {
                fprintf(stderr, "failed parsing physical device number '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'p':
            if (sscanf(opts.optarg, "%u", &cmd.config.period_size) != 1) {
                fprintf(stderr, "failed parsing period size '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'q':
            if (sscanf(opts.optarg, "%u", &cmd.config.period_count) != 1) {
                fprintf(stderr, "failed parsing period count '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            if (sscanf(opts.optarg, "%u", &cmd.config.channels) != 1) {
                fprintf(stderr, "failed parsing channel count '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'r':
            if (sscanf(opts.optarg, "%u", &cmd.config.rate) != 1) {
                fprintf(stderr, "failed parsing rate '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'b':
            if (sscanf(opts.optarg, "%u", &cmd.bits) != 1) {
                fprintf(stderr, "failed parsing bits per one sample '%s'\n", argv[1]);
                return EXIT_FAILURE;
            }
            break;
        case 'f':
            cmd.is_float = true;
            break;
        case 'x':
            /* Try to parse as number first */
            if (sscanf(opts.optarg, "%u", &cmd.stream_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                cmd.stream_kv.value = get_streamrx_value(opts.optarg);
                if (cmd.stream_kv.value == 0) {
                    fprintf(stderr, "failed parsing stream key value '%s' (not a valid number or stream name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'y':
            /* Try to parse as number first */
            if (sscanf(opts.optarg, "%u", &cmd.streampp_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                cmd.streampp_kv.value = get_streampp_rx_value(opts.optarg);
                if (cmd.streampp_kv.value == 0) {
                    fprintf(stderr, "failed parsing streampp key value '%s' (not a valid number or streampp name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'w':
            /* Try to parse as number first */
            if (sscanf(opts.optarg, "%u", &cmd.devicepp_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                cmd.devicepp_kv.value = get_device_pp_rx_value(opts.optarg);
                if (cmd.devicepp_kv.value == 0) {
                    fprintf(stderr, "failed parsing devicepp key value '%s' (not a valid number or devicepp name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'z':
            /* Try to parse as number first */
            if (sscanf(opts.optarg, "%u", &cmd.device_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                cmd.device_kv.value = get_device_rx_value(opts.optarg);
                if (cmd.device_kv.value == 0) {
                    fprintf(stderr, "failed parsing device key value '%s' (not a valid number or device name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
            }
            break;
        case 'i':
            /* Try to parse as number first */
            if (sscanf(opts.optarg, "%u", &cmd.instance_kv.value) != 1) {
                /* If not a number, try to lookup as string */
                cmd.instance_kv.value = get_instance_value(opts.optarg);
                if (cmd.instance_kv.value == 0) {
                    fprintf(stderr, "failed parsing instance key value '%s' (not a valid number or instance name)\n", opts.optarg);
                    return EXIT_FAILURE;
                }
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

    if (cmd.backend_name == NULL) {
        fprintf(stderr, "Missing required argument backend-name\n");
        return EXIT_FAILURE;
    }

    if (init_ctx(&ctx, &cmd) < 0) {
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }

    if (init_hw_mixer(cmd.physical_card, MIXER_PATHS) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }

    if (set_hw_mixer_path(hw_mixer_path) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }

    if (init_agm_mixer(cmd.virtual_card, cmd.frontend_name, cmd.backend_name, 
        (char *)BACKEND_CONF_FILE, cmd.stream_kv, cmd.instance_kv, 
        cmd.streampp_kv, cmd.devicepp_kv, cmd.device_kv ) < 0) {
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }

    if (configure_agm(cmd.physical_card, cmd.physical_device, cmd.config.period_count, 
        cmd.frame_size_fcr) < 0) {
        cleanup_agm();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }

    if (init_pcm(&ctx, &cmd) < 0) {    
        cleanup_agm();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);;
        return EXIT_FAILURE;
    }

    if (cmd.config.format == PCM_FORMAT_FLOAT_LE) {
        printf("floating-point PCM\n");
    } 
    else {
        printf("signed PCM\n");
    }

    if (start_audio(&ctx) < 0) {
        cleanup_pcm(ctx.pcm);
        cleanup_agm();
        cleanup_hw_mixer();
        cleanup_ctx(&ctx);
        cleanup_cmd(&cmd);
        return EXIT_FAILURE;
    }
    
    cleanup_pcm(ctx.pcm);
    cleanup_agm();
    cleanup_hw_mixer();
    cleanup_ctx(&ctx);
    cleanup_cmd(&cmd);

    return EXIT_SUCCESS;
}

int audio_loop(struct pcm_ctx *ctx)
{
    const struct pcm_config *config;
    int ret = 0;
    int written_frames;

    config = pcm_get_config(ctx->pcm);
    if (config == NULL) {
        fprintf(stderr, "unable to get pcm config\n");
        return -1;
    }

    if (pcm_start(ctx->pcm) < 0) {
        fprintf(stderr, "PCM start error\n");
        return -1;
    }

    struct audio_ctx actx = create_audio_ctx(ctx);  // Must declare AND initialize together
    if(setup(&actx, NULL) != 0) {
        fprintf(stderr, "setup function failed\n");
        cleanup(&actx, NULL);
        pcm_stop(ctx->pcm);
        return -2;
    }

    // catch ctrl-c to shutdown cleanly
    signal(SIGINT, stream_close);

    //------------------------
    // actual audio loop
    while(!should_stop) {
        render(&actx, NULL);
        
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

    cleanup(&actx, NULL);
    // don't call pcm_drain(ctx->pcm), it will seg-fault!
    pcm_stop(ctx->pcm);

    return ret;
}

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