/*
    BRAVE PCA Streaming Decoder
    ============================
    Loads brave_acoustic_drumset_pca_decoder_4_128.onnx and generates
    audio from 4 low-dimensional PCA controls.

    ONNX I/O:
      Input:  pca_input (1,4,1) + 40 cache tensors
      Output: audio (1,1,128) + 40 cache tensors

    PCA variance breakdown (from 302 wav files, ~890s of audio):
      PC1: 49.2%  — dominant timbral axis
      PC2: 32.6%  — second timbral axis
      PC3: 17.5%  — third timbral axis
      PC4:  0.2%  — near-negligible, mostly noise

    Control ranges (±3 std covers ~99.7% of training data):
      PC1: [-12.5, +12.5]  (std=4.18)
      PC2: [-10.2, +10.2]  (std=3.40)
      PC3: [ -7.5,  +7.5]  (std=2.49)
      PC4: [ -0.8,  +0.8]  (std=0.27)
*/

#include "render.h"
#include "OrtModel.h"
#include <cstring>  // memcpy
#include <cmath>
#include <chrono>

#define PROFILE_INFERENCE

// ── Model config ─────────────────────────────────────────────────────────────
OrtModel model(false);
const std::string modelName = "brave_acoustic_drumset_pca_decoder_4_1024";
const int BRAVE_BLOCK = 1024;  // output audio block size (samples)
const int N_PCA = 4;         // number of PCA control dimensions

// ── PCA control ranges (±3 std from pca_params.json) ─────────────────────────
const float pcaStd[N_PCA]      = { 3.991f,  3.280f,  2.367f,  0.208f }; //{ 4.175f,  3.400f,  2.493f,  0.273f };
const float pcaRangeMin[N_PCA] = {-11.97f, -9.84f,  -7.10f,  -0.62f }; //{-12.53f, -10.20f,  -7.48f,  -0.82f };
const float pcaRangeMax[N_PCA] = { 11.97f,  9.84f,   7.10f,   0.62f }; //{ 12.53f,  10.20f,   7.48f,   0.82f };

// ── PCA controls ─────────────────────────────────────────────────────────────
// Values are in PCA space — 0.0 = dataset mean for that component
float pcaControls[N_PCA] = {0.0f, 0.0f, 0.0f, 0.0f};

// ── LFO automation ───────────────────────────────────────────────────────────
// Four incommensurable rates so the trajectory never repeats on short timescales.
// PC1 (49% variance) moves slowest; PC4 (0.2%) moves fastest.
const float lfoRates[N_PCA]  = { 0.20f, 0.52f, 0.92f, 1.48f };  // Hz
const float lfoPhase0[N_PCA] = { 0.0f,  M_PI * 0.5f, M_PI, M_PI * 1.5f };  // start offsets
float lfoPhase[N_PCA]        = { 0.0f, 0.0f, 0.0f, 0.0f };
float lfoPhaseInc[N_PCA]     = { 0.0f, 0.0f, 0.0f, 0.0f };  // computed in setup

// ── Inference buffers (allocated in setup) ───────────────────────────────────
size_t numInputs  = 0;
size_t numOutputs = 0;
float** inputs  = nullptr;
float** outputs = nullptr;


