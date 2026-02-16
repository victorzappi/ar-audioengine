// Audio context structure (high-level audio buffer processing)
struct audio_ctx {
    float * const audio_buffer;  // const pointer (address cannot change), but data can be modified
    const unsigned int period_size;
    const unsigned int channels;
    const unsigned int sample_rate;
};


int setup(struct audio_ctx *ctx, void *user_data);

void render(struct audio_ctx *ctx, void *user_data);

void cleanup(struct audio_ctx *ctx, void *user_data);