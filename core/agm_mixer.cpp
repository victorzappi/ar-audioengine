/*
** Copyright (c) 2019, 2021, The Linux Foundation. All rights reserved.
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
** Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
** SPDX-License-Identifier: BSD-3-Clause-Clear
**
** Modifications by Victor Zappi, 2026
** SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <expat.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include <agm/agm_api.h>
#include "pcm_utils.h"
#include "agm_mixer.h"
#include "audioreach_mappings.h"

#define PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT            0x08001024
#define PARAM_ID_ALSA_DEVICE_INTF_CFG               0x08FFFFF3
#define PARAM_ID_HW_EP_FRAME_SIZE_FACTOR            0x08001018

//-------------------------------
// moved from agm_mixer.cpp
//-------------------------------
struct device_config {
    char name[80];
    unsigned int rate;
    unsigned int ch;
    unsigned int bits;
    enum pcm_format format;
};

struct group_config {
    char name[80];
    unsigned int rate;
    unsigned int ch;
    unsigned int bits;
    unsigned int slot_mask;
    enum pcm_format format;
};
//-------------------------------

static struct mixer *g_mixer = NULL;
static char *g_frontend_name;
static char *g_backend_name;
static struct device_config g_backend_config;


enum {
    DEVICE = 1,
    GROUP,
};

enum pcm_channel_map
{
   PCM_CHANNEL_L = 1,
   PCM_CHANNEL_R = 2,
   PCM_CHANNEL_C = 3,
   PCM_CHANNEL_LS = 4,
   PCM_CHANNEL_RS = 5,
   PCM_CHANNEL_LFE = 6,
   PCM_CHANNEL_CS = 7,
   PCM_CHANNEL_CB = PCM_CHANNEL_CS,
   PCM_CHANNEL_LB = 8,
   PCM_CHANNEL_RB = 9,
   PCM_CHANNEL_TS = 10,
   PCM_CHANNEL_TFC = 11,
   PCM_CHANNEL_MS = 12,
   PCM_CHANNEL_FLC = 13,
   PCM_CHANNEL_FRC = 14,
   PCM_CHANNEL_RLC = 15,
   PCM_CHANNEL_RRC = 16,
};
/* Payload of the PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT parameter in the
 Media Format Converter Module. Following this will be the variable payload for channel_map. */
struct param_id_mfc_output_media_fmt_t
{
   int32_t sampling_rate;
   int16_t bit_width;
   int16_t num_channels;
   uint16_t channel_type[0];
}__attribute__((packed));

/* Payload of the PARAM_ID_ALSA_DEVICE_INTF_CFG parameter in the Alsa Sink Module.  */
struct param_id_alsa_device_intf_cfg_t
{
   int32_t card_id;
   int32_t device_id;
   int32_t period_count;
   int32_t start_threshold;
   int32_t stop_threshold;
   int32_t silence_threshold;
}__attribute__((packed));

/* Payload of the PARAM_ID_HW_EP_FRAME_SIZE_FACTOR parameter in the Alsa Sink Module.  */
struct param_id_hw_ep_frame_size_factor_t
{
   int32_t frame_size_factor;
}__attribute__((packed));

struct apm_module_param_data_t
{
   uint32_t module_instance_id;
   uint32_t param_id;
   uint32_t param_size;
   uint32_t error_code;
};

struct gsl_module_id_info_entry {
    uint32_t module_id; /**< module id */
    uint32_t module_iid; /**< globally unique module instance id */
};

/**
 * Structure mapping the tag_id to module info (mid and miid)
 */
struct gsl_tag_module_info_entry {
    uint32_t tag_id; /**< tag id of the module */
    uint32_t num_modules; /**< number of modules matching the tag_id */
    struct gsl_module_id_info_entry module_entry[0]; /**< module list */
};

struct gsl_tag_module_info {
    uint32_t num_tags; /**< number of tags */
    struct gsl_tag_module_info_entry tag_module_entry[0];
    /**< variable payload of type struct gsl_tag_module_info_entry */
};


#define PADDING_8BYTE_ALIGN(x)  ((((x) + 7) & 7) ^ 7)



// Helper function to print metadata debug info
static void print_metadata_info(const char *mixer_str, 
                                struct agm_key_value *graph_kv, unsigned int num_graph_kv,
                                struct agm_key_value *calibraiton_kv, unsigned int num_ckv)
{
    printf("---\tmixer ctl: %s", mixer_str);
    
    // Print graph_kv values
    if (graph_kv && num_graph_kv > 0) {
        printf(" graph_kv:");
        for (unsigned int i = 0; i < num_graph_kv; i++) {
            printf(" [0x%X (%s), 0x%X (%s)]", 
                   graph_kv[i].key,   
                   get_audioreach_name(graph_kv[i].key), 
                   graph_kv[i].value,
                   get_audioreach_name(graph_kv[i].value));
        }
    }
    
    // Print calibraiton_kv values as numbers
    if (calibraiton_kv && num_ckv > 0) {
        printf(", calibraiton_kv:");
        //TODO calibration define mapping?
        for (unsigned int i = 0; i < num_ckv; i++) {
            printf(" [0x%X, %d]", calibraiton_kv[i].key, calibraiton_kv[i].value);
        }
    }
    printf("\n");
}

