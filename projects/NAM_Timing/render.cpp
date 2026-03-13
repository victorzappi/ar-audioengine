/*
 * NAM WaveNet example for LDSP
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
#include <fstream>
#include <string>


// Audio file to process
std::string filename = "715794__guitarman213__clean-electric-guitar_mono_norm_48k.wav";

// NAM model file
std::string modelFilename = "Kustom K150-1 (Ch1 High Input OD Boost Bright Pot Pulled).nam"; //"Full Rig Peavey 5150 No boost Mesa OS SM57 - jp_is_out_of_tune.nam";

float volume = 0.75;

//------------------------------------

MonoFilePlayer player;
std::unique_ptr<nam::DSP> model;

// -- Timing setup
std::vector<unsigned long long> inferenceTimes;

int logPtr = 0;

int numLogs;

constexpr int testDuration_sec = 10;

// Buffers for block-based NAM processing
// Buffers for block-based NAM processing
double *inputBuffer = nullptr;
double *outputBuffer = nullptr;
double *inputPtr = nullptr;
double *outputPtr = nullptr;

int setup(struct audio_ctx *context, void *userData)
{
    // Load the audio file
    if(!player.setup(filename, true, true))
    {
        printf("Error loading audio file '%s'\n", filename.c_str());
        return false;
    }

    // Load the NAM model from .nam file
    model = nam::get_dsp(std::filesystem::path(modelFilename));
    if(model == nullptr)
    {
        printf("Error loading NAM model '%s'\n", modelFilename.c_str());
        return false;
    }

    // Initialize the model with sample rate and block size
    model->Reset(context->sample_rate, context->period_size);
    model->prewarm();

    printf("NAM model loaded: %s\n", modelFilename.c_str());
    printf("Sample rate: %f, Block size: %d\n",
           context->sample_rate, context->period_size);

    // Allocate processing buffers (one block each)
    inputBuffer = new double[context->period_size];
    outputBuffer = new double[context->period_size];
    inputPtr = inputBuffer;
    outputPtr = outputBuffer;

    size_t totalSize = context->sample_rate * testDuration_sec * 1.01f; // add 1% margin, in case strean_close() request lags
    inferenceTimes.reserve(totalSize); // reserve() allocates heap memory and prevents push_back() from re-allocating, if within capacity
    numLogs = (context->sample_rate * testDuration_sec) / context->period_size;

    return true;
}

void render(struct audio_ctx *context, void *userData)
{
    // Fill input buffer from file player
    for(int n = 0; n < context->period_size; n++)
        inputBuffer[n] = (double)player.process();

    auto start_time = std::chrono::high_resolution_clock::now();
    // Process the entire block through NAM (expects double**)
    model->process(&inputPtr, &outputPtr, context->period_size);

    auto end_time = std::chrono::high_resolution_clock::now();

    inferenceTimes.push_back(std::chrono::duration_cast
        <std::chrono::nanoseconds>(end_time - start_time).count());
        
    logPtr += 1;

    /*
    static int printCounter = 0;
    if (++printCounter >= (int)(context->sample_rate / context->period_size))
    {
        float blockBudgetUs = context->sample_rate / context->period_size * 1e6f;
        float load = inferUs / blockBudgetUs * 100.0f;
        fprintf(stderr, "Inference: %.0f us | Budget: %.0f us | Load: %.1f%%\n",
            inferUs, blockBudgetUs, load);
        printCounter = 0;
    }
    */
    // Write output to both channels
    for(int n = 0; n < context->period_size; n++)
    {
        float out = (float)(outputBuffer[n] * volume);
        //float out = (float)(inputBuffer[n] * volume);

        for (int chn = 0; chn < context->channels; chn++)
        {
            context->audio_buffer[(context->channels * n) + chn] = out;
        }
    }

    if (logPtr >= numLogs)
    {
        printf("Test duration (%d s) elapsed, stopping now. Total number of logs: %d\n", testDuration_sec, logPtr);
        stream_close();
    }
}

void cleanup(struct audio_ctx *context, void *userData)
{
    delete[] inputBuffer;
    delete[] outputBuffer;
    model.reset();

    std::string timingDir = ".";
    std::string timingFileName = "inferenceTimes_" + modelFilename + "_" + std::to_string(context->period_size) + "NAM.txt";
    std::string timingFilePath = timingDir + "/" + timingFileName;

    std::ofstream logFile(timingFilePath);

    if (logFile.is_open())
    {
        for (int i = 0; i < inferenceTimes.size(); i++) 
        {
            logFile << std::to_string(inferenceTimes[i]) << "\n";
        }
    }
    logFile.close();
    printf("\nInference times written to: %s\n", timingFilePath.c_str());
    return;
}