/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <iostream>
#include <memory>
#include <string>
#include <math.h>
#include <cstdlib>

// QNN SDK
#include <Logger.hpp>

// QNN library
#include "QnnModel.h"

// AR includes
#include "optparse.h"
#include "render.h"

using namespace qnn::tools;

int longIndex = 0;
int opt = 0;
// App parameters set by CLI args
std::string backendPath;
QnnLog_Level_t logLevel{QNN_LOG_LEVEL_ERROR};
std::string systemLibraryPath;
std::string dlcPath;

std::unique_ptr<QnnModel> model;

// App-specific variables
// This project assumes a graph with a single model which has only one input and one output,
// though that isn't always the case.
const int g_graphIdx = 0;
const int g_inputIdx = 0;
const int g_outputIdx = 0;

// For each graph, dedicate an array of float* to store input/output data to be copied to/from that graph's IO tensors.
// Each float* will contain the elements of an n-dimensional tensor, flattened.
float **g_inputDataBuffers = nullptr;
float **g_outputDataBuffers = nullptr;

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
        << "  --dlc-model        <FILE>   Path to the DLC file containing the model.\n"
        << "                              Requires --qnn-system to be specified.\n"
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
    // short-form may easily clash with the manu arguments dealt with by main
    enum
    {
        OPT_DLC_MODEL = 256,
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
        {"dlc-model", OPT_DLC_MODEL, OPTPARSE_REQUIRED},
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
        case OPT_DLC_MODEL:
        {
            char *dlcPathC = strdup(opts.optarg);
            if (dlcPathC == NULL)
            {
                fprintf(stderr, "failed parsing DLC model path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            dlcPath = dlcPathC;
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
            // accept any QNN log level directly (1=ERROR .. 5=DEBUG); the numeric
            // values map 1:1 to QnnLog_Level_t, so no per-level switch is needed
            unsigned int logLevel_i;
            if (sscanf(opts.optarg, "%u", &logLevel_i) != 1)
            {
                fprintf(stderr, "failed parsing log level '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            if (logLevel_i < QNN_LOG_LEVEL_ERROR || logLevel_i > QNN_LOG_LEVEL_DEBUG)
            {
                fprintf(stderr, "invalid log level '%u' (must be 1=ERROR .. 5=DEBUG)\n", logLevel_i);
                std::exit(EXIT_FAILURE);
            }
            logLevel = (QnnLog_Level_t)logLevel_i;
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
    // Initialize the QNN logger - this lets us use the `QNN_INFO/WARN/ERROR` macros
    // This logger will also be used in the QNN backend.
    if (!qnn::log::initializeLogging())
    {
        std::cerr << "ERROR: Unable to initialize logging!\n";
        return EXIT_FAILURE;
    }

    if (!qnn::log::setLogLevel(logLevel))
    {
        std::cerr << "ERROR: invalid log level given";
        return EXIT_FAILURE;
    }

    processCommandLine((char **)user_data);
    model =
        std::unique_ptr<QnnModel>(new QnnModel(
            backendPath,
            dlcPath,
            systemLibraryPath));

    if (nullptr == model)
    {
        return EXIT_FAILURE;
    }

    QNN_INFO("Setting up model");
    model->setup();

    const auto &inputTensors = model->getInputTensors(g_graphIdx);
    const auto &outputTensors = model->getOutputTensors(g_graphIdx);

    std::vector<size_t> inputDimensions = inputTensors[g_inputIdx].dimensions;
    std::vector<size_t> outputDimensions = outputTensors[g_outputIdx].dimensions;

    /*
        Make sure the model's IO dimensions are what we expect.
        This specific project expects a model to take amplitude and phase inputs in batches of period_size (i.e. dimension [period_size, 2])
        and output an equal batch of samples (i.e. dimension [period_size, 1]).
        This will vary depending on the model you use.
    */
    if (inputDimensions.size() != 2 || inputDimensions[0] != ctx->period_size || inputDimensions[1] != 2)
    {
        std::cerr << "Given model has incorrect input dimensions (expected [" << ctx->period_size << ", 2]): [ ";
        for (const size_t &dim : inputDimensions)
        {
            std::cerr << dim << " ";
        }
        std::cerr << "]\n";
        return EXIT_FAILURE;
    }

    if (outputDimensions.size() != 2 || outputDimensions[0] != ctx->period_size || outputDimensions[1] != 1)
    {
        std::cerr << "Given model has incorrect output dimensions (expected [" << ctx->period_size << "]): [ ";
        for (const size_t &dim : outputDimensions)
        {
            std::cerr << dim << " ";
        }
        std::cerr << "]\n";
        return EXIT_FAILURE;
    }

    // Allocate flat buffers for a graph based on the input or output tensors for that graph
    QNN_INFO("Preparing IO buffers");
    if (iotensor::StatusCode::SUCCESS !=
        iotensor::allocateIOFloatBuffer(model->getInputTensors(g_graphIdx), g_inputDataBuffers))
    {
        QNN_ERROR("Input buffer allocation failure");
        return EXIT_FAILURE;
    }

    if (iotensor::StatusCode::SUCCESS !=
        iotensor::allocateIOFloatBuffer(model->getOutputTensors(g_graphIdx), g_outputDataBuffers))
    {
        QNN_ERROR("Output buffer allocation failure");
        return EXIT_FAILURE;
    }

    phase_inc = 2.0f * M_PI * frequency / (float)(ctx->sample_rate);

    return EXIT_SUCCESS;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    /**
     * General flow for working with QNN models:
     * 1. Write float inputs into an input buffer for a graph
     * 2. Pass that input buffer, along with an output float buffer, to `executeGraph()`
     *    This will populate the output buffer with the model's outputs
     * 3. Copy output buffer data into audio context's audio buffer
     */
    
    for (size_t frame = 0; frame < ctx->period_size; ++frame)
    {
        /**
         * The QNN SDK (and by extension, QnnModel) expects data in row-major order (the same way numpy stores by default)
         * In other words, the last dimension varies the fastest, and the first dimension varies the slowest
        **/ 
        const size_t inputOffset = frame * 2; // 2 is the number of input features we have
        g_inputDataBuffers[g_inputIdx][inputOffset] = amplitude;
        g_inputDataBuffers[g_inputIdx][inputOffset + 1] = phase;

        phase = fmod(phase + phase_inc, 2.0f * M_PI);
    }

    // Run the model with your inputs and write outputs to output buffer
    if (StatusCode::SUCCESS != model->executeGraph(g_graphIdx, g_inputDataBuffers, g_outputDataBuffers))
    {
        QNN_ERROR("Load And Execute failure");
        return;
    }

    // Write outputs to audio buffer
    for (size_t frame = 0; frame < ctx->period_size; frame++)
    {
        const float sample = g_outputDataBuffers[g_outputIdx][frame];
        for (unsigned int channel = 0; channel < ctx->channels; channel++)
        {
            ctx->audio_buffer[(frame * ctx->channels) + channel] = sample;
        }
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    // Free input/output float buffers
    if (g_inputDataBuffers != nullptr)
    {
        const auto &inputTensors = model->getInputTensors(g_graphIdx);
        for (size_t i = 0; i < inputTensors.size(); ++i)
        {
            free(g_inputDataBuffers[i]);
        }
        free(g_inputDataBuffers);
        g_inputDataBuffers = nullptr;
    }

    if (g_outputDataBuffers != nullptr)
    {
        const auto &outputTensors = model->getOutputTensors(g_graphIdx);
        for (size_t i = 0; i < outputTensors.size(); ++i)
        {
            free(g_outputDataBuffers[i]);
        }
        free(g_outputDataBuffers);
        g_outputDataBuffers = nullptr;
    }

    // Free all other model resources
    if (model != nullptr)
    {
        if (StatusCode::SUCCESS != model->cleanup())
        {
            QNN_ERROR("Failure cleaning up model - some resources may not have been freed!");
        }
        model.reset();
    }
}