// Helper function to get mixer value via an array, including metadata
static int get_mixer_ctl_array(struct mixer *mixer, const char *mixer_str, 
                                void *payload, size_t payload_size)
{
    struct mixer_ctl *ctl;
    int ret = 0;
    
    ctl = mixer_get_ctl_by_name(mixer, mixer_str);
    if (!ctl) {
        printf("Could not get ctl for mixer cmd - %s\n", mixer_str);
        return -ENODEV;
    }
    
    ret = mixer_ctl_get_array(ctl, payload, payload_size);
    if (ret < 0) {
        printf("Could not get ctl for mixer cmd - %s, ret %d\n", mixer_str, ret);
    }
    
    return ret;
}

// Helper function to set mixer value via an array, including metadata
static int set_mixer_ctl_array(struct mixer *mixer, const char *mixer_str, 
                              const void *payload, size_t payload_size)
{
    struct mixer_ctl *ctl;
    int ret = 0;
    
    ctl = mixer_get_ctl_by_name(mixer, mixer_str);
    if (!ctl) {
        printf("Could not get ctl for mixer cmd - %s\n", mixer_str);
        return -ENODEV;
    }
    
    ret = mixer_ctl_set_array(ctl, payload, payload_size);
    if (ret < 0) {
        printf("Could not set ctl for mixer cmd - %s, ret %d\n", mixer_str, ret);
    }
    
    return ret;
}

// Helper function to set mixer enum via a string
int set_mixer_ctl_string(struct mixer *mixer, const char *mixer_str, 
                            const char *payload)                    
{
    struct mixer_ctl *ctl;
    int ret = 0;
    
    ctl = mixer_get_ctl_by_name(mixer, mixer_str);
    if (!ctl) {
        printf("Could not get ctl for mixer cmd - %s\n", mixer_str);
        return -ENODEV;
    }
    
    ret = mixer_ctl_set_enum_by_string(ctl, (const char *)payload); 
    if (ret < 0) {
        printf("Could not set ctl for mixer cmd - %s %s, ret %d\n", mixer_str, payload, ret);
    }
    
    return ret;
}

// Helper function to create metadata
// struct prop_data **props is the only double pointer argument because
// prop_data has a flexible array member (values[]), making it variable-sized.
// Variable-sized structs can't form regular arrays, so we use an array of pointers instead.
static uint8_t* create_metadata(struct agm_key_value *graph_kv, unsigned int num_graph_kv,
                                    struct agm_key_value *calibraiton_kv, unsigned int num_ckv,
                                    struct prop_data **props, unsigned int num_props,
                                    size_t *metadata_size)
{
    uint32_t graph_kv_size, ckv_size, prop_size;
    uint8_t *metadata = NULL;
    int offset = 0;
    
    graph_kv_size = num_graph_kv * sizeof(struct agm_key_value);
    ckv_size = num_ckv * sizeof(struct agm_key_value);
    prop_size = 0;
    for (unsigned int i = 0; i < num_props; i++) {
        prop_size += sizeof(struct prop_data) + props[i]->num_values * sizeof(uint32_t);
    }

    *metadata_size = sizeof(num_graph_kv) + sizeof(num_ckv) + graph_kv_size + ckv_size + prop_size;
    
    metadata = (uint8_t *)calloc(1, *metadata_size);
    if (!metadata)
        return NULL;
    
    // Copy num_graph_kv
    memcpy(metadata + offset, &num_graph_kv, sizeof(num_graph_kv));
    offset += sizeof(num_graph_kv);

    // Copy graph_kv data
    if (graph_kv && num_graph_kv > 0) {
        memcpy(metadata + offset, graph_kv, graph_kv_size);
        offset += graph_kv_size;
    }
    
    // Copy num_ckv
    memcpy(metadata + offset, &num_ckv, sizeof(num_ckv));
    offset += sizeof(num_ckv);
    
    // Copy calibraiton_kv data
    if (calibraiton_kv && num_ckv > 0) {
        memcpy(metadata + offset, calibraiton_kv, ckv_size);
        offset += ckv_size;
    }
    
    // Copy prop data
    for (unsigned int i = 0; i < num_props; i++) {
        size_t this_size = sizeof(struct prop_data) + props[i]->num_values * sizeof(uint32_t);
        memcpy(metadata + offset, props[i], this_size);
        offset += this_size;
    }
    
    return metadata;
}

// Helper function to build the mixer string containing device name and control
static char* build_mixer_control_string(const char *device_name, const char *control)
{
    int ctl_len = strlen(device_name) + 1 + strlen(control) + 1;
    char *mixer_str = (char *)calloc(1, ctl_len);
    if (!mixer_str) {
        printf("mixer_str calloc failed\n");
        return NULL;
    }
    snprintf(mixer_str, ctl_len, "%s %s", device_name, control);
    return mixer_str;
}

int set_agm_device_metadata(struct mixer *mixer, char* backend_name, 
                            struct agm_key_value device_kv, 
                            struct agm_key_value *calibraiton_kv, unsigned int num_ckv)
{
    printf("---set_agm_device_metadata\n");

    char *control = (char *)"metadata";
    char *mixer_str;
    uint8_t *metadata = NULL;
    size_t metadata_size;
    int ret = 0;
    struct prop_data **props = NULL;
    int num_dkv = 1, num_props = 0;
    
    // Create metadata using helper function
    // Pass &device_kv as graph_kv, NULL for props
    metadata = create_metadata(&device_kv, num_dkv, calibraiton_kv, num_ckv, props, num_props, &metadata_size);
    if (!metadata) {
        return -ENOMEM;
    }
    
