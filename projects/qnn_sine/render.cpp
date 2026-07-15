/*
 * Copyright 2026 Victor Zappi, Avery Huang
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <math.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// clean-room QNN wrapper (public-API only)
#include "QnnModel.h"

// AR includes
#include "optparse.h"
#include "render.h"

// App parameters set by CLI args
std::string backendPath;
std::string systemLibraryPath;
std::string modelPath;
int logLevel = 1; // 1=ERROR .. 5=DEBUG

std::unique_ptr<ar::qnn::QnnModel> model;

// App-specific variables
// This project assumes a graph with a single model which has only one input and one output,
// though that isn't always the case.
const uint32_t g_graphIdx = 0;
const int g_inputIdx = 0;
const int g_outputIdx = 0;

// One flattened float buffer per input/output tensor of the graph.
float **g_inputDataBuffers = nullptr;
float **g_outputDataBuffers = nullptr;
size_t g_numInputs = 0;
size_t g_numOutputs = 0;

// This project uses the model to generate a periodic wave.
float frequency = 440;
float amplitude = 0.8;
float phase = 0.0;
float phase_inc;

void showHelp()
{
    std::cout
        << "\nDESCRIPTION:\n"
        << "------------\n"
        << "Sample application demonstrating how to load and execute a neural network\n"
        << "using QNN APIs.\n"
        << "\n\n"
        << "REQUIRED ARGUMENTS:\n"
        << "-------------------\n"
        << "  --qnn-backend      <FILE>   Path to a QNN backend to execute the model.\n"
        << "\n"
        << "  --qnn-model        <FILE>   Path to the model: a .dlc container or a\n"
        << "                              .bin context binary. Requires --qnn-system.\n"
        << "\n"
        << "  --qnn-system       <FILE>   Path to the QNN System library (libQnnSystem.so),\n"
        << "                              needed when loading a model from a DLC.\n"
        << "\n\n"
        << "OPTIONAL ARGUMENTS:\n"
        << "-------------------\n"
        << "  --freq             <VAL>    Set oscillator frequency in Hz. Defaults to 440.\n"
        << "\n"
        << "  --amp              <VAL>    Set oscillator amplitude (0.0-1.0). Defaults to 0.8.\n"
        << "\n"
        << "  --log-level        <1-5>    QNN log level: 1=ERROR, 2=WARN, 3=INFO,\n"
        << "                              4=VERBOSE, 5=DEBUG. Defaults to ERROR.\n"
        << "\n"
        << "  --project-help              Show this help message.\n"
        << std::endl;
}

void processCommandLine(char **argv)
{
    // long-only option ids: start past the ASCII range so optparse treats them as
    // long-form only, i.e. there are no single-char short options
    // short-form may easily clash with the many arguments dealt with by main
    enum
    {
        OPT_QNN_MODEL = 256,
        OPT_QNN_BACKEND,
        OPT_QNN_SYSTEM,
        OPT_LOG_LEVEL,
        OPT_FREQ,
        OPT_AMP,
        OPT_PROJECT_HELP,
    };

    int c;
    struct optparse opts;
    struct optparse_long long_options[] = {
        {"qnn-model", OPT_QNN_MODEL, OPTPARSE_REQUIRED},
        {"qnn-backend", OPT_QNN_BACKEND, OPTPARSE_REQUIRED},
        {"qnn-system", OPT_QNN_SYSTEM, OPTPARSE_REQUIRED},
        {"log-level", OPT_LOG_LEVEL, OPTPARSE_REQUIRED},
        {"freq", OPT_FREQ, OPTPARSE_REQUIRED},
        {"amp", OPT_AMP, OPTPARSE_REQUIRED},
        {"project-help", OPT_PROJECT_HELP, OPTPARSE_NONE},
        {0, 0, OPTPARSE_NONE}};

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, long_options, NULL)) != -1)
    {
        switch (c)
        {
        case OPT_QNN_MODEL:
        {
            char *modelPathC = strdup(opts.optarg);
            if (modelPathC == NULL)
            {
                fprintf(stderr, "failed parsing model path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            modelPath = modelPathC;
            break;
        }
        case OPT_QNN_BACKEND:
        {
            char *backendPathC = strdup(opts.optarg);
            if (backendPathC == NULL)
            {
                fprintf(stderr, "failed parsing QNN backend path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            backendPath = backendPathC;
            break;
        }
        case OPT_QNN_SYSTEM:
        {
            char *systemPathC = strdup(opts.optarg);
            if (systemPathC == NULL)
            {
                fprintf(stderr, "failed parsing system library path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            systemLibraryPath = systemPathC;
            break;
        }
        case OPT_LOG_LEVEL:
        {
            // accept any QNN log level directly (1=ERROR .. 5=DEBUG)
            unsigned int logLevel_i;
            if (sscanf(opts.optarg, "%u", &logLevel_i) != 1)
            {
                fprintf(stderr, "failed parsing log level '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            if (logLevel_i < 1 || logLevel_i > 5)
            {
                fprintf(stderr, "invalid log level '%u' (must be 1=ERROR .. 5=DEBUG)\n", logLevel_i);
                std::exit(EXIT_FAILURE);
            }
            logLevel = (int)logLevel_i;
            break;
        }
        case OPT_FREQ:
        {
            if (sscanf(opts.optarg, "%f", &frequency) != 1)
            {
                fprintf(stderr, "failed parsing frequency '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            break;
        }
        case OPT_AMP:
        {
            if (sscanf(opts.optarg, "%f", &amplitude) != 1)
            {
                fprintf(stderr, "failed parsing amplitude '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            break;
        }
        case OPT_PROJECT_HELP:
            showHelp();
            std::exit(EXIT_SUCCESS);
            break;
        case '?':
            std::cerr << "ERROR: Invalid argument passed: " << argv[opts.optind - 1]
                      << "\nPlease check the Arguments section in the description below.\n";
            showHelp();
            std::exit(EXIT_FAILURE);
            break;
        }
    }
}

int setup(struct audio_ctx *ctx, void *user_data)
{
    processCommandLine((char **)user_data);

    if (modelPath.empty() || backendPath.empty() || systemLibraryPath.empty())
    {
        std::cerr << "qnn_sine: --qnn-model, --qnn-backend and --qnn-system are all required\n";
        return EXIT_FAILURE;
    }

    model.reset(new ar::qnn::QnnModel(backendPath, modelPath, systemLibraryPath));
    if (!model->load(logLevel))
    {
        std::cerr << "qnn_sine: failed to load model\n";
        return EXIT_FAILURE;
    }

    const std::vector<ar::qnn::TensorInfo> &inputs = model->inputs(g_graphIdx);
    const std::vector<ar::qnn::TensorInfo> &outputs = model->outputs(g_graphIdx);

    /*
        Make sure the model's IO dimensions are what we expect.
        This specific project expects a model to take amplitude and phase inputs in batches of
        period_size (i.e. dimension [period_size, 2]) and output an equal batch of samples
        (i.e. dimension [period_size, 1]). This will vary depending on the model you use.
    */
    const std::vector<uint32_t> &inDims = inputs[g_inputIdx].dims;
    const std::vector<uint32_t> &outDims = outputs[g_outputIdx].dims;

    if (inDims.size() != 2 || inDims[0] != ctx->period_size || inDims[1] != 2)
    {
        std::cerr << "Given model has incorrect input dimensions (expected [" << ctx->period_size << ", 2]): [ ";
        for (uint32_t dim : inDims)
            std::cerr << dim << " ";
        std::cerr << "]\n";
        return EXIT_FAILURE;
    }
    if (outDims.size() != 2 || outDims[0] != ctx->period_size || outDims[1] != 1)
    {
        std::cerr << "Given model has incorrect output dimensions (expected [" << ctx->period_size << ", 1]): [ ";
        for (uint32_t dim : outDims)
            std::cerr << dim << " ";
        std::cerr << "]\n";
        return EXIT_FAILURE;
    }

    // Allocate one flat float buffer per input/output tensor.
    g_numInputs = inputs.size();
    g_numOutputs = outputs.size();
    g_inputDataBuffers = (float **)calloc(g_numInputs, sizeof(float *));
    g_outputDataBuffers = (float **)calloc(g_numOutputs, sizeof(float *));
    if (!g_inputDataBuffers || !g_outputDataBuffers)
        return EXIT_FAILURE;
    for (size_t i = 0; i < g_numInputs; ++i)
        g_inputDataBuffers[i] = (float *)calloc(inputs[i].numElements, sizeof(float));
    for (size_t i = 0; i < g_numOutputs; ++i)
        g_outputDataBuffers[i] = (float *)calloc(outputs[i].numElements, sizeof(float));

    phase_inc = 2.0f * M_PI * frequency / (float)(ctx->sample_rate);

    return EXIT_SUCCESS;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    // 1. write float inputs (amplitude, phase) into the input buffer, row-major:
    //    the last dimension (2 features) varies fastest.
    for (size_t frame = 0; frame < ctx->period_size; ++frame)
    {
        const size_t inputOffset = frame * 2; // 2 input features
        g_inputDataBuffers[g_inputIdx][inputOffset] = amplitude;
        g_inputDataBuffers[g_inputIdx][inputOffset + 1] = phase;

        phase = fmod(phase + phase_inc, 2.0f * M_PI);
    }

    // 2. run the model
    if (!model->execute(g_graphIdx, g_inputDataBuffers, g_outputDataBuffers))
        return;

    // 3. write the model outputs to the audio buffer (same sample to every channel)
    for (size_t frame = 0; frame < ctx->period_size; ++frame)
    {
        const float sample = g_outputDataBuffers[g_outputIdx][frame];
        for (unsigned int channel = 0; channel < ctx->channels; ++channel)
            ctx->audio_buffer[(frame * ctx->channels) + channel] = sample;
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    if (g_inputDataBuffers != nullptr)
    {
        for (size_t i = 0; i < g_numInputs; ++i)
            free(g_inputDataBuffers[i]);
        free(g_inputDataBuffers);
        g_inputDataBuffers = nullptr;
    }
    if (g_outputDataBuffers != nullptr)
    {
        for (size_t i = 0; i < g_numOutputs; ++i)
            free(g_outputDataBuffers[i]);
        free(g_outputDataBuffers);
        g_outputDataBuffers = nullptr;
    }

    // releases the backend, context, model and libraries (see QnnModel destructor)
    model.reset();
}
