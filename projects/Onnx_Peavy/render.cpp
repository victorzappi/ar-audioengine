#include "render.h"
#include "OrtModel.h"
#include <cstring>
#include "MonoFilePlayer.h"

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

    return true;
}

void render(struct audio_ctx *ctx, void *userData)
{
    // ── Build input: [lookback | new samples] ──
    memcpy(inputBuf, lookBack, sizeof(float) * RECEPTIVE_FIELD);
    for (int i = 0; i < BLOCK_SIZE; i++)
        inputBuf[RECEPTIVE_FIELD + i] = player.process();

    // ── Run model ──
    model.run(inputBuf, outputBuf);

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
}

void cleanup(struct audio_ctx *ctx, void *userData)
{
    model.cleanup();
}