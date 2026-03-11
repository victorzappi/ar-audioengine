/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

// This code example uses this sound from freesound:
// Simple Lofi Vinyl E-Piano Loop 95 BPM.wav by holizna -- https://freesound.org/s/629178/ -- License: Creative Commons 0
// the original file has been exported as a mono track and resampled at 48 kHz

#include "render.h"
#include "MonoFilePlayer.h"
#include <string>

// drum loop has to have same samplerate as project!
std::string filename = "629178__holizna__simple-lofi-vinyl-e-piano-loop-95-bpm_mono_48k.wav";
//-------------------------------------------

MonoFilePlayer player;


int setup(struct audio_ctx *ctx, void *user_data) 
{
	bool loop = true;
	bool autostart = true;
 	
	// load the audio file
	if( !player.setup(filename, loop, autostart) ) 
	{
    	printf("Error loading audio file '%s'\n", filename.c_str());
    	return 1;
	}

    return 0;
}

void render(struct audio_ctx *ctx, void *user_data)
{
    for (unsigned int n=0; n<ctx->period_size; n++) {
		// get next sample from file
		float sample = player.process(); 

 		for (unsigned int c=0; c<ctx->channels; c++)
            ctx->audio_buffer[n*ctx->channels + c] = sample;
	}
}

void cleanup(struct audio_ctx *ctx, void *user_data) 
{

}