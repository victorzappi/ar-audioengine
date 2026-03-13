#include "render.h"
#include "OrtModel.h"
#include <cstring>  // memcpy
#include "MonoFilePlayer.h"
#include <fstream>
#include <chrono>

// ── Model config ─────────────────────────────────────────────────────────────
OrtModel model;
const std::string modelName = "nam_wavenet_128";
const int BLOCK_SIZE = 128;
const int RECEPTIVE_FIELD = 4092;
const int INPUT_LENGTH = RECEPTIVE_FIELD + BLOCK_SIZE;

// ── File player ──────────────────────────────────────────────────────────────
const std::string wavFileName = "323623__shpira__tech-drums_mono.wav";
MonoFilePlayer player;

// ── Pre-allocated buffers (not on stack, not in real-time thread) ─────────────
float inputBuf[INPUT_LENGTH] = {0};   // [lookback | new block]
float outputBuf[INPUT_LENGTH] = {0};  // full model output
float lookBack[RECEPTIVE_FIELD] = {0}; // persistent lookback state

// -- Timing setup
std::vector<unsigned long long> inferenceTimes;

int logPtr = 0;

int numLogs;

int outputSize = 128;

constexpr int testDuration_sec = 10;

int setup(struct audio_ctx *context, void *userData)
{
    // Period size must match the ONNX model's expected block size
    if (context->period_size != BLOCK_SIZE)
    {
        printf("Error: period size (%d) must be %d to match ONNX model\n",
               context->period_size, BLOCK_SIZE);
        return false;
    }

    // Load ONNX model
    std::string modelPath = "./" + modelName + ".onnx";
    if (!model.setup("nam_wavenet", modelPath))
    {
        printf("Error: unable to load model %s\n", modelPath.c_str());
        return false;
    }

    // Load wav file
    if (player.setup(wavFileName, true, true) == 0)
    {
        printf("Error loading audio file '%s'\n", wavFileName.c_str());
        return false;
    }

    size_t totalSize = context->sample_rate * testDuration_sec * 1.01f; // add 1% margin, in case strean_close() request lags
    inferenceTimes.reserve(totalSize); // reserve() allocates heap memory and prevents push_back() from re-allocating, if within capacity
    numLogs = (context->sample_rate * testDuration_sec) / outputSize;

    return true;
}

void audio_callback(float *in, float *out, float *lookBack) 
{
    float inputBuf[RECEPTIVE_FIELD + BLOCK_SIZE];
    float outputBuf[RECEPTIVE_FIELD + BLOCK_SIZE];
    memcpy(inputBuf, lookBack, sizeof(float) * RECEPTIVE_FIELD);
    memcpy(inputBuf + RECEPTIVE_FIELD, in, sizeof(float) * BLOCK_SIZE);
    model.run(inputBuf, outputBuf);
    memcpy(out, outputBuf + RECEPTIVE_FIELD, sizeof(float) * BLOCK_SIZE);
    memcpy(lookBack, inputBuf + BLOCK_SIZE, sizeof(float) * RECEPTIVE_FIELD);
}

void render(struct audio_ctx *ctx, void *userData)
{
    // ── Build input: [lookback | new samples] ──
    memcpy(inputBuf, lookBack, sizeof(float) * RECEPTIVE_FIELD);
    for (int i = 0; i < BLOCK_SIZE; i++)
        inputBuf[RECEPTIVE_FIELD + i] = player.process();

    // ── Run model ──
    auto start_time = std::chrono::high_resolution_clock::now();

    model.run(inputBuf, outputBuf);

    auto end_time = std::chrono::high_resolution_clock::now();

    inferenceTimes.push_back(std::chrono::duration_cast
        <std::chrono::nanoseconds>(end_time - start_time).count());

    logPtr += 1;

    // ── Extract valid output (last BLOCK_SIZE samples) ──
    // ── Write to interleaved audio buffer ──
    for (int i = 0; i < BLOCK_SIZE; i++)
    {
        float sample = outputBuf[RECEPTIVE_FIELD + i];
        for (int chan = 0; chan < ctx->channels; chan++)
            ctx->audio_buffer[i * ctx->channels + chan] = sample;
    }

    // ── Slide lookback: last RECEPTIVE_FIELD samples of input ──
    memcpy(lookBack, inputBuf + BLOCK_SIZE, sizeof(float) * RECEPTIVE_FIELD);

    if (logPtr >= numLogs)
    {
        printf("Test duration (%d s) elapsed, stopping now. Total number of logs: %d\n", testDuration_sec, logPtr);
        stream_close();
    }
}


void cleanup(struct audio_ctx *ctx, void *userData) 
{
    
    model.cleanup();

    std::string timingDir = ".";
    std::string timingFileName = "inferenceTimes_" + modelName + "_" + std::to_string(outputSize) + "onnx.txt";
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