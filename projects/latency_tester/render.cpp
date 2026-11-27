// This code is based on the code credited below, but it has been modified
// further by Victor Zappi (LDSP's latency_tester, ported to ar-audioengine)

/*
 ____  _____ _        _
| __ )| ____| |      / \
|  _ \|  _| | |     / _ \
| |_) | |___| |___ / ___ \
|____/|_____|_____/_/   \_\

http://bela.io

C++ Real-Time Audio Programming with Bela - Lecture 10: Latency
latency-tester-audio: Measures the latency from audio output to audio input
*/

// latency_tester: once a second, plays a 20 ms pulse on every playback channel and
// counts the samples until the capture input crosses a threshold: the round trip,
// output to input. Without line in/out, point a speaker at the mic: the result then
// includes the sound's travel through the air (29 us per cm).
//
// Every second it also prints the input's peak (the loudest sample after the pulse)
// and its noise (the loudest sample in the quiet end of the second), to set the
// threshold: above the noise, below the peak.
//
// Options (after the engine's): --threshold <level>, the input level that ends the
// count (default 0.1); the input counts as quiet again below half of it.
//
// render() runs on the real-time thread, so it never prints: results go through a
// small queue to a printer thread, on the other CPUs.

#include <atomic>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "render.h"

// Constants that define the program behaviour
const float kTestsPerSecond = 1.0;  // How many tests to run per second
const float kPulseLength = .02;     // Length of the output pulse
const float kNoiseWindow = .2;      // The quiet end of each second, where the noise is measured

// Input thresholds: look for an edge from below the "lo" to above the "hi"
static float gInputHiThreshold = 0.1;
static float gInputLoThreshold = 0.05;

// Global variables
unsigned int gTestInterval = 0;     // Samples between successive tests
unsigned int gPulseLength = 0;      // How long is the pulse in samples?
unsigned int gNoiseStart = 0;       // First sample of the noise window
unsigned int gTestCounter = 0;      // How many samples since the last test?
bool gInTest = false;               // Are we running a test?
bool gInputAboveThreshold = false;  // Is the input high or low?
bool gStarted = false;              // Has the first test started?
int gLatency = -1;                  // Samples to the edge in this test, -1 if none yet
float gPeak = 0;                    // Loudest input sample in this test
float gNoise = 0;                   // Loudest input sample in this test's noise window

// one line of results per test, from render() to the printer thread
struct result {
    int latency;  // samples, -1: no edge
    float peak;
    float noise;
};
static const unsigned int kQueueSize = 64;
static struct result gQueue[kQueueSize];
static std::atomic<unsigned int> gQueueWrite(0), gQueueRead(0);
static std::atomic<bool> gPrinterStop(false);
static pthread_t gPrinter;
static bool gPrinterRunning = false;
static unsigned int gBufferingSamples = 0;  // the engine's part of the round trip
static float gRate = 48000;

static float to_dbfs(float level)
{
    return 20.0f * log10f(level > 1e-9f ? level : 1e-9f);
}

static void *printer(void *arg)
{
    while (!gPrinterStop.load()) {
        while (gQueueRead.load() != gQueueWrite.load()) {
            const struct result *r = &gQueue[gQueueRead.load() % kQueueSize];
            if (r->latency >= 0) {
                int hardwareSamples = r->latency - (int)gBufferingSamples;
                printf("Latency: %.2fms (%d samples) - buff: %.2fms (%u samples) - hw: %.2fms (%d samples)",
                       1000.0 * r->latency / gRate, r->latency,
                       1000.0 * gBufferingSamples / gRate, gBufferingSamples,
                       1000.0 * hardwareSamples / gRate, hardwareSamples);
            }
            else {
                printf("No edge above %.3f", gInputHiThreshold);
            }
            printf(" | peak %.4f (%.1f dBFS) | noise %.4f (%.1f dBFS)\n", r->peak, to_dbfs(r->peak),
                   r->noise, to_dbfs(r->noise));
            fflush(stdout);
            gQueueRead.store(gQueueRead.load() + 1);
        }
        usleep(50000);
    }
    return nullptr;
}