    // Construct mixer control string
    mixer_str = build_mixer_control_string(backend_name, control);
    if (!mixer_str) {
        free(metadata);
        return -ENOMEM;
    }
        
    // Set the metadata using helper function
    ret = set_mixer_ctl_array(mixer, mixer_str, metadata, metadata_size);

    // Print success message with all key-value pairs
    if (ret == 0) {
        print_metadata_info(mixer_str, &device_kv, 1, calibraiton_kv, num_ckv);
    }
    
    free(mixer_str);
    free(metadata);
    return ret;
}

int set_agm_stream_metadata_type(struct mixer *mixer, char* frontend_name, char *metadata_type)
{
    printf("\t---set_agm_stream_metadata_type\n");
    
    char *control = (char *)"control";
    char *mixer_str;
    int ret = 0;

    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        return -ENOMEM;
    }
 
    // Set the ctl enum via a string using helper function
    ret = set_mixer_ctl_string(mixer, mixer_str, metadata_type);

    // Print success message with the mixer control and the enum/string
    if (ret == 0) {
        printf("---\t\tmixer ctl: %s %s\n", mixer_str, metadata_type);
    }

    free(mixer_str);
    return ret;
}

int set_agm_stream_metadata(struct mixer *mixer, char* frontend_name, 
                            struct agm_key_value *stream_kv, unsigned int num_skv, 
                            struct agm_key_value *calibraiton_kv, unsigned int num_ckv)
{
    printf("---set_agm_stream_metadata\n");

    char *control = (char *)"metadata";
    char *mixer_str;
    uint8_t *metadata = NULL;
    size_t metadata_size;
    int ret = 0;
    struct prop_data **props = NULL;
    int num_props = 0;
    char *type = (char *)"ZERO";

    ret = set_agm_stream_metadata_type(mixer, frontend_name, type);
    if (ret)
        return ret;

    // Create metadata using helper function
    // Pass &stream_kv as graph_kv, NULL for props
    metadata = create_metadata(stream_kv, num_skv, calibraiton_kv, num_ckv, props, num_props, &metadata_size);
    if (!metadata) {
        return -ENOMEM;
    }
    
    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        free(metadata);
        return -ENOMEM;
    }
       
    // Set the metadata using helper function
    ret = set_mixer_ctl_array(mixer, mixer_str, metadata, metadata_size);

    // Print success message with all key-value pairs
    if (ret == 0) {
        print_metadata_info(mixer_str, stream_kv, num_skv, calibraiton_kv, num_ckv);
    }
    
    free(mixer_str);
    free(metadata);
    return ret;
} 

int set_agm_streamdevice_metadata(struct mixer *mixer, char* frontend_name, char* backend_name, 
                                  struct agm_key_value *streamdevice_kv, unsigned int num_sdkv, 
                                  struct agm_key_value *calibraiton_kv, unsigned int num_ckv)
{
    printf("---set_agm_streamdevice_metadata\n");

    char *control = (char *)"metadata";
    char *mixer_str;
    uint8_t *metadata = NULL;
    size_t metadata_size;
    int ret = 0;
    struct prop_data **props = NULL;
    int num_props = 0;
    char *type = backend_name;

    ret = set_agm_stream_metadata_type(mixer, frontend_name, type);
    if (ret)
        return ret;

    // Create metadata using helper function
    // Pass &stream_kv as graph_kv, NULL for props
    metadata = create_metadata(streamdevice_kv, num_sdkv, calibraiton_kv, num_ckv, props, num_props, &metadata_size);
    if (!metadata) {
        return -ENOMEM;
    }
    
    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        free(metadata);
        return -ENOMEM;
    }
    
    // Set the metadata using helper function
    ret = set_mixer_ctl_array(mixer, mixer_str, metadata, metadata_size);

    // Print success message with all key-value pairs
    if (ret == 0) {
        print_metadata_info(mixer_str, streamdevice_kv, num_sdkv, calibraiton_kv, num_ckv);
    }
    
    free(mixer_str);
    free(metadata);
    return ret;

}

int set_agm_graph(struct mixer *mixer, char* frontend_name, struct agm_key_value *graph_kv, unsigned int num_graph_kv)
{
    printf("---set_agm_graph\n");

    struct agm_key_value *calibraiton_kv = NULL;
    unsigned int num_ckv = 0;

    // we can pass the whole graph_kv vector as stream metadata
    return set_agm_stream_metadata(mixer, frontend_name, graph_kv, num_graph_kv, calibraiton_kv, num_ckv);
}

int connect_agm_frontend_to_backend(struct mixer *mixer, char* frontend_name, char* backend_name, bool connect)
{
    printf("---connect_agm_frontend_to_backend\n");
    
    char *control;
    char *mixer_str;
    int ret = 0;

    if (connect)
        control = (char *)"connect";
    else
        control = (char *)"disconnect";

    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        return -ENOMEM;
    }
 
    // Set the ctl enum via a string using helper function
    ret = set_mixer_ctl_string(mixer, mixer_str, backend_name);

    // Print success message with the mixer control and the enum/string
    if (ret == 0) {
        printf("---\t\tmixer ctl: %s %s\n", mixer_str, backend_name);
    }

    free(mixer_str);
    return ret;
}

