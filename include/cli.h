/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __CLI_H__
#define __CLI_H__

#include <tinyalsa/asoundlib.h>  // struct pcm_config, PCM_OUT / PCM_IN, enum pcm_format
#include <agm/agm_api.h>         // struct agm_key_value

// one audio direction (playback or capture): device, routing names, mixer path,
// graph keys, and pcm config. params shared across both directions live in
// struct settings.
struct pcm_stream {
    unsigned int virtual_device;   // CLI -d / -D
    unsigned int physical_device;  // CLI -e / -E
    char *frontend_name;           // CLI -t / -T; else auto from (virtual_card, virtual_device)
    char *backend_name;            // CLI -k / -K; else auto from (physical_card, physical_device, dir)
    char *mixer_path;              // hardware mixer path (-o playback / -O capture)
    int flags;                     // PCM_OUT / PCM_IN -- also encodes direction

    unsigned int bits;             // sample bit-depth (-b playback / -B capture)
    bool is_float;                 // floating-point PCM (-f playback / -F capture)

    struct pcm_config config;      // per-stream pcm params (channels, rate, period...)

    // graph params: CLI args set the kv *values*, the keys are fixed
    struct agm_key_value stream_kv;
    struct agm_key_value instance_kv;
    struct agm_key_value streampp_kv;
    struct agm_key_value devicepp_kv;
    struct agm_key_value device_kv;
};

// engine configuration. shared fields are command-line args; per-direction fields
// live in the pcm_stream(s).
struct settings {
    // shared across both directions
    unsigned int virtual_card;     // CLI -c
    unsigned int physical_card;    // CLI -s
    bool full_duplex;              // defaults on; -u/--no-capture disables capture
    bool echo_reference;           // defaults off; -a/--echo-reference enables the
                                   // capture<-playback echo reference (only if capture is active)

    struct pcm_stream playback;    // PCM_OUT
    struct pcm_stream capture;     // PCM_IN

    // args main didn't recognize (plus trailing positionals), forwarded to the
    // project as setup/render/cleanup's user_data. NULL-terminated, argv-style
    // with the program name in slot 0 so a project's own optparse works as usual.
    char **user_argv;
};

// set defaults for both streams; call before parse_cli
void init_settings(struct settings *settings);

// free everything init_settings/parse_cli allocated in settings
void cleanup_settings(struct settings *settings);

// parse argv into settings. returns 0 to continue, >0 if help was shown
// (caller should exit success), <0 on error (caller should exit failure).
int parse_cli(int argc, char **argv, struct settings *settings);

#endif //__CLI_H__
