#ifndef __PCM_UTILS_H__
#define __PCM_UTILS_H__

#include <tinyalsa/asoundlib.h>

unsigned int bits_to_sndrv_format(unsigned int bits);

unsigned int alsa_to_sndrv_format(enum pcm_format fmt);

enum pcm_format get_pcm_format(const char *str);

int format_to_signed_pcm_bits(enum pcm_format fmt_id);

enum pcm_format signed_pcm_bits_to_format(int bits);

bool is_format_big_endian(enum pcm_format format);

bool is_format_float(enum pcm_format format);


#endif //__PCM_UTILS_H__