int setup(struct audio_ctx *context, void *userData)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    // Load model
    std::string modelPath = "./" + modelName + ".onnx";
    if (!model.setup("brave_pca_dec", modelPath, false)) {
        printf("Error: unable to load model %s\n", modelPath.c_str());
        return false;
    }

    // Check period size
    if (context->period_size % BRAVE_BLOCK != 0) {
        printf("Error: period size (%d) must be a multiple of BRAVE_BLOCK (%d)!\n",
               context->period_size, BRAVE_BLOCK);
        return false;
    }

    // Allocate I/O buffers based on model metadata
    numInputs  = model.getNumInputs();
    numOutputs = model.getNumOutputs();

    inputs  = new float*[numInputs];
    outputs = new float*[numOutputs];

    for (size_t i = 0; i < numInputs; i++) {
        size_t sz = model.getInputSize(i);
        inputs[i] = new float[sz];
        std::memset(inputs[i], 0, sz * sizeof(float));
    }
    
    for (size_t i = 0; i < numOutputs; i++) {
        size_t sz = model.getOutputSize(i);
        outputs[i] = new float[sz];
        std::memset(outputs[i], 0, sz * sizeof(float));
    }

    // Compute per-period LFO phase increments and seed initial phases
    for (int i = 0; i < N_PCA; i++) {
        lfoPhaseInc[i] = 2.0f * (float)M_PI * lfoRates[i]
                         * context->period_size / (float)context->sample_rate;
        lfoPhase[i] = lfoPhase0[i];
    }

    printf("BRAVE PCA decoder ready\n");
    printf("  Inputs : %zu (pca[%d] + %zu caches)\n", numInputs, N_PCA, numInputs - 1);
    printf("  Outputs: %zu (audio[%d] + %zu caches)\n", numOutputs, BRAVE_BLOCK, numOutputs - 1);
    printf("  Period : %d samples (%d inferences per period)\n",
           context->period_size, context->period_size / BRAVE_BLOCK);
    printf("  PCA ranges (±3 std):\n");
    for (int i = 0; i < N_PCA; i++)
        printf("    PC%d: [%.2f, %.2f]  std=%.3f  LFO=%.2f Hz\n",
               i+1, pcaRangeMin[i], pcaRangeMax[i], pcaStd[i], lfoRates[i]);

    return 0;
}


void render(struct audio_ctx *ctx, void *userData)
{
    // Advance LFOs and map [-1,+1] sine output to each PCA range
    for (int i = 0; i < N_PCA; i++) {
        float mid = (pcaRangeMax[i] + pcaRangeMin[i]) * 0.5f;
        float amp = (pcaRangeMax[i] - pcaRangeMin[i]) * 0.5f;
        pcaControls[i] = mid + amp * sinf(lfoPhase[i]);
        lfoPhase[i] += lfoPhaseInc[i];
        if (lfoPhase[i] >= 2.0f * (float)M_PI)
            lfoPhase[i] -= 2.0f * (float)M_PI;
    }

    int chunksPerPeriod = ctx->period_size / BRAVE_BLOCK;

    for (int chunk = 0; chunk < chunksPerPeriod; chunk++)
    {
        int offset = chunk * BRAVE_BLOCK;

        // ── Fill PCA controls (input[0]) ─────────────────────────────────────
        for (int i = 0; i < N_PCA; i++)
            inputs[0][i] = pcaControls[i];

        // ── Run inference ────────────────────────────────────────────────────
#ifdef PROFILE_INFERENCE
        auto t0 = std::chrono::high_resolution_clock::now();
#endif
        model.run(inputs, outputs);
#ifdef PROFILE_INFERENCE
        auto t1 = std::chrono::high_resolution_clock::now();
        float inferUs = std::chrono::duration<float, std::micro>(t1 - t0).count();
        static int printCounter = 0;
        if (++printCounter >= (int)(ctx->sample_rate / ctx->period_size))
        {
            float blockBudgetUs = (float)BRAVE_BLOCK / (float)ctx->sample_rate * 1e6f;
            float load = inferUs / blockBudgetUs * 100.0f;
            fprintf(stderr, "Inference: %.0f us | Budget: %.0f us | Load: %.1f%%\n",
                    inferUs, blockBudgetUs, load);
            printCounter = 0;
        }
#endif

        // ── Write audio output ───────────────────────────────────────────────
        for (int i = 0; i < BRAVE_BLOCK; i++) {
            ctx->audio_buffer[(ctx->channels * (offset + i)) + 0] = outputs[0][i];
            ctx->audio_buffer[(ctx->channels * (offset + i)) + 1] = outputs[0][i];
        }

        // ── Copy output caches → input caches for next frame ─────────────────
        for (size_t c = 1; c < numInputs; c++) {
            size_t sz = model.getInputSize(c);
            std::memcpy(inputs[c], outputs[c], sz * sizeof(float));
        }
    }
}

void cleanup(struct audio_ctx *context, void *userData)
{
    model.cleanup();

    if (inputs) {
        for (size_t i = 0; i < numInputs; i++)
            delete[] inputs[i];
        delete[] inputs;
        inputs = nullptr;
    }
    
    if (outputs) {
        for (size_t i = 0; i < numOutputs; i++)
            delete[] outputs[i];
        delete[] outputs;
        outputs = nullptr;
    }
}
