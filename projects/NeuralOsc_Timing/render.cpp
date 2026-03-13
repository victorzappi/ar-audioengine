/*
 * [2-Clause BSD License]
 *
 * Copyright 2022 Victor Zappi
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <RTNeural/RTNeural.h>
#include <string>
#include "render.h"
#include <fstream>
#include <chrono>

float frequency = 300;
float amplitude = 1.0;

//This file must be in the same location as the executable on the board
std::string modelFileName = "2i-512-relu_gaussian_p=0.5_r=0.5_pruned409.json";

float phase;
float inverseSampleRate;

const int hidden_size = 409;

RTNeural::ModelT<float, 2, 1,
    RTNeural::DenseT<float, 2, hidden_size>,
    RTNeural::ReLuActivationT<float, hidden_size>,
    RTNeural::DenseT<float, hidden_size, 1> > model;


int outputSize = 1;

std::vector<unsigned long long> inferenceTimes;

constexpr int testDuration_sec = 10;

int numLogs;

int logPtr = 0;

int setup(struct audio_ctx *ctx, void *user_data)
{
    inverseSampleRate = 1.0 / (float)(ctx->sample_rate);
    phase = 0.0;

    std::cout << "Loading model from path: " << modelFileName << std::endl;
    std::ifstream jsonStream(modelFileName, std::ifstream::binary);
    nlohmann::json modelJson;
    jsonStream >> modelJson;

    // Layer indices: 0 = first dense, 1 = ReLU (no weights), 2 = second dense.
    auto& inputLayer = model.get<0>();
    RTNeural::torch_helpers::loadDense<float>(modelJson, "input_layer.", inputLayer);

    auto& outputLayer = model.get<2>();
    RTNeural::torch_helpers::loadDense<float>(modelJson, "output_layer.", outputLayer);

    model.reset();

    size_t totalSize = ctx->sample_rate * testDuration_sec * 1.01f; // add 1% margin, in case strean_close() request lags
    inferenceTimes.reserve(totalSize); // reserve() allocates heap memory and prevents push_back() from re-allocating, if within capacity
    numLogs = (ctx->sample_rate * testDuration_sec) / outputSize;

    return true;
}

void render(struct audio_ctx *ctx, void *userData)
{
    
	for(int n=0; n<ctx->period_size; n++)
	{
        float in[2] = {amplitude, phase};

        auto start_time = std::chrono::high_resolution_clock::now();

        float out = model.forward(in);

        auto end_time = std::chrono::high_resolution_clock::now();

        inferenceTimes.push_back(std::chrono::duration_cast
                                <std::chrono::nanoseconds>(end_time - start_time).count());

        logPtr += 1;

		phase += 2.0f * (float)M_PI * frequency * inverseSampleRate;

		while(phase > 2.0f * (float)M_PI)
			phase -= 2.0f * (float)M_PI;
		
		for(int chn=0; chn<ctx->channels; chn++)
            ctx->audio_buffer[(n * ctx->channels) + chn] = out;

        if (logPtr >= numLogs)
        {
            printf("Test duration (%d s) elapsed, stopping now. Total number of logs: %d\n", testDuration_sec, logPtr);
            stream_close();
        }

	}
}

void cleanup(struct audio_ctx *ctx, void *userData)
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
