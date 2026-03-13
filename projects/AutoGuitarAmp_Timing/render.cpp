

#include <RTNeural/RTNeural.h>
#include <string>
#include "render.h"
#include "MonoFilePlayer.h"
#include <fstream>
#include <chrono>

std::string filename = "715794__guitarman213__clean-electric-guitar_mono_norm_48k.wav";
std::string modelFileName = "model_best.json";

RTNeural::ModelT<float, 1, 1,
        RTNeural::LSTMLayerT<float, 1, 20>,
        RTNeural::DenseT<float, 20, 1>> model;


MonoFilePlayer player;

std::vector<unsigned long long> inferenceTimes;

int logPtr = 0;

int numLogs;

int outputSize = 1;

//Not entirely sure what to set this to. I would usually put the length of the .wav input file. However, there
//is a loop parameter that could make the testing duration indefinite.
constexpr int testDuration_sec = 10;


int setup (struct audio_ctx *ctx, void *user_data)
{
    bool loop = true;
    bool autostart = true;

    if (player.setup(filename, loop, autostart) == 0)
    {
        printf("Error loading audio file '%s'\n", filename.c_str());
        return false;
    }

    std::cout << "Loading model from path: " << modelFileName << std::endl;
    std::ifstream jsonStream(modelFileName, std::ifstream::binary);
    nlohmann::json modelJson;
    jsonStream >> modelJson;

    //Layer 0: LSTM (rec) layer
    //Layer 1: Dense (lin) layer
    auto& lstm_layer = model.get<0>();
    RTNeural::torch_helpers::loadLSTM<float>(modelJson, "rec.", lstm_layer);
    
    auto& dense_layer = model.get<1>();
    RTNeural::torch_helpers::loadDense<float>(modelJson, "lin.", dense_layer);

    model.reset();

    size_t totalSize = ctx->sample_rate * testDuration_sec * 1.01f; // add 1% margin, in case strean_close() request lags
    inferenceTimes.reserve(totalSize); // reserve() allocates heap memory and prevents push_back() from re-allocating, if within capacity
    numLogs = (ctx->sample_rate * testDuration_sec) / outputSize;

    return true;

}


void render (struct audio_ctx *ctx, void *user_data)
{
    //This array only as 1 element, it is pretty much acting like a dereference operator
    float output[] = {0};

    for(int n = 0; n<ctx->period_size; n++) 
    {
        float original = player.process();

        const float input[] = {original};

        auto start_time = std::chrono::high_resolution_clock::now();

        output[0] = model.forward(input) + input[0];

        auto end_time = std::chrono::high_resolution_clock::now();

        inferenceTimes.push_back(std::chrono::duration_cast
                                <std::chrono::nanoseconds>(end_time - start_time).count());

        logPtr += 1;

        for (int c = 0; c < ctx->channels; c++) 
        {
            ctx->audio_buffer[(n * ctx->channels) + c] = output[0];
        }

        if (logPtr >= numLogs)
        {
            printf("Test duration (%d s) elapsed, stopping now. Total number of logs: %d\n", testDuration_sec, logPtr);
            stream_close();
        }
    }
}

void cleanup (struct audio_ctx *ctx, void *user_data)
{
    std::string timingDir = ".";
    std::string timingFileName = "inferenceTimes_" + modelFileName + "_" + std::to_string(outputSize) + "_rtneural.txt";
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