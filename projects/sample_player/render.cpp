/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// sample_player: loops a mono input audio file, copying it to every output
// channel.
//
// This code example uses this sound from freesound:
// Simple Lofi Vinyl E-Piano Loop 95 BPM.wav by holizna -- https://freesound.org/s/629178/ -- License: Creative Commons 0
// the original file has been exported as a mono track and resampled at 48 kHz
//
// The input audio file is not bundled with the source -- copy it into the
// current working directory you launch the engine from (not necessarily
// the executable's own directory -- see kInputFile below) before running.

#include "render.h"
#include "AudioFile.h"

#include <vector>
#include <cstdio>

// drum loop has to have the same samplerate as the project!
static const char *kInputFile = "629178__holizna__simple-lofi-vinyl-e-piano-loop-95-bpm_mono_48k.wav";

static std::vector<float> sampleBuffer;
static unsigned int readPos = 0;

int setup(struct audio_ctx *ctx, void *user_data)
{
    sampleBuffer = AudioFileUtilities::loadMono(kInputFile);
    if (sampleBuffer.empty()) {
        fprintf(stderr, "sample_player: failed to load audio file '%s' (expected in the current working directory)\n", kInputFile);
        return -1;
    }

    readPos = 0;

    printf("sample_player: loaded '%s' (%zu frames)\n", kInputFile, sampleBuffer.size());

    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    unsigned int pos = readPos;
    unsigned int size = (unsigned int)sampleBuffer.size();

    for (unsigned int n = 0; n < ctx->period_size; n++) {
        float sample = sampleBuffer[pos];
        if (++pos == size)
            pos = 0;

        for (unsigned int c = 0; c < ctx->channels; c++)
            ctx->audio_buffer[n * ctx->channels + c] = sample;
    }

    readPos = pos;
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
}
