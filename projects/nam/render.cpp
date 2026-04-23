/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * NAM WaveNet example for ar-audioengine
 * Processes a guitar DI recording through a Neural Amp Modeler WaveNet model.
 *
 * Uses NeuralAmpModelerCore to load a .nam file and run block-based inference.
 * The model is purely convolutional (no recurrence), so it processes
 * a full audio block at a time.
 *
 * Model: Full Rig Peavey 5150 No boost Mesa OS SM57
 * Source: TONE3000 (https://www.tone3000.com)
 *
 * This code example uses this sound from freesound:
 * Clean Electric Guitar by guitarman213
 * https://freesound.org/s/715794/ -- License: Creative Commons 0
 * The original file has been normalized, exported as a mono track
 * and resampled at 48 kHz.
 */

#include "render.h"
#include "MonoFilePlayer.h"
#include "NAM/get_dsp.h"
#include <filesystem>
#include <chrono>
#include <string>


// by default, all files must be in the same location from where the executable is launched

// Audio file to process
std::string audiofilePath = "./715794__guitarman213__clean-electric-guitar_mono_norm_48k.wav";

// NAM model file
std::string modelFilePath = "./Full Rig Peavey 5150 No boost Mesa OS SM57 - jp_is_out_of_tune.nam";

float volume = 0.75;

//------------------------------------

MonoFilePlayer player;
std::unique_ptr<nam::DSP> model;

// Buffers for block-based NAM processing
double *inputBuffer = nullptr;
double *outputBuffer = nullptr;
double *inputPtr = nullptr;
double *outputPtr = nullptr;

int setup(struct audio_ctx *ctx, void *user_data) 
{
    // Load the audio file
    if (!player.setup(audiofilePath, true, true)) {
        printf("Error loading audio file '%s'\n", audiofilePath.c_str());
        return false;
    }

    // Load the NAM model from .nam file
    model = nam::get_dsp(std::filesystem::path(modelFilePath));
    if(model == nullptr) {
        printf("Error loading NAM model '%s'\n", modelFilePath.c_str());
        return false;
    }

    // Initialize the model with sample rate and block size
    model->Reset(ctx->sample_rate, ctx->period_size);
    model->prewarm();

    printf("NAM model loaded: %s\n", modelFilePath.c_str());
    printf("Sample rate: %d, Block size: %d\n",
           ctx->sample_rate, ctx->period_size);

    // Allocate processing buffers (one block each)
    inputBuffer = new double[ctx->period_size];
    outputBuffer = new double[ctx->period_size];
    inputPtr = inputBuffer;
    outputPtr = outputBuffer;

    return 0;
}

void render(struct audio_ctx *ctx, void *userData)
{
    // Fill input buffer from file player
    for (unsigned int n=0; n<ctx->period_size; n++)
        inputBuffer[n] = (double)player.process();

    // Process the entire block through NAM (expects double**)
    model->process(&inputPtr, &outputPtr, ctx->period_size);

    /*
    static int printCounter = 0;
    if (++printCounter >= (int)(ctx->sample_rate / ctx->period_size))
    {
        float blockBudgetUs = ctx->sample_rate / ctx->period_size * 1e6f;
        float load = inferUs / blockBudgetUs * 100.0f;
        fprintf(stderr, "Inference: %.0f us | Budget: %.0f us | Load: %.1f%%\n",
            inferUs, blockBudgetUs, load);
        printCounter = 0;
    }
    */
    // Write output to both channels
    for (unsigned int n=0; n<ctx->period_size; n++) {
        float out = (float)(outputBuffer[n] * volume);
        //float out = (float)(inputBuffer[n] * volume);

        for (unsigned int chn=0; chn<ctx->channels; chn++)
            ctx->audio_buffer[(ctx->channels * n) + chn] = out;
    }
}

void cleanup(struct audio_ctx *context, void *userData)
{
    delete[] inputBuffer;
    delete[] outputBuffer;
    model.reset();
}