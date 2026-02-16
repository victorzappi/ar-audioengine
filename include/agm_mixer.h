/*
** Copyright (c) 2019, The Linux Foundation. All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above
**     copyright notice, this list of conditions and the following
**     disclaimer in the documentation and/or other materials provided
**     with the distribution.
**   * Neither the name of The Linux Foundation nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
** WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
** ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
** BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
** WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
** OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
** IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
** Changes from Qualcomm Innovation Center are provided under the following license:
** Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
** SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __AGM_MIXER_H__
#define __AGM_MIXER_H__

#include <kvh2xml.h> // for the graph keys and  values 
#include <agm/agm_api.h> // for struct agm_key_value


int init_agm_mixer(unsigned int virtual_card, char *frontend_name, char *backend_name, const char *backend_xml,
                   struct agm_key_value stream_kv, struct agm_key_value instance_kv, struct agm_key_value streampp_kv, 
                   struct agm_key_value devicepp_kv, struct agm_key_value device_kv);

int configure_agm(unsigned int physical_card, unsigned int physical_device, unsigned int period_count, 
                  unsigned int frame_size_fcr);

void cleanup_agm(void);


//-------------------------------
// moved to agm_mixer.cpp
//-------------------------------
// struct device_config {
//     char name[80];
//     unsigned int rate;
//     unsigned int ch;
//     unsigned int bits;
//     enum pcm_format format;
// };

// struct group_config {
//     char name[80];
//     unsigned int rate;
//     unsigned int ch;
//     unsigned int bits;
//     unsigned int slot_mask;
//     enum pcm_format format;
// };
//-------------------------------


//-------------------------------
// for now unused
//-------------------------------
// enum usecase_type{
//     PLAYBACK,
//     CAPTURE,
//     LOOPBACK,
//     HAPTICS,
// };

// enum stream_type {
//     STREAM_PCM,
//     STREAM_COMPRESS,
// };

// typedef enum {
//    SLOT_MASK1  = 1,
//    SLOT_MASK3  = 3,
//    SLOT_MASK7  = 7,
//    SLOT_MASK15 = 15,
// } slot_mask_t;
//-------------------------------


//-------------------------------
// now not need to be exposed
//-------------------------------
// int convert_char_to_hex(char *char_num);

// int set_agm_graph(struct mixer *mixer, char* frontend_name, struct agm_key_value *graph_kv, unsigned int num_graph_kv);
// int set_agm_device_metadata(struct mixer *mixer, char* backend_name, struct agm_key_value `, struct agm_key_value *calibraiton_kv, unsigned int num_ckv);
// int set_agm_stream_metadata(struct mixer *mixer, char* frontend_name, struct agm_key_value *stream_kv, unsigned int num_skv, struct agm_key_value *calibraiton_kv, 
//                             unsigned int num_ckv);
// int set_agm_streamdevice_metadata(struct mixer *mixer, char* frontend_name, char* backend_name, struct agm_key_value *streamdevice_kv, unsigned int num_sdkv, 
//                                   struct agm_key_value *calibraiton_kv, unsigned int num_ckv);
// int connect_agm_frontend_to_backend(struct mixer *mixer, char* frontend_name, char* backend_name, bool connect);
// int get_backend_config(char* filename, char *backend_name, struct device_config *config);
// int set_agm_backend_config(struct mixer *mixer, char *backend_name, struct device_config *config);
// int get_agm_module_iid(struct mixer *mixer, char *frontend_name, char *backend_name, int tag_id, uint32_t *miid);
// int set_agm_param(struct mixer *mixer, char *frontend_name, void *payload, uint32_t size);
// int configure_agm_mfc(struct mixer *mixer, char *frontend_name, unsigned int rate, unsigned int channels, 
//                       unsigned int bits, uint32_t miid);
// int configure_agm_alsa_sink(struct mixer *mixer, char *frontend_name, unsigned int card_id, unsigned int device_id, 
//                             unsigned int period_cnt, unsigned int frame_size_fcr, uint32_t miid);

//VIC re-implemented
// int configure_alsa_sink(struct mixer *mixer, int device, char *intf_name, /*int tag,*/ enum stream_type stype, unsigned int card_id,
//                         unsigned int device_id, unsigned int period_cnt, unsigned int frame_size_fcr, uint32_t miid);
// int configure_mfc(struct mixer *mixer, int device, char *intf_name, /*int tag,*/ enum stream_type stype, unsigned int rate,
//                       unsigned int channels, unsigned int bits, uint32_t miid);
// int agm_mixer_set_param(struct mixer *mixer, int device, enum stream_type stype, void *payload, int size);
// int agm_mixer_get_miid(struct mixer *mixer, int device, char *intf_name, enum stream_type stype, int tag_id, uint32_t *miid);
// int set_agm_device_media_config(struct mixer *mixer, char *intf_name, struct device_config *config);
// int get_device_media_config(char* filename, char *intf_name, struct device_config *config);
// int set_agm_audio_intf_metadata(struct mixer *mixer, char *intf_name, unsigned int device_kv, enum usecase_type, int rate, int bitwidth, unsigned int stream_kv);
// int set_agm_streamdevice_metadata(struct mixer *mixer, int device, uint32_t val, enum usecase_type usecase, enum stream_type stype,
//                                   char *intf_name, unsigned int devicepp_kv);
// int set_agm_stream_metadata(struct mixer *mixer, int device, uint32_t val, enum usecase_type utype, enum stream_type stype,
//                             unsigned int instance_kv);
// int set_agm_capture_stream_metadata(struct mixer *mixer, int device, uint32_t val, enum usecase_type utype, enum stream_type stype,
//                             unsigned int instance_kv);
// int connect_agm_audio_intf_to_stream(struct mixer *mixer, unsigned int device,
//                                   char *intf_name, enum stream_type, bool connect);

//VIC temporarily removed
// int agm_mixer_set_param_with_file(struct mixer *mixer, int device,
//                                   enum stream_type stype, char *path);
// int set_agm_group_device_config(struct mixer *mixer, char *intf_name, struct group_config *config);
// int set_agm_group_mux_config(struct mixer *mixer, unsigned int device, struct group_config *config, char *intf_name, unsigned int channels);
// int connect_play_pcm_to_cap_pcm(struct mixer *mixer, unsigned int p_device, unsigned int c_device);
// int agm_mixer_register_event(struct mixer *mixer, int device, enum stream_type stype, uint32_t miid, uint8_t is_register);
// int agm_mixer_set_ecref_path(struct mixer *mixer, unsigned int device, enum stream_type stype, char *intf_name);
// int agm_mixer_get_event_param(struct mixer *mixer, int device, enum stream_type stype,uint32_t miid);
// int agm_mixer_get_buf_tstamp(struct mixer *mixer, int device, enum stream_type stype, uint64_t *tstamp);
// int get_group_device_info(char* filename, char *intf_name, struct group_config *config);
//-------------------------------

#endif //__AGM_MIXER_H__
