

#include <RTNeural/RTNeural.h>
#include <string>
#include "render.h"
#include "MonoFilePlayer.h"

std::string filename = "715794__guitarman213__clean-electric-guitar_mono_norm_48k.wav";
std::string modelFileName = "model_best.json";

RTNeural::ModelT<float, 1, 1,
        RTNeural::LSTMLayerT<float, 1, 20>,
        RTNeural::DenseT<float, 20, 1>> model;

RTNeural::ModelT<float, 2, 1,
        RTNeural::LSTMLayerT<float, 2, 20>,
        RTNeural::DenseT<float, 20, 1>> model_cond1;


MonoFilePlayer player;
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

    return true;

}


void render (struct audio_ctx *ctx, void *user_data)
{
    //This array only as 1 element, it is pretty much acting like a dereference operator
    float output[] = {0};

    for(int n = 0; n<ctx->period_size; n++) {
        float original = player.process();

        const float input[] = {original};
        output[0] = model.forward(input) + input[0];

        for (int c = 0; c < ctx->channels; c++) {
            ctx->audio_buffer[(n * ctx->channels) + c] = output[0];
        }
    }
}

void cleanup (struct audio_ctx *ctx, void *user_data)
{
    return;
}