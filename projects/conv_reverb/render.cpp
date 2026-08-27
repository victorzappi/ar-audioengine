/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// Convolution reverb: loops a mono/stereo input audio file through a
// fixed-IR FIR filter (reverb_ir_f32.h) and writes the result to the output
// buffer.
//
// The input audio file is not bundled with the source -- copy it into the
// current working directory you launch the engine from (not necessarily
// the executable's own directory -- see kInputFile below) before running.

#include "render.h"
#include "reverb_ir_f32.h"
#include "AudioFile.h"
#include "Fir.h"

#include <vector>
#include <cstdio>

static const char *kInputFile = "dry_percussions.wav";
static const float kInputGain = 0.4f;   // the IR's coefficients sum to ~82 in
                                        // absolute value, so input may clip if full-scale

static Fir fir;

// one entry per output channel; channels beyond the input audio file's own
// stay empty (rendered as silence)
static std::vector<std::vector<float>> fileData;
static unsigned int fileFrames = 0;
static unsigned int usedChannels = 0;
static unsigned int readPos = 0;

// de-interleaved per-period scratch, allocated once here so render() never
// allocates; scratchPtrs mirrors scratch's storage for Fir::process()
static std::vector<std::vector<float>> scratch;
static std::vector<float *> scratchPtrs;

int setup(struct audio_ctx *ctx, void *user_data)
{
    if (ctx->sample_rate != REVERB_IR_SAMPLERATE) {
        fprintf(stderr, "conv_reverb: project sample rate (%u Hz) does not match the impulse response's rate (%u Hz)\n",
                ctx->sample_rate, (unsigned int)REVERB_IR_SAMPLERATE);
        return -1;
    }

    std::vector<std::vector<float>> loaded = AudioFileUtilities::load(kInputFile);
    if (loaded.empty() || loaded[0].empty()) {
        fprintf(stderr, "conv_reverb: failed to load audio file '%s' (expected in the current working directory)\n", kInputFile);
        return -1;
    }

    unsigned int fileChannels = (unsigned int)loaded.size();
    usedChannels = (fileChannels < ctx->channels) ? fileChannels : ctx->channels;
    fileFrames = (unsigned int)loaded[0].size();
    readPos = 0;

    fileData.assign(ctx->channels, std::vector<float>());
    for (unsigned int c = 0; c < usedChannels; c++)
        fileData[c] = std::move(loaded[c]);

    scratch.assign(ctx->channels, std::vector<float>(ctx->period_size, 0.0f));
    scratchPtrs.resize(ctx->channels);
    for (unsigned int c = 0; c < ctx->channels; c++)
        scratchPtrs[c] = scratch[c].data();

    if (fir.setup(REVERB_IR_NUM_TAPS, ctx->channels, ctx->period_size) != 0) {
        fprintf(stderr, "conv_reverb: Fir::setup failed\n");
        return -1;
    }
    if (fir.setCoefficients(kReverbIr, REVERB_IR_NUM_TAPS) != 0) {
        fprintf(stderr, "conv_reverb: Fir::setCoefficients failed\n");
        return -1;
    }

    printf("conv_reverb: loaded '%s' (%u channel(s), %u frames), using %u of %u output channel(s)\n",
           kInputFile, fileChannels, fileFrames, usedChannels, ctx->channels);

    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    unsigned int pos = readPos;

    for (unsigned int c = 0; c < ctx->channels; c++) {
        float *out = scratchPtrs[c];

        if (c < usedChannels) {
            const float *src = fileData[c].data();
            unsigned int p = pos;
            for (unsigned int n = 0; n < ctx->period_size; n++) {
                out[n] = src[p] * kInputGain;
                if (++p == fileFrames)
                    p = 0;
            }
        } else {
            for (unsigned int n = 0; n < ctx->period_size; n++)
                out[n] = 0.0f;
        }
    }
    readPos = (pos + ctx->period_size) % fileFrames;

    fir.process(scratchPtrs.data(), ctx->period_size);

    for (unsigned int n = 0; n < ctx->period_size; n++)
        for (unsigned int c = 0; c < ctx->channels; c++)
            ctx->audio_out[n * ctx->channels + c] = scratchPtrs[c][n];
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
}
