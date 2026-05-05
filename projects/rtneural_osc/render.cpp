/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <RTNeural/RTNeural.h>
#include <string>
#include <math.h>
#include "render.h"

float frequency = 440;
float amplitude = 0.5;

// by default, the model file must be in the same location from where the executable is launched
std::string modelFilePath = "./1i-512-relu_p=0.5_r=0.5.json";

float phase;
float inverseSampleRate;

const int hidden_size = 512;
const int inputSize = 1;
const int outputSize = 1;

RTNeural::ModelT<float, inputSize, outputSize,
    RTNeural::DenseT<float, inputSize, hidden_size>,
    RTNeural::ReLuActivationT<float, hidden_size>,
    RTNeural::DenseT<float, hidden_size, outputSize> > model;


int setup(struct audio_ctx *ctx, void *user_data)
{
    inverseSampleRate = 1.0 / (float)(ctx->sample_rate);
    phase = 0.0;

    std::cout << "Loading model from path: " << modelFilePath << std::endl;
    std::ifstream jsonStream(modelFilePath, std::ifstream::binary);
    nlohmann::json modelJson;
    jsonStream >> modelJson;


    // Layer indices: 0 = first dense, 1 = ReLU (no weights), 2 = second dense
    auto& inputLayer = model.get<0>();
    RTNeural::torch_helpers::loadDense<float>(modelJson, "input_layer.", inputLayer);

    auto& outputLayer = model.get<2>();
    RTNeural::torch_helpers::loadDense<float>(modelJson, "output_layer.", outputLayer);

    model.reset();

    return 0;
}

void render(struct audio_ctx *ctx, void *userData)
{
    for (unsigned int n=0; n<ctx->period_size; n++) {        
        float in[1] = {phase};
        float out = amplitude*model.forward(in);

		phase += 2.0f * (float)M_PI * frequency * inverseSampleRate;
		while(phase > 2.0f * (float)M_PI)
			phase -= 2.0f * (float)M_PI;
		
        for (unsigned int chn=0; chn<ctx->channels; chn++)
            ctx->audio_buffer[(n * ctx->channels) + chn] = out;
	}
}

void cleanup(struct audio_ctx *ctx, void *userData)
{
    return;
}
