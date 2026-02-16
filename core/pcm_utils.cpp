/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <string.h>
#include <sound/asound.h>

#include "pcm_utils.h"

unsigned int bits_to_sndrv_format(unsigned int bits)
{
    switch (bits) {
    case 32:
        return SNDRV_PCM_FORMAT_S32_LE;
    case 8:
        return SNDRV_PCM_FORMAT_S8;
    case 24:
        return SNDRV_PCM_FORMAT_S24_3LE;
    default:
    case 16:
        return SNDRV_PCM_FORMAT_S16_LE;
    };
}

unsigned int alsa_to_sndrv_format(enum pcm_format fmt)
{
    switch (fmt) {
    case PCM_FORMAT_S32_LE:
        return SNDRV_PCM_FORMAT_S32_LE;
    case PCM_FORMAT_S8:
        return SNDRV_PCM_FORMAT_S8;
    case PCM_FORMAT_S24_3LE:
        return SNDRV_PCM_FORMAT_S24_3LE;
    case PCM_FORMAT_S24_LE:
        return SNDRV_PCM_FORMAT_S24_LE;
    default:
    case PCM_FORMAT_S16_LE:
        return SNDRV_PCM_FORMAT_S16_LE;
    };
}

enum pcm_format get_pcm_format(const char *str)
{
    if (!strncmp(str, "PCM_FORMAT_S16_LE", strlen("PCM_FORMAT_S16_LE"))) {
        return PCM_FORMAT_S16_LE;
    } else if (!strncmp(str, "PCM_FORMAT_S32_LE", strlen("PCM_FORMAT_S32_LE"))) {
        return PCM_FORMAT_S32_LE;
    } else if (!strncmp(str, "PCM_FORMAT_S8", strlen("PCM_FORMAT_S8"))) {
        return PCM_FORMAT_S8;
    } else if (!strncmp(str, "PCM_FORMAT_S24_LE", strlen("PCM_FORMAT_S24_LE"))) {
        return PCM_FORMAT_S24_LE;
    } else if (!strncmp(str, "PCM_FORMAT_S24_3LE", strlen("PCM_FORMAT_S24_3LE"))) {
        return PCM_FORMAT_S24_3LE;
    } else {
        return PCM_FORMAT_INVALID;
    }
}

int format_to_signed_pcm_bits(enum pcm_format fmt_id)
{
    switch (fmt_id) {
    case PCM_FORMAT_S24_3LE:
    /*
     *This api returns the number of audio data bit width specific to the format
     *e.g. In S24_LE, even if the number of bytes is 4, the audio data is only in 3 bytes
     *Hence we return 24 as the bit_width, whereas the bitspersample for this format would
     *return 32
     */
    case PCM_FORMAT_S24_LE:
        return 24;
    case PCM_FORMAT_S32_LE:
        return 32;
    case PCM_FORMAT_S8:
        return 8;
    case PCM_FORMAT_S16_LE:
    default:
        return 16;
    }
}

enum pcm_format signed_pcm_bits_to_format(int bits)
{
    // these are based on libtinyalsa2.0.0 pcm.h
    switch(bits) {
    case 8:
        return PCM_FORMAT_S8;
    case 16:
        return PCM_FORMAT_S16_LE;
    case 24:
        return PCM_FORMAT_S24_3LE;
    case 32:
        return PCM_FORMAT_S32_LE;
    default:
        return PCM_FORMAT_INVALID;
    }
}

bool is_format_big_endian(enum pcm_format format) 
{
    // these are based on libtinyalsa2.0.0 pcm.h
    switch(format) {
    case PCM_FORMAT_S16_BE:
    case PCM_FORMAT_S24_BE:
    case PCM_FORMAT_S24_3BE:
    case PCM_FORMAT_S32_BE:
    case PCM_FORMAT_FLOAT_BE:
        return true;
    default:
        return false;
    }
}

bool is_format_float(enum pcm_format format) 
{
    // these are based on libtinyalsa2.0.0 pcm.h
    switch(format) {
    case PCM_FORMAT_FLOAT_LE:
    case PCM_FORMAT_FLOAT_BE:
        return true;
    default:
        return false;
    }
}