void start_tag(void *userdata, const XML_Char *tag_name, const XML_Char **attr)
{
    struct device_config *config = (struct device_config *)userdata;
    enum pcm_format fmt;

    if (strncmp(tag_name, "device", strlen("device")) != 0)
        return;

    if (strcmp(attr[0], "name") != 0) {
        printf("name not found\n");
        return;
    }

    if (strcmp(attr[2], "rate") != 0) {
        printf("rate not found\n");
        return;
    }

    if (strcmp(attr[4], "ch") != 0) {
        printf("channels not found\n");
        return;
    }

    if (strcmp(attr[6], "bits") != 0) {
        printf("bits not found\n");
        return;
    }

    if (strncmp(config->name, attr[1], sizeof(config->name)))
        return;

    if (attr[8]) {
        if (strcmp(attr[8], "format") == 0) {
            printf("PCM format found\n");
            fmt = get_pcm_format(attr[9]);
            if (fmt != PCM_FORMAT_INVALID && fmt < PCM_FORMAT_MAX)
                config->format = fmt;
        }
    } else {
           config->format = PCM_FORMAT_INVALID;
    }

    config->rate = atoi(attr[3]);
    config->ch = atoi(attr[5]);
    config->bits = atoi(attr[7]);
}

void start_group_tag(void *userdata, const XML_Char *tag_name, const XML_Char **attr)
{
    struct group_config *config = (struct group_config *)userdata;
    enum pcm_format fmt;

    if (strncmp(tag_name, "group_device", strlen("group_device")) != 0)
        return;

    if (strcmp(attr[0], "name") != 0) {
        printf("name not found\n");
        return;
    }

    if (strcmp(attr[2], "rate") != 0) {
        printf("rate not found\n");
        return;
    }

    if (strcmp(attr[4], "ch") != 0) {
        printf("channels not found\n");
        return;
    }

    if (strcmp(attr[6], "bits") != 0) {
        printf("bits not found\n");
        return;
    }

    if (strcmp(attr[8], "slot_mask") != 0) {
        printf("slot_mask not found\n");
        return;
    }

    if (strncmp(config->name, attr[1], sizeof(config->name)))
        return;

    if (attr[10]) {
        if (strcmp(attr[10], "format") == 0) {
            printf("PCM format found\n");
            fmt = get_pcm_format(attr[11]);
            if (fmt != PCM_FORMAT_INVALID && fmt < PCM_FORMAT_MAX)
                config->format = fmt;
        }
    } else {
        config->format = PCM_FORMAT_INVALID;
    }
    config->rate = atoi(attr[3]);
    config->ch = atoi(attr[5]);
    config->bits = atoi(attr[7]);
    config->slot_mask = atoi(attr[9]);
}

static int get_backend_info(const char* filename, char *backend_name, void *config, int type)
{
    FILE *file = NULL;
    XML_Parser parser;
    int ret = 0;
    int bytes_read;
    void *buf = NULL;
    struct device_config *dev_cfg;
    struct group_config *grp_cfg;

    file = fopen(filename, "r");
    if (!file) {
        ret = -EINVAL;
        printf("Failed to open xml file name %s ret %d", filename, ret);
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ret = -EINVAL;
        printf("Failed to create XML ret %d", ret);
        goto closeFile;
    }
    if (type == DEVICE) {
        dev_cfg = (struct device_config *)config;
        memset(dev_cfg, 0, sizeof(*dev_cfg));
        strlcpy(dev_cfg->name, backend_name, sizeof(dev_cfg->name));
        XML_SetElementHandler(parser, start_tag, NULL);
    } else {
        grp_cfg = (struct group_config *)config;
        memset(grp_cfg, 0, sizeof(*grp_cfg));
        strlcpy(grp_cfg->name, backend_name, sizeof(grp_cfg->name));
        XML_SetElementHandler(parser, start_group_tag, NULL);
    }

    XML_SetUserData(parser, config);

    while (1) {
        buf = XML_GetBuffer(parser, 1024);
        if (buf == NULL) {
            ret = -EINVAL;
            printf("XML_Getbuffer failed ret %d", ret);
            goto freeParser;
        }

        bytes_read = fread(buf, 1, 1024, file);
        if (bytes_read < 0) {
            ret = -EINVAL;
            printf("fread failed ret %d", ret);
            goto freeParser;
        }

        if (XML_ParseBuffer(parser, bytes_read, bytes_read == 0) == XML_STATUS_ERROR) {
            ret = -EINVAL;
            printf("XML ParseBuffer failed for %s file ret %d", filename, ret);
            goto freeParser;
        }
        if (bytes_read == 0 || ((struct device_config *)config)->rate != 0)
            break;
    }

    if (((struct device_config *)config)->rate == 0) {
        ret = -EINVAL;
        printf("Entry not found\n");
    }
freeParser:
    XML_ParserFree(parser);
closeFile:
    fclose(file);
done:
    return ret;
}

int get_backend_config(const char* filename, char *backend_name, struct device_config *config)
{
    return get_backend_info(filename, backend_name, (void *)config, DEVICE);
}

