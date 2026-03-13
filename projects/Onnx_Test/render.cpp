#include "render.h"
#include <OrtModel.h>
#include <cmath> // sin

float frequency = 440.0;
float amplitude = 0.6;

std::string modelType = "onnx";
std::string modelName = "model";
int numInputSamples = 1;

//------------------------------------------------
float phase;
float inverseSampleRate;

OrtModel ortModel(true);

float input[1] = {0};
float output[1];


int setup(struct audio_ctx *ctx, void *userData) 
{
    // Initialize ONNX Runtime Model Wrapper
    std::string modelPath = "./"+modelName+"."+modelType;
    if (!ortModel.setup("session1", modelPath)) // ORT session name, path to onnx model
        printf("unable to setup ortModel");


    // for sine
    inverseSampleRate = 1.0 / ctx->sample_rate;
	phase = 0.0;

    return true;
}

void render(struct audio_ctx *ctx, void *userData) 
{
    for(int n=0; n< ctx->period_size; n++) 
    {

        if( (n%32) == 0)
        {
            input[0] = 0.5; 
            
            //I'm assuming we ingore the output as the onnx.model file does not amount
            //to anything. The purpose of this .cpp is to ensure that our dependencies and dynmically compiled
            //library work correctly.
            ortModel.run(input, output); 
        }

    
		float out = amplitude * sinf(phase);
		phase += 2.0f * (float)M_PI * frequency * inverseSampleRate;
		while(phase > 2.0f *M_PI)
			phase -= 2.0f * (float)M_PI;
		
		for(int chn=0; chn<ctx->channels; chn++)
            ctx->audio_buffer[(n * ctx->channels) + chn] = out;
  }
}

void cleanup(struct audio_ctx *ctx, void *userData) 
{
    ortModel.cleanup();
}