// the printer runs as a normal thread on the CPUs other than the audio thread's: it
// must not inherit the audio thread's priority or its CPU
static int start_printer(void)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    struct sched_param param = {};
    pthread_attr_setschedparam(&attr, &param);
    int audio_cpu = sched_getcpu();
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (audio_cpu >= 0 && num_cpus > 1) {
        cpu_set_t cpus;
        CPU_ZERO(&cpus);
        for (long c = 0; c < num_cpus; c++)
            if (c != audio_cpu)
                CPU_SET(c, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpus), &cpus);
    }
    int rc = pthread_create(&gPrinter, &attr, printer, nullptr);
    pthread_attr_destroy(&attr);
    return rc;
}

int setup(struct audio_ctx *ctx, void *user_data)
{
    // --threshold <level>, among the options the engine forwards
    char **argv = (char **)user_data;
    for (int i = 1; argv && argv[i]; i++) {
        if (strcmp(argv[i], "--threshold") == 0 && argv[i + 1]) {
            gInputHiThreshold = atof(argv[++i]);
            gInputLoThreshold = gInputHiThreshold / 2;
        }
    }
    if (gInputHiThreshold <= 0 || gInputHiThreshold > 1) {
        fprintf(stderr, "latency_tester: the threshold must be in (0, 1]\n");
        return -1;
    }

    // Initialise the number of samples between successive pulses
    // and the length of the pulse
    gTestInterval = ctx->sample_rate / kTestsPerSecond;
    gPulseLength = ctx->sample_rate * kPulseLength;
    gNoiseStart = gTestInterval - (unsigned int)(ctx->sample_rate * kNoiseWindow);
    gRate = (float)ctx->sample_rate;
    // the engine's part: the block captured in one period is played two periods later
    gBufferingSamples = 2 * ctx->period_size;

    printf("latency_tester: a pulse every %.1f s, threshold %.3f (%.1f dBFS), %u-frame blocks\n",
           1.0 / kTestsPerSecond, gInputHiThreshold, to_dbfs(gInputHiThreshold), ctx->period_size);
    if (start_printer() != 0) {
        fprintf(stderr, "latency_tester: could not start the printer thread\n");
        return -1;
    }
    gPrinterRunning = true;
    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    // Loop through the number of audio frames
    for (unsigned int n = 0; n < ctx->period_size; n++) {
        float input = 0;
        float output = 0;

        // Average the audio inputs
        for (unsigned int ch = 0; ch < ctx->in_channels; ch++)
            input += fabsf(ctx->audio_in[n * ctx->in_channels + ch]);
        input /= (float)ctx->in_channels;

        // Increment the test counter and, when it reaches the interval,
        // start a new test
        if (++gTestCounter >= gTestInterval) {
            // Reset event counter to 0
            gTestCounter = 0;

            // Report the test that ends (a missing edge shows as latency -1)
            if (gStarted) {
                unsigned int w = gQueueWrite.load();
                if (w - gQueueRead.load() < kQueueSize) {
                    gQueue[w % kQueueSize] = { gLatency, gPeak, gNoise };
                    gQueueWrite.store(w + 1);
                }
            }
            gStarted = true;
            gLatency = -1;
            gPeak = 0;
            gNoise = 0;

            // Init the next Pulse
            gInTest = true;
        }

        if (gStarted) {
            if (input > gPeak)
                gPeak = input;
            if (gTestCounter >= gNoiseStart && input > gNoise)
                gNoise = input;
        }

        // If we are counting samples, then look for a rising edge above the threshold
        if (gInTest) {
            if (!gInputAboveThreshold && input >= gInputHiThreshold) {
                // Edge found: stop counting samples
                gInTest = false;
                gInputAboveThreshold = true;
                gLatency = (int)gTestCounter;
            }
        }

        // Generate the pulse if we are still in its window
        if (gStarted && gTestCounter < gPulseLength)
            output = 1.0 - (float)gTestCounter / (float)gPulseLength;
        else
            output = 0.0;

        // If the input falls below the lower threshold, set the flag accordingly
        if (input < gInputLoThreshold)
            gInputAboveThreshold = false;

        // Write the output to all the channels
        for (unsigned int ch = 0; ch < ctx->channels; ch++)
            ctx->audio_out[n * ctx->channels + ch] = output;
    }
}

void cleanup(struct audio_ctx *ctx, void *user_data)
{
    if (gPrinterRunning) {
        gPrinterStop.store(true);
        pthread_join(gPrinter, nullptr);
        gPrinterRunning = false;
    }
}