int set_agm_backend_config(struct mixer *mixer, char *backend_name, struct device_config *config)
{
    printf("---set_agm_backend_config\n");

    char *control = (char *)"rate ch fmt";
    char *mixer_str;
    long media_config[4];
    int ret = 0;

    // Create ctl values as array
    media_config[0] = config->rate;
    media_config[1] = config->ch;
    if (config->format == PCM_FORMAT_INVALID)
        media_config[2] = bits_to_sndrv_format(config->bits);
    else
        media_config[2] = alsa_to_sndrv_format(config->format);
    media_config[3] = AGM_DATA_FORMAT_FIXED_POINT;

    // Construct mixer control string
    mixer_str = build_mixer_control_string(backend_name, control);
    if (!mixer_str) {
        return -ENOMEM;
    }

    // Set the ctl values via an array using helper function
    ret = set_mixer_ctl_array(mixer, mixer_str, media_config, sizeof(media_config)/sizeof(media_config[0]));

    // Print success message with all key-value pairs
    if (ret == 0) {
        printf("---\tmixer ctl: %s (data type) %ld %ld %ld %ld", mixer_str, media_config[0], media_config[1], media_config[2],
               media_config[3]);
    }
        
    free(mixer_str);
    return ret;
}

int get_agm_module_iid(struct mixer *mixer, char *frontend_name, char *backend_name, int tag_id, uint32_t *miid)
{
    printf("---get_agm_module_iid, searching for tag 0x%X (%s)\n", tag_id, get_tag_name(tag_id));

    char *control = (char *)"getTaggedInfo";
    char *mixer_str;
    int ret = 0, i;
    void *payload;
    int payload_size = 1024;
    struct gsl_tag_module_info *tag_info;
    struct gsl_tag_module_info_entry *tag_entry;
    char *tag_entry_ptr;
    char *type = backend_name;

    ret = set_agm_stream_metadata_type(mixer, frontend_name, type);
    if (ret)
        return ret;

    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        return -ENOMEM;
    }

    // Prepare container for ctl array
    payload = calloc(payload_size, sizeof(char));
    if (!payload) {
        free(mixer_str);
        return -ENOMEM;
    }

    // Get the ctl array using helper function
    ret = get_mixer_ctl_array(mixer, mixer_str, payload, payload_size);
    if (ret < 0) {
        free(payload);
        free(mixer_str);
        return ret;
    }

    printf("---\t%s\n", mixer_str);

    tag_info = (struct gsl_tag_module_info *)payload;
    ret = -1;
    tag_entry_ptr = (char *)&tag_info->tag_module_entry[0]; // cast useful to apply byte offset in between entries

    printf("---\tnum_tags: %d\n", tag_info->num_tags);

    int found=0;
    for (i = 0; i < (int)tag_info->num_tags; i++) {
        tag_entry = (struct gsl_tag_module_info_entry *)tag_entry_ptr; // current tag_entry, variable-length struct 
        tag_entry_ptr += sizeof(struct gsl_tag_module_info_entry) + 
           (tag_entry->num_modules * sizeof(struct gsl_module_id_info_entry)); // move poitner to next entry via byte offset

        printf("---\t\ttag_entry %d, id: 0x%X (%s)\n", i, tag_entry->tag_id, get_tag_name(tag_entry->tag_id));
        
        if(found)
            continue; // or beak if we don't want to see other modules's iids!

        if (tag_entry->tag_id == (u_int32_t)tag_id) {
            printf("---\t\t\tfound tag id 0x%X (%s)\n", tag_entry->tag_id, get_tag_name(tag_id));
            struct gsl_module_id_info_entry *mod_info_entry;

            if (tag_entry->num_modules) {
                printf("---\t\t\ttag_entry[%d].num_modules: %d\n", i, tag_entry->num_modules);
                 mod_info_entry = &tag_entry->module_entry[0];
                 *miid = mod_info_entry->module_iid;
                 printf("---\t\t\ttag_entry[%d].module_entry[0].id: 0x%X\n", i, mod_info_entry->module_id);
                 printf("---\t\t\ttag_entry[%d].module_entry[0].iid: 0x%X\n", i, mod_info_entry->module_iid);
                 ret = 0;
                 found=1;
            }
        }
    }

    if (found == 0) {
        printf("---\tCould not find tag 0x%X (%s)\n", tag_id, get_tag_name(tag_id));
        ret = 1;
    }

    free(payload);
    free(mixer_str);
    return ret;
}

int set_agm_param(struct mixer *mixer, char *frontend_name, void *payload, uint32_t size)
{
    char *control = (char *)"setParam";
    char *mixer_str;
    int ret = 0;

    // Construct mixer control string
    mixer_str = build_mixer_control_string(frontend_name, control);
    if (!mixer_str) {
        return -ENOMEM;
    }

    // Set the param using helper function
    ret = set_mixer_ctl_array(mixer, mixer_str, payload, size);
    
    // Print success message with all key-value pairs
    if (ret == 0) {
        printf("---\tmixer ctl: %s payload\n", mixer_str);
    }

    free(mixer_str);
    return ret;
}

