/*
 * Copyright 2026 Victor Zappi, Avery Huang
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// qnn_osc: batches a running amplitude/phase pair over a whole period into a
// QNN model that approximates a sinusoidal oscillator, and writes the
// result to every channel.
//
// Unlike the other examples, the model isn't loaded from a fixed path in
// the source -- it's passed at runtime via --qnn-model (plus --qnn-backend
// and --qnn-system), so it must be present on the target device when you
// run it. Two variants are bundled here, full_oscillator_256x2.dlc and
// full_oscillator_512x2.dlc, each with a fixed batch size (256 or 512)
// baked in at export time; pick the one whose batch size matches the
// engine's --period-size.

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <math.h>
#include <time.h>
#include <cstdint>
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

// Profiling (--profile / --dump). All buffers are allocated in setup(), so
// render() only writes into them; stats and the dump are produced in cleanup().
float profileSeconds = 0; // recording window; 0 = profiling off
std::string dumpPath;
std::string timesPath;
const size_t g_warmupPeriods = 10; // excluded from the steady-state stats
size_t g_maxPeriods = 0;
size_t g_periodIdx = 0;
std::vector<uint64_t> g_execNs;        // execute() duration per period
std::vector<uint64_t> g_periodStartNs; // render() entry time per period
std::vector<float> g_dump;             // model output, period after period
double g_loadMs = 0;

static uint64_t nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

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
        << "  --profile          <SEC>    Time load() and every execute() for the first SEC\n"
        << "                              seconds; prints a stats line at exit.\n"
        << "\n"
        << "  --dump             <FILE>   Write the model output of the --profile window to\n"
        << "                              FILE as raw float32 at exit. Requires --profile.\n"
        << "\n"
        << "  --times            <FILE>   Write every execute() duration of the --profile\n"
        << "                              window to FILE as raw uint64 nanoseconds at exit.\n"
        << "                              Requires --profile.\n"
        << "\n"
        << "  --project-help              Show this help message.\n"
        << std::endl;
}

void processCommandLine(char **argv)
{
    // long-only option ids: start past the ASCII range so optparse treats them as
    // long-form only, i.e. there are no single-char short options
    // short-form may easily clash with the many arguments dealt with by main
    enum {
        OPT_QNN_MODEL = 256,
        OPT_QNN_BACKEND,
        OPT_QNN_SYSTEM,
        OPT_LOG_LEVEL,
        OPT_FREQ,
        OPT_AMP,
        OPT_PROFILE,
        OPT_DUMP,
        OPT_TIMES,
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
        {"profile", OPT_PROFILE, OPTPARSE_REQUIRED},
        {"dump", OPT_DUMP, OPTPARSE_REQUIRED},
        {"times", OPT_TIMES, OPTPARSE_REQUIRED},
        {"project-help", OPT_PROJECT_HELP, OPTPARSE_NONE},
        {0, 0, OPTPARSE_NONE}};

    optparse_init(&opts, argv);
    while ((c = optparse_long(&opts, long_options, NULL)) != -1) {
        switch (c) {
        case OPT_QNN_MODEL: {
            char *modelPathC = strdup(opts.optarg);
            if (modelPathC == NULL) {
                fprintf(stderr, "failed parsing model path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            modelPath = modelPathC;
            break;
        }
        case OPT_QNN_BACKEND: {
            char *backendPathC = strdup(opts.optarg);
            if (backendPathC == NULL) {
                fprintf(stderr, "failed parsing QNN backend path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            backendPath = backendPathC;
            break;
        }
        case OPT_QNN_SYSTEM: {
            char *systemPathC = strdup(opts.optarg);
            if (systemPathC == NULL) {
                fprintf(stderr, "failed parsing system library path '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            systemLibraryPath = systemPathC;
            break;
        }
        case OPT_LOG_LEVEL: {
            // accept any QNN log level directly (1=ERROR .. 5=DEBUG)
            unsigned int logLevel_i;
            if (sscanf(opts.optarg, "%u", &logLevel_i) != 1) {
                fprintf(stderr, "failed parsing log level '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            if (logLevel_i < 1 || logLevel_i > 5) {
                fprintf(stderr, "invalid log level '%u' (must be 1=ERROR .. 5=DEBUG)\n", logLevel_i);
                std::exit(EXIT_FAILURE);
            }
            logLevel = (int)logLevel_i;
            break;
        }
        case OPT_FREQ: {
            if (sscanf(opts.optarg, "%f", &frequency) != 1) {
                fprintf(stderr, "failed parsing frequency '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            break;
        }
        case OPT_AMP: {
            if (sscanf(opts.optarg, "%f", &amplitude) != 1) {
                fprintf(stderr, "failed parsing amplitude '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            break;
        }
        case OPT_PROFILE: {
            if (sscanf(opts.optarg, "%f", &profileSeconds) != 1 || profileSeconds <= 0) {
                fprintf(stderr, "failed parsing profile duration '%s'\n", opts.optarg);
                std::exit(EXIT_FAILURE);
            }
            break;
        }
        case OPT_DUMP:
            dumpPath = opts.optarg;
            break;
        case OPT_TIMES:
            timesPath = opts.optarg;
            break;
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

    if (modelPath.empty() || backendPath.empty() || systemLibraryPath.empty()) {
        std::cerr << "qnn_osc: --qnn-model, --qnn-backend and --qnn-system are all required\n";
        return EXIT_FAILURE;
    }
    if ((!dumpPath.empty() || !timesPath.empty()) && profileSeconds <= 0) {
        std::cerr << "qnn_osc: --dump and --times require --profile\n";
        return EXIT_FAILURE;
    }

    model.reset(new ar::qnn::QnnModel(backendPath, modelPath, systemLibraryPath));
    const uint64_t loadStart = nowNs();
    if (!model->load(logLevel)) {
        std::cerr << "qnn_osc: failed to load model\n";
        return EXIT_FAILURE;
    }
    g_loadMs = (nowNs() - loadStart) / 1e6;

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

    if (inDims.size() != 2 || inDims[0] != ctx->period_size || inDims[1] != 2) {
        std::cerr << "Given model has incorrect input dimensions (expected [" << ctx->period_size << ", 2]): [ ";
        for (uint32_t dim : inDims)
            std::cerr << dim << " ";
        std::cerr << "]\n";
        return EXIT_FAILURE;
    }
    if (outDims.size() != 2 || outDims[0] != ctx->period_size || outDims[1] != 1) {
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

    if (profileSeconds > 0) {
        g_maxPeriods = (size_t)ceil(profileSeconds * ctx->sample_rate / ctx->period_size);
        g_execNs.assign(g_maxPeriods, 0);
        g_periodStartNs.assign(g_maxPeriods, 0);
        if (!dumpPath.empty())
            g_dump.assign(g_maxPeriods * ctx->period_size, 0.0f);
    }

    // warmup execution, because first execute on .bin files on GPU has unusually high latency:
    for (size_t frame = 0; frame < ctx->period_size; ++frame) {
        const size_t inputOffset = frame * 2; // 2 input features
        g_inputDataBuffers[g_inputIdx][inputOffset] = 0.0f;
        g_inputDataBuffers[g_inputIdx][inputOffset + 1] = 0.0f;
    }
    const bool ok = model->execute(g_graphIdx, g_inputDataBuffers, g_outputDataBuffers);

    return EXIT_SUCCESS;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    const bool recording = g_periodIdx < g_maxPeriods;
    if (recording)
        g_periodStartNs[g_periodIdx] = nowNs();

    // 1. write float inputs (amplitude, phase) into the input buffer, row-major:
    //    the last dimension (2 features) varies fastest.
    for (size_t frame = 0; frame < ctx->period_size; ++frame) {
        const size_t inputOffset = frame * 2; // 2 input features
        g_inputDataBuffers[g_inputIdx][inputOffset] = amplitude;
        g_inputDataBuffers[g_inputIdx][inputOffset + 1] = phase;

        phase = fmod(phase + phase_inc, 2.0f * M_PI);
    }

    // 2. run the model
    const uint64_t execStart = recording ? nowNs() : 0;
    const bool ok = model->execute(g_graphIdx, g_inputDataBuffers, g_outputDataBuffers);
    if (recording) {
        g_execNs[g_periodIdx] = nowNs() - execStart;
        if (!g_dump.empty())
            memcpy(&g_dump[g_periodIdx * ctx->period_size], g_outputDataBuffers[g_outputIdx],
                   ctx->period_size * sizeof(float));
        ++g_periodIdx;
    }
    if (!ok)
        return;

    // 3. write the model outputs to the audio buffer (same sample to every channel)
    for (size_t frame = 0; frame < ctx->period_size; ++frame) {
        const float sample = g_outputDataBuffers[g_outputIdx][frame];
        for (unsigned int channel = 0; channel < ctx->channels; ++channel)
            ctx->audio_out[(frame * ctx->channels) + channel] = sample;
    }
}

// Prints one "key=value" line (times in microseconds) so runs can be parsed by script.
static void printProfile(struct audio_ctx *ctx)
{
    const size_t n = g_periodIdx;
    const double budgetUs = 1e6 * ctx->period_size / ctx->sample_rate;
    printf("[qnn_osc profile] model=%s backend=%s period=%u rate=%u load_ms=%.3f budget_us=%.1f periods=%zu",
           modelPath.c_str(), backendPath.c_str(), ctx->period_size, ctx->sample_rate, g_loadMs, budgetUs, n);

    if (n > 0) {
        double warmMax = 0;
        for (size_t i = 1; i < std::min(n, g_warmupPeriods + 1); ++i)
            warmMax = std::max(warmMax, g_execNs[i] / 1e3);
        printf(" first_us=%.1f warmup_max_us=%.1f", g_execNs[0] / 1e3, warmMax);
    }

    if (n > g_warmupPeriods + 1) {
        std::vector<double> us;
        size_t over50 = 0, over100 = 0;
        double sum = 0, sumSq = 0, maxGapUs = 0;
        for (size_t i = g_warmupPeriods + 1; i < n; ++i) {
            const double t = g_execNs[i] / 1e3;
            us.push_back(t);
            sum += t;
            sumSq += t * t;
            over50 += t > 0.5 * budgetUs;
            over100 += t > budgetUs;
            maxGapUs = std::max(maxGapUs, (g_periodStartNs[i] - g_periodStartNs[i - 1]) / 1e3);
        }
        std::sort(us.begin(), us.end());
        auto pct = [&](double p) { return us[(size_t)(p * (us.size() - 1))]; };
        const double mean = sum / us.size();
        const double std = sqrt(std::max(0.0, sumSq / us.size() - mean * mean));
        printf(" n=%zu min_us=%.1f mean_us=%.1f std_us=%.1f median_us=%.1f p99_us=%.1f p999_us=%.1f max_us=%.1f"
               " over50=%zu over100=%zu max_gap_us=%.1f",
               us.size(), us.front(), mean, std, pct(0.5), pct(0.99), pct(0.999), us.back(),
               over50, over100, maxGapUs);
    }
    printf("\n");

    // every period, warm-up included, so the analysis can choose what to drop
    if (!timesPath.empty()) {
        FILE *f = fopen(timesPath.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "qnn_osc: cannot open times file '%s'\n", timesPath.c_str());
        } else {
            if (fwrite(g_execNs.data(), sizeof(uint64_t), n, f) != n)
                fprintf(stderr, "qnn_osc: short write to times file '%s'\n", timesPath.c_str());
            fclose(f);
        }
    }

    if (!g_dump.empty()) {
        FILE *f = fopen(dumpPath.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "qnn_osc: cannot open dump file '%s'\n", dumpPath.c_str());
            return;
        }
        const size_t samples = n * ctx->period_size;
        if (fwrite(g_dump.data(), sizeof(float), samples, f) != samples)
            fprintf(stderr, "qnn_osc: short write to dump file '%s'\n", dumpPath.c_str());
        fclose(f);
        printf("[qnn_osc profile] dumped %zu samples to %s\n", samples, dumpPath.c_str());
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    if (g_maxPeriods > 0)
        printProfile(ctx);

    if (g_inputDataBuffers != nullptr) {
        for (size_t i = 0; i < g_numInputs; ++i)
            free(g_inputDataBuffers[i]);
        free(g_inputDataBuffers);
        g_inputDataBuffers = nullptr;
    }
    if (g_outputDataBuffers != nullptr) {
        for (size_t i = 0; i < g_numOutputs; ++i)
            free(g_outputDataBuffers[i]);
        free(g_outputDataBuffers);
        g_outputDataBuffers = nullptr;
    }

    // releases the backend, context, model and libraries (see QnnModel destructor)
    model.reset();
}
