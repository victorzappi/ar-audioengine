/*
    BRAVE Streaming Timbre Transfer
    ================================
    Loads brave_acoustic_drumset_128.onnx (streaming forward model)
    and runs frame-by-frame timbre transfer from microphone input.

    ONNX I/O:
      Input:  audio (1,1,128) + 47 cache tensors
      Output: audio_out (1,1,128) + 47 cache tensors

    The temporal context is carried entirely in the cache tensors,
    so no circular buffer is needed — just 128-sample chunks.
*/

#include "render.h"
#include "OrtModel.h"
#include <cstring>
#include "MonoFilePlayer.h"

// ── Model config ─────────────────────────────────────────────────────────────
OrtModel model;
const std::string modelName = "brave_acoustic_drumset_128";
int BRAVE_BLOCK = 128;

// ── Inference buffers (allocated in setup) ───────────────────────────────────
size_t numInputs  = 0;
size_t numOutputs = 0;
float** inputs  = nullptr;
float** outputs = nullptr;

// ── File player ──────────────────────────────────────────────────────────────
const std::string wavFileName = "323623__shpira__tech-drums_mono.wav";
MonoFilePlayer player;

int setup(struct audio_ctx *context, void *userData)
{
    BRAVE_BLOCK = context->period_size;
    // Period size must match the ONNX model's block size
    if (context->period_size != BRAVE_BLOCK)
    {
        printf("Error: period size (%d) must be %d to match ONNX model\n",
               context->period_size, BRAVE_BLOCK);
        return false;
    }

    // Load model
    std::string modelPath = "./" + modelName + ".onnx";
    if (!model.setup("brave_forward", modelPath))
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

    // Allocate I/O buffers based on model metadata
    numInputs  = model.getNumInputs();
    numOutputs = model.getNumOutputs();

    inputs  = new float*[numInputs];
    outputs = new float*[numOutputs];

    for (size_t i = 0; i < numInputs; i++)
    {
        size_t sz = model.getInputSize(i);
        inputs[i] = new float[sz];
        std::memset(inputs[i], 0, sz * sizeof(float));
    }
    for (size_t i = 0; i < numOutputs; i++)
    {
        size_t sz = model.getOutputSize(i);
        outputs[i] = new float[sz];
        std::memset(outputs[i], 0, sz * sizeof(float));
    }

    printf("BRAVE timbre transfer ready\n");
    printf("  Inputs : %zu (audio + %zu caches)\n", numInputs, numInputs - 1);
    printf("  Outputs: %zu (audio + %zu caches)\n", numOutputs, numOutputs - 1);
    printf("  Block  : %d samples\n", BRAVE_BLOCK);

    return true;
}

void render(struct audio_ctx *context, void *userData)
{
    // ── Fill audio input (input[0]) ──────────────────────────────────────
    for (int i = 0; i < BRAVE_BLOCK; i++)
        inputs[0][i] = player.process();

    // ── Run inference ────────────────────────────────────────────────────
    model.run(inputs, outputs);

    // ── Write audio output to interleaved buffer ─────────────────────────
    for (int i = 0; i < BRAVE_BLOCK; i++)
    {
        for (int chan = 0; chan < context->channels; chan++)
            context->audio_buffer[(i * context->channels) + chan] = outputs[0][i];
    }

    // ── Copy output caches → input caches for next frame ─────────────────
    for (size_t c = 1; c < numInputs; c++)
    {
        size_t sz = model.getInputSize(c);
        std::memcpy(inputs[c], outputs[c], sz * sizeof(float));
    }
}

void cleanup(struct audio_ctx *context, void *userData)
{
    model.cleanup();

    if (inputs)
    {
        for (size_t i = 0; i < numInputs; i++)
            delete[] inputs[i];
        delete[] inputs;
        inputs = nullptr;
    }
    if (outputs)
    {
        for (size_t i = 0; i < numOutputs; i++)
            delete[] outputs[i];
        delete[] outputs;
        outputs = nullptr;
    }
}