void populateChannelMap(uint16_t *pcmChannel, uint8_t numChannel)
{
    if (numChannel == 1) {
        pcmChannel[0] = PCM_CHANNEL_C;
    } else if (numChannel == 2) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
    } else if (numChannel == 3) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
    } else if (numChannel == 4) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_LB;
        pcmChannel[3] = PCM_CHANNEL_RB;
    }  else if (numChannel == 5) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LB;
        pcmChannel[4] = PCM_CHANNEL_RB;
    }  else if (numChannel == 6) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LFE;
        pcmChannel[4] = PCM_CHANNEL_LB;
        pcmChannel[5] = PCM_CHANNEL_RB;
    }  else if (numChannel == 7) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LFE;
        pcmChannel[4] = PCM_CHANNEL_LB;
        pcmChannel[5] = PCM_CHANNEL_RB;
        pcmChannel[6] = PCM_CHANNEL_CS;
    }  else if (numChannel == 8) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LFE;
        pcmChannel[4] = PCM_CHANNEL_LB;
        pcmChannel[5] = PCM_CHANNEL_RB;
        pcmChannel[6] = PCM_CHANNEL_LS;
        pcmChannel[7] = PCM_CHANNEL_RS;
    } else if (numChannel == 10) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_LFE;
        pcmChannel[6] = PCM_CHANNEL_CS;
        pcmChannel[7] = PCM_CHANNEL_LB;
        pcmChannel[8] = PCM_CHANNEL_RB;
        pcmChannel[9] = PCM_CHANNEL_TS;
    } else if (numChannel == 12) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_LFE;
        pcmChannel[6] = PCM_CHANNEL_CS;
        pcmChannel[7] = PCM_CHANNEL_LB;
        pcmChannel[8] = PCM_CHANNEL_RB;
        pcmChannel[9] = PCM_CHANNEL_TS;
        pcmChannel[10] = PCM_CHANNEL_TFC;
        pcmChannel[11] = PCM_CHANNEL_MS;
    } else if (numChannel == 14) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_LFE;
        pcmChannel[6] = PCM_CHANNEL_CS;
        pcmChannel[7] = PCM_CHANNEL_LB;
        pcmChannel[8] = PCM_CHANNEL_RB;
        pcmChannel[9] = PCM_CHANNEL_TS;
        pcmChannel[10] = PCM_CHANNEL_TFC;
        pcmChannel[11] = PCM_CHANNEL_MS;
        pcmChannel[12] = PCM_CHANNEL_FLC;
        pcmChannel[13] = PCM_CHANNEL_FRC;
    } else if (numChannel == 16) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_LFE;
        pcmChannel[6] = PCM_CHANNEL_CS;
        pcmChannel[7] = PCM_CHANNEL_LB;
        pcmChannel[8] = PCM_CHANNEL_RB;
        pcmChannel[9] = PCM_CHANNEL_TS;
        pcmChannel[10] = PCM_CHANNEL_TFC;
        pcmChannel[11] = PCM_CHANNEL_MS;
        pcmChannel[12] = PCM_CHANNEL_FLC;
        pcmChannel[13] = PCM_CHANNEL_FRC;
        pcmChannel[14] = PCM_CHANNEL_RLC;
        pcmChannel[15] = PCM_CHANNEL_RRC;
    }
}

// Helper function to create AGM parameter payload with proper alignment
static uint8_t* create_agm_param_payload(size_t param_size, 
                                          size_t *payload_size_out, 
                                          size_t *pad_bytes_out)
{
    uint8_t* payload = NULL;
    size_t payloadSize = 0, padBytes = 0;
    
    // Calculate total payload size (header + parameter data)
    payloadSize = sizeof(struct apm_module_param_data_t) + param_size;
    
    // Calculate padding for 8-byte alignment
    padBytes = PADDING_8BYTE_ALIGN(payloadSize);
    
    // Allocate zeroed memory for payload + padding
    payload = (uint8_t*) calloc(1, payloadSize + padBytes);
    if (!payload) {
        return NULL;
    }
    
    // Return sizes via output parameters
    if (payload_size_out) {
        *payload_size_out = payloadSize;
    }
    if (pad_bytes_out) {
        *pad_bytes_out = padBytes;
    }
    
    return payload;
}

int configure_agm_mfc(struct mixer *mixer, char *frontend_name, unsigned int rate, unsigned int channels, 
                  unsigned int bits, uint32_t miid)
{
    printf("---configure_agm_mfc\n");

    int ret = 0;
    struct apm_module_param_data_t* header = NULL;
    struct param_id_mfc_output_media_fmt_t *mfc_outMediaFmt;
    uint16_t* pcmChannel = NULL;
    uint8_t* payload = NULL;
    size_t payloadSize = 0, padBytes = 0, paramSize, paddedSize;

    
    // Create payload container
    paramSize = sizeof(struct param_id_mfc_output_media_fmt_t) + 
                    sizeof(uint16_t)*channels;
    payload = create_agm_param_payload(paramSize, &payloadSize, &padBytes);
    if (!payload) {
        return -ENOMEM;
    }

    // Fill header
    header = (struct apm_module_param_data_t*)payload;
    header->module_instance_id = miid;
    header->param_id = PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT;
    header->error_code = 0x0;
    header->param_size = paramSize;

    // Fill actual param's components
    mfc_outMediaFmt = (struct param_id_mfc_output_media_fmt_t*)(payload +
               sizeof(struct apm_module_param_data_t));
    mfc_outMediaFmt->sampling_rate = rate;
    mfc_outMediaFmt->bit_width = bits;
    mfc_outMediaFmt->num_channels = channels;

    pcmChannel = (uint16_t*)(payload + sizeof(struct apm_module_param_data_t) +
                                       sizeof(struct param_id_mfc_output_media_fmt_t));
    populateChannelMap(pcmChannel, channels);
    
    paddedSize = payloadSize + padBytes;

    printf("---\tpayload:\n");
    printf("---\t\theader->module_instance_id: 0x%X\n", header->module_instance_id);
    printf("---\t\theader->param_id: 0x%X (PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT)\n", header->param_id);
    printf("---\t\tmfc_outMediaFmt->sampling_rate: %d\n", rate);
    printf("---\t\tmfc_outMediaFmt->bit_width: %d\n", bits);
    printf("---\t\tmfc_outMediaFmt->num_channels: %d\n", channels);

    ret = set_agm_param(mixer, frontend_name, (void *)payload, paddedSize);

    free(payload);
    return ret;
}

