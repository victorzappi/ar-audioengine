/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// passthrough: copy the first capture channel to every playback channel.
//
// Buffers are interleaved: audio_in[frame * in_channels + chn] (capture) and
// audio_out[frame * channels + chn] (playback).

#include <stdio.h>
#include "render.h"

int setup(struct audio_ctx *ctx, void *user_data)
{
    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    const unsigned int channels = ctx->channels;

    for (unsigned int n = 0; n < ctx->period_size; n++) {
        // first capture channel of this frame
        float sample = ctx->audio_in[n * ctx->in_channels];

        // fan it out to every playback channel
        for (unsigned int chn = 0; chn < channels; chn++)
            ctx->audio_out[n * channels + chn] = sample;
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    return;
}
