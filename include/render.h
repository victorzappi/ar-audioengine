/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// Audio context structure (high-level audio buffer processing)
struct audio_ctx {
    const float * const audio_in;  // capture samples for this period: period_size * in_channels, interleaved
    float * const audio_out;  // playback output, period_size * channels, interleaved; const pointer (address cannot change), but data can be modified
    const unsigned int period_size;
    const unsigned int channels;     // playback channels
    const unsigned int sample_rate;
    const unsigned int in_channels;  // capture channels; can differ from the playback ones
};


int setup(struct audio_ctx *ctx, void *user_data);

void render(struct audio_ctx *ctx, void *user_data);

void cleanup(struct audio_ctx *ctx, void *user_data);

void stream_close();