int configure_agm_alsa_sink(struct mixer *mixer, char *frontend_name, unsigned int card_id, unsigned int device_id, 
                            unsigned int period_cnt, unsigned int frame_size_fcr, uint32_t miid)
{
    printf("---configure_agm_alsa_sink\n");

    int ret = 0;
    struct apm_module_param_data_t* header = NULL;
    struct param_id_alsa_device_intf_cfg_t *alsaSink_devIntfCfg;
    // struct param_id_hw_ep_frame_size_factor_t *alsaSink_frmSizeFcr;
    uint8_t* payload = NULL;
    size_t payloadSize = 0, padBytes = 0, paramSize, paddedSize;


    /* set PARAM_ID_ALSA_DEVICE_INTF_CFG */

    // Create payload container
    paramSize =  sizeof(struct param_id_alsa_device_intf_cfg_t);
    payload = create_agm_param_payload(paramSize, &payloadSize, &padBytes);
    if (!payload) {
        return -ENOMEM;
    }

    // Fill header
    header = (struct apm_module_param_data_t*)payload;
    header->module_instance_id = miid;
    header->param_id = PARAM_ID_ALSA_DEVICE_INTF_CFG;
    header->error_code = 0x0;
    header->param_size = paramSize;

    // Fill actual param's components
    alsaSink_devIntfCfg = (struct param_id_alsa_device_intf_cfg_t*)(payload +
               sizeof(struct apm_module_param_data_t));
    alsaSink_devIntfCfg->card_id = card_id;
    alsaSink_devIntfCfg->device_id = device_id;
    alsaSink_devIntfCfg->period_count = period_cnt;
    alsaSink_devIntfCfg->start_threshold = 0;    // auto
    alsaSink_devIntfCfg->stop_threshold = 0;     // auto
    alsaSink_devIntfCfg->silence_threshold = 0;  // auto
    
    paddedSize = payloadSize + padBytes;

    printf("---\tpayload:\n");
    printf("---\t\theader->module_instance_id: 0x%X\n", header->module_instance_id);
    printf("---\t\theader->param_id: 0x%X (PARAM_ID_ALSA_DEVICE_INTF_CFG)\n", header->param_id);
    printf("---\t\talsaSink_devIntfCfg->card_id: %d\n", card_id);
    printf("---\t\talsaSink_devIntfCfg->device_id: %d\n", device_id);
    printf("---\t\talsaSink_devIntfCfg->period_count: %d\n", period_cnt);
    printf("---\t\talsaSink_devIntfCfg->start_threshold: %d\n", 0);
    printf("---\t\talsaSink_devIntfCfg->stop_threshold: %d\n", 0);
    printf("---\t\talsaSink_devIntfCfg->silence_threshold: %d\n", 0);

    ret = set_agm_param(mixer, frontend_name, (void *)payload, paddedSize);

    free(payload);

    if (ret != 0) {
        return ret;  // Exit early if first param fails
    }

    //VIC this can be done only once [before pcm_start()?], so if this is done multi stream use cases cannot run with more instances!
    /* set PARAM_ID_HW_EP_FRAME_SIZE_FACTOR */
    // paramSize = sizeof(struct param_id_hw_ep_frame_size_factor_t);
    // payload = create_agm_param_payload(paramSize, &payloadSize, &padBytes);
    // if (!payload) {
    //     return -ENOMEM;
    // }

    // // Fill header
    // header = (struct apm_module_param_data_t*)payload;
    // header->module_instance_id = miid;
    // header->param_id = PARAM_ID_HW_EP_FRAME_SIZE_FACTOR;
    // header->error_code = 0x0;
    // header->param_size = paramSize;

    // alsaSink_frmSizeFcr = (struct param_id_hw_ep_frame_size_factor_t*)(payload +
    //            sizeof(struct apm_module_param_data_t));
    // alsaSink_frmSizeFcr->frame_size_factor = frame_size_fcr;
    
    // paddedSize = payloadSize + padBytes;

    // printf("---\tpayload:\n");
    // printf("---\t\theader->module_instance_id: 0x%X\n", header->module_instance_id);
    // printf("---\t\theader->param_id: 0x%X (PARAM_ID_HW_EP_FRAME_SIZE_FACTOR)\n", header->param_id);
    // printf("---\t\talsaSink_frmSizeFcr->frame_size_factor: %d\n", frame_size_fcr);

    // ret = set_agm_param(mixer, frontend_name, (void *)payload, paddedSize);

    // free(payload);

    return ret;
}












int init_agm_mixer(unsigned int virtual_card, char *frontend_name, char *backend_name, const char *backend_xml,
                   struct agm_key_value stream_kv, struct agm_key_value instance_kv, struct agm_key_value streampp_kv, 
                   struct agm_key_value devicepp_kv, struct agm_key_value device_kv)
{
    struct agm_key_value *graph_kv = NULL;
    int num_graph_kv = 0;
    int idx = 0;
    int ret = 0;

