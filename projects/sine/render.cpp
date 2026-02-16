#include <math.h>
#include "render.h"


static const float amp = 0.5;
static const float freq = 330.0;

static float phase = 0.0;
static float phase_inc;

int setup(struct audio_ctx *ctx, void *user_data) 
{
    phase_inc = 2.0f * M_PI * freq / (float)(ctx->sample_rate);

    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    float sample;

    for (unsigned int n=0; n<ctx->period_size; n++) {
        sample = amp * sinf(phase);
        phase += phase_inc;
        while(phase > 2.0f * M_PI)
			phase -= 2.0f * M_PI;

        for (unsigned int c=0; c<ctx->channels; c++)
            ctx->audio_buffer[n*ctx->channels + c] = sample;
    }
}


void cleanup(struct audio_ctx *ctx, void *user_data) 
{
    return;
}
