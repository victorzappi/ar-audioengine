/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// passthrough: copy the first capture channel to every playback channel.
//
// Run full-duplex (capture must be active, i.e. do NOT pass -u/--no-capture).
// Buffers are interleaved as sample[frame*channels + chn]. This assumes capture
// and playback share the same channel count (audio_ctx exposes a single
// `channels`, taken from the playback config) and period size.

#include <stdio.h>
#include "render.h"

int setup(struct audio_ctx *ctx, void *user_data)
{
    if (ctx->input_buffer == nullptr) {
        fprintf(stderr, "passthrough needs capture active: run full-duplex (drop -u/--no-capture)\n");
        return -1;
    }
    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    const unsigned int channels = ctx->channels;

    for (unsigned int n = 0; n < ctx->period_size; n++) {
        // first capture channel of this frame
        float sample = ctx->input_buffer[n * channels];

        // fan it out to every playback channel
        for (unsigned int chn = 0; chn < channels; chn++)
            ctx->audio_buffer[n * channels + chn] = sample;
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    return;
}