    g_frontend_name = strdup(frontend_name);
    g_backend_name = strdup(backend_name);


    g_mixer = mixer_open(virtual_card);
    if (!g_mixer) {
        printf("Failed to open mixer\n");
        ret = -1;
        goto done;
    }

    // retrieve standard configuration of backend card
    if (get_backend_config(backend_xml, g_backend_name, &g_backend_config)) {
        printf("Invalid backend card, entry not found for: %s\n", g_backend_name);
        ret = -1;
        goto done;
    }
    if (g_backend_config.format != PCM_FORMAT_INVALID) {    
        // format is optional, but if provided let's make sure that the num of bits matches the format!
        g_backend_config.bits = format_to_signed_pcm_bits(g_backend_config.format); // inverse of signed_pcm_bits_to_format()
    }

    //VIC this is not needed on Pi, but still unclear if needed on other platforms!
    // intialize backend via mixer with its standard configuration
    if (set_agm_backend_config(g_mixer, g_backend_name, &g_backend_config)) {
        printf("Failed to configure backend %s\n", g_backend_name);
        ret = -1;
        goto done;
    }
    printf("\n");


    // let's build the graph!

    // Count non-zero key-value pairs
    if (stream_kv.value != 0)
        num_graph_kv++;
    if (instance_kv.value != 0)
        num_graph_kv++;
    if (streampp_kv.value != 0)
        num_graph_kv++;
    if (devicepp_kv.value != 0)
        num_graph_kv++;
    if (device_kv.value != 0)
        num_graph_kv++;
    
    // Allocate array if we have any non-zero values
    if (num_graph_kv == 0) {
        printf("Empty graph key-value vector, no use case can be loaded\n");
        ret = -1;
        goto done;
    }

    graph_kv = (struct agm_key_value *)calloc(num_graph_kv, sizeof(struct agm_key_value));
    if (!graph_kv) {
        fprintf(stderr, "Failed to allocate memory for graph_kv array\n");
        return -ENOMEM;
    }
    
    // Copy non-zero key-value pairs to the array
    if (stream_kv.value != 0) {
        graph_kv[idx].key = stream_kv.key;
        graph_kv[idx].value = stream_kv.value;
        idx++;
    }
    if (instance_kv.value != 0) {
        graph_kv[idx].key = instance_kv.key;
        graph_kv[idx].value = instance_kv.value;
        idx++;
    }
    if (streampp_kv.value != 0) {
        graph_kv[idx].key = streampp_kv.key;
        graph_kv[idx].value = streampp_kv.value;
        idx++;
    }
    if (devicepp_kv.value != 0) {
        graph_kv[idx].key = devicepp_kv.key;
        graph_kv[idx].value = devicepp_kv.value;
        idx++;
    }
    if (device_kv.value != 0) {
        graph_kv[idx].key = device_kv.key;
        graph_kv[idx].value = device_kv.value;
        idx++;
    }

    // build graph
    if (set_agm_graph(g_mixer, g_frontend_name, graph_kv, num_graph_kv)) {
        printf("Failed to build graph for use case\n");
        ret = -1;
        goto done;
    }

    // connect frontend and backend to graph
    if (connect_agm_frontend_to_backend(g_mixer, g_frontend_name, g_backend_name, true)) {
        printf("Failed to connect pcm to audio interface\n");
        ret = -1;
        goto done;
    }
    printf("\n");

done:
    free(graph_kv);
    return ret;
}



int configure_agm(unsigned int physical_card, unsigned int physical_device, unsigned int period_count, 
                         unsigned int frame_size_fcr)
{
    uint32_t miid = 0;

    // retrieve the instance id of the PSPD MFC module...
    if (get_agm_module_iid(g_mixer, g_frontend_name, g_backend_name, PER_STREAM_PER_DEVICE_MFC, &miid) == 0) {
        printf("\n");
         // ...and use it to configure one of its params
        if (configure_agm_mfc(g_mixer, g_frontend_name, g_backend_config.rate, 
                              g_backend_config.ch, g_backend_config.bits, miid)) {
            printf("Failed to configure pspd mfc\n");
            return -1;
        }    
    } 
    else {
        printf("MFC not present in this graph\n");
        //return -1; //VIC we can live without an MFC module
    }
    printf("\n");
   

    // same with the Alsa Sink module found in the device subgraph...
    // if (get_agm_module_iid (g_mixer, g_frontend_name, g_backend_name, DEVICE_HW_ENDPOINT_RX, &miid) == 0) {
    //     printf("\n");
    //     // ...we configure two of its params
    //     if (configure_agm_alsa_sink(g_mixer, g_frontend_name, physical_card, physical_device, 
    //                                 period_count, frame_size_fcr, miid)) {
    //         printf("Failed to configure Alsa Sink\n");
    //         // return -1;
    //     }
    // }
    // else {
    //     printf("Alsa Sink not present in this graph\n");
    //     return -1; //VIC the Alsa Sink is absolutely needed on the Rpi
    // }
    
    printf("\n");

    return 0;
}


void cleanup_agm(void)
{   
    if (g_mixer != NULL) {
        connect_agm_frontend_to_backend(g_mixer, g_frontend_name, g_backend_name, false);
        mixer_close(g_mixer);
    }

    if (g_frontend_name != NULL) {
        free(g_frontend_name);
    }

    if (g_backend_name != NULL) {
        free(g_backend_name);
    }
}