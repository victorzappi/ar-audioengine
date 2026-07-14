// Audio context structure (high-level audio buffer processing)
struct audio_ctx {
    const float * const input_buffer;  // capture samples for this period (nullptr in playback-only mode)
    float * const audio_buffer;  // playback output; const pointer (address cannot change), but data can be modified  //TODO change this to .output_buffer, in all projects too
    const unsigned int period_size;
    const unsigned int channels;
    const unsigned int sample_rate;
};


int setup(struct audio_ctx *ctx, void *user_data);

void render(struct audio_ctx *ctx, void *user_data);

void cleanup(struct audio_ctx *ctx, void *user_data);

void stream_close();