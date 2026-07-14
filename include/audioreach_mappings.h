#ifndef AUDIOREACH_MAPPINGS_H
#define AUDIOREACH_MAPPINGS_H

#include <stdint.h>
#include <stddef.h> //VIC need only in C
#include <string.h>

/* Structure for key-value mapping */
typedef struct {
    uint32_t key;
    const char* name;
} KeyMapping;

/* AllKeyIds mapping table */
static const KeyMapping allkeyids_map[] = {
    { 0xA1000000, "STREAMRX" },
    { 0xA2000000, "DEVICERX" },
    { 0xA3000000, "DEVICETX" },
    { 0xA4000000, "VOLUME" },
    { 0xA5000000, "SAMPLINGRATE" },
    { 0xA6000000, "BITWIDTH" },
    { 0xA7000000, "PAUSE" },
    { 0xA8000000, "MUTE" },
    { 0xA9000000, "CHANNELS" },
    { 0xAA000000, "ECNS" },
    { 0xAB000000, "INSTANCE" },
    { 0xAC000000, "DEVICEPP_RX" },
    { 0xAD000000, "DEVICEPP_TX" },
    { 0xAE000000, "MEDIA_FMT_ID" },
    { 0xAF000000, "STREAMPP_RX" },
    { 0xB0000000, "STREAMPP_TX" },
    { 0xB1000000, "STREAMTX" },
    { 0xB2000000, "EQUALIZER_SWITCH" },
    { 0xB3000000, "VSID" },
    { 0xB4000000, "BT_PROFILE" },
    { 0xB5000000, "BT_FORMAT" },
    { 0xB6000000, "PBE_SWITCH" },
    { 0xB7000000, "BASS_BOOST_SWITCH" },
    { 0xB8000000, "REVERB_SWITCH" },
    { 0xB9000000, "VIRTUALIZER_SWITCH" },
    { 0xBA000000, "SW_SIDETONE" },
    { 0xBB000000, "TAG_KEY_SLOW_TALK" },
    { 0xBC000000, "STREAM_CONFIG" },
    { 0xBD000000, "TAG_KEY_MUX_DEMUX_CONFIG" },
    { 0xBE000000, "SPK_PRO_DEV_MAP" },
    { 0xBF000000, "SPK_PRO_VI_MAP" },
    { 0xD0000000, "RAS_SWITCH" },
    { 0xD1000000, "PROXY_TX_TYPE" },
    { 0xD2000000, "GAIN" },
    { 0xD3000000, "STREAM" },
    { 0xD4000000, "STREAM_CHANNELS" },
    { 0xD5000000, "ICL" },
    { 0xD6000000, "ASPHERE_SWITCH" },
    { 0xD7000000, "DEVICETX_EXT" },
    { 0xD8000000, "LOGGING" },
    { 0xD9000000, "BMT" },
    { 0xDA000000, "FNB" },
    { 0xDB000000, "SUMX" },
    { 0xDC000000, "AVC" },
    { 0xDD000000, "VMI" },
    { 0xDE000000, "TAG_KEY_DTMF_SWITCH" },
    { 0xDF000000, "TAG_KEY_DTMF_GEN_TONE" },
    { 0xE0000000, "TAG_KEY_SLOT_MASK" },
    { 0xE1000000, "TAG_KEY_DUTY_CYCLE" },
    { 0xE2000000, "TAG_KEY_ORIENTATION" },
    { 0xE3000000, "SPK_PRO_CPS_MAP" },
    { 0xE4000000, "HAPTICS_PRO_VI_MAP" },
    { 0xE5000000, "HAPTICS_PRO_DEV_MAP" },
    { 0xE6000000, "USB_VENDOR_ID" },
    { 0xE7000000, "TAG_KEY_ULTRASOUND_GAIN" },
    { 0xE7010000, "PROXY_RX_TYPE" },
};

#define ALLKEYIDS_MAP_SIZE (sizeof(allkeyids_map) / sizeof(allkeyids_map[0]))

/* StreamRX mapping table */
static const KeyMapping streamrx_map[] = {
    { 0xA1000001, "PCM_DEEP_BUFFER" },
    { 0xA1000003, "PCM_RX_LOOPBACK" },
    { 0xA1000005, "VOIP_RX_PLAYBACK" },
    { 0xA100000A, "COMPRESSED_OFFLOAD_PLAYBACK" },
    { 0xA100000C, "HFP_RX_PLAYBACK" },
    { 0xA100000D, "HFP_TX_PLAYBACK" },
    { 0xA100000E, "PCM_LL_PLAYBACK" },
    { 0xA100000F, "PCM_OFFLOAD_PLAYBACK" },
    { 0xA1000010, "VOICE_CALL_RX" },
    { 0xA1000011, "PCM_ULL_PLAYBACK" },
    { 0xA1000012, "PCM_PROXY_PLAYBACK" },
    { 0xA1000013, "INCALL_MUSIC" },
    { 0xA1000014, "GENERIC_PLAYBACK" },
    { 0xA1000015, "HAPTICS_PLAYBACK" },
    { 0xA1000016, "VOICE_CALL_RX_HPCM_PLAYBACK" },
    { 0xA1000017, "VOICE_CALL_TX_HPCM_PLAYBACK" },
    { 0xA1000018, "SPATIAL_AUDIO_PLAYBACK" },
    { 0xA1000019, "RAW_PLAYBACK" },
    { 0xA100001A, "INCALL_MUSIC_PLUS" },
    { 0xA100001B, "INCALL_MUSIC_COMPRESS_UPLINK" },
    { 0xA100001C, "INCALL_MUSIC_COMPRESS_DOWNLINK" },
};

#define STREAMRX_MAP_SIZE (sizeof(streamrx_map) / sizeof(streamrx_map[0]))

/* DeviceRX mapping table */
static const KeyMapping devicerx_map[] = {
    { 0xA2000001, "SPEAKER" },
    { 0xA2000002, "HEADPHONES" },
    { 0xA2000003, "BT_RX" },
    { 0xA2000004, "HANDSET" },
    { 0xA2000005, "USB_RX" },
    { 0xA2000006, "HDMI_RX" },
    { 0xA2000007, "PROXY_RX" },
    { 0xA2000008, "PROXY_RX_VOICE" },
    { 0xA2000009, "HAPTICS_DEVICE" },
    { 0xA200000A, "ULTRASOUND_RX" },
    { 0xA200000B, "ULTRASOUND_RX_DEDICATED" },
    { 0xA200000C, "DUMMY_RX" },
};

#define DEVICERX_MAP_SIZE (sizeof(devicerx_map) / sizeof(devicerx_map[0]))

/* Instance mapping table */
static const KeyMapping instance_map[] = {
    { 1, "INSTANCE_1" },
    { 2, "INSTANCE_2" },
    { 3, "INSTANCE_3" },
    { 4, "INSTANCE_4" },
    { 5, "INSTANCE_5" },
    { 6, "INSTANCE_6" },
    { 7, "INSTANCE_7" },
    { 8, "INSTANCE_8" },
};

#define INSTANCE_MAP_SIZE (sizeof(instance_map) / sizeof(instance_map[0]))

/* DevicePP_RX mapping table */
static const KeyMapping devicepp_rx_map[] = {
    { 0xAC000001, "DEVICEPP_RX_DEFAULT" },
    { 0xAC000002, "DEVICEPP_RX_AUDIO_MBDRC" },
    { 0xAC000003, "DEVICEPP_RX_VOIP_MBDRC" },
    { 0xAC000004, "DEVICEPP_RX_HFPSINK" },
    { 0xAC000005, "DEVICEPP_RX_VOICE_DEFAULT" },
    { 0xAC000006, "DEVICEPP_RX_ULTRASOUND_GENERATOR" },
    { 0xAC000007, "DEVICEPP_RX_VOICE_RVE" },
    { 0xAC000008, "DEVICEPP_RX_HPCM" },
    { 0xAC000009, "DEVICEPP_RX_VOICE_Fluence_NN_NS" },
    { 0xAC00000A, "DEVICEPP_RX_VOIP_Fluence_NN_NS" },
    { 0xAC00000B, "DEVICEPP_RX_AUDIO_MSPP" },
    { 0xAC00000C, "DEVICEPP_RX_BTSINK" },
    { 0xAC00000D, "DEVICEPP_RX_HAPTICS_GENERATOR" },
};

#define DEVICEPP_RX_MAP_SIZE (sizeof(devicepp_rx_map) / sizeof(devicepp_rx_map[0]))

/* StreamPP_RX mapping table */
static const KeyMapping streampp_rx_map[] = {
    {0xAF000001, "STREAMPP_RX_DEFAULT"},
};

#define STREAMPP_RX_MAP_SIZE (sizeof(streampp_rx_map) / sizeof(streampp_rx_map[0]))

/* StreamTX mapping table */
static const KeyMapping streamtx_map[] = {
    { 0xB1000001, "PCM_RECORD" },
    { 0xB1000002, "PCM_TX_LOOPBACK" },
    { 0xB1000003, "VOICE_UI" },
    { 0xB1000004, "VOIP_TX_RECORD" },
    { 0xB1000005, "HFP_RX_CAPTURE" },
    { 0xB1000006, "HFP_TX_CAPTURE" },
    { 0xB1000007, "VOICE_CALL_TX" },
    { 0xB1000008, "DEEPBUFFER_RECORD" },
    { 0xB1000009, "RAW_RECORD" },
    { 0xB100000A, "PCM_ULL_RECORD" },
    { 0xB100000B, "PCM_PROXY_RECORD" },
    { 0xB100000C, "INCALL_RECORD" },
    { 0xB100000D, "ACD" },
    { 0xB100000E, "SENSOR_PCM_DATA" },
    { 0xB100000F, "VOICE_CALL_RX_HPCM_RECORD" },
    { 0xB1000010, "VOICE_CALL_TX_HPCM_RECORD" },
    { 0xB1000011, "VOICE_RECOGNITION_RECORD" },
    { 0xB1000012, "COMPRESS_CAPTURE" },
};

#define STREAMTX_MAP_SIZE (sizeof(streamtx_map) / sizeof(streamtx_map[0]))

/* DeviceTX mapping table */
static const KeyMapping devicetx_map[] = {
    { 0xA3000001, "SPEAKER_MIC" },
    { 0xA3000002, "BT_TX" },
    { 0xA3000003, "HEADPHONE_MIC" },
    { 0xA3000004, "HANDSETMIC" },
    { 0xA3000005, "USB_TX" },
    { 0xA3000006, "HANDSETMIC_VA" },
    { 0xA3000007, "HEADSETMIC_VA" },
    { 0xA3000008, "PROXY_TX" },
    { 0xA3000009, "VI_TX" },
    { 0xA300000A, "FM_TX" },
    { 0xA300000B, "ULTRASOUND_TX" },
    { 0xA300000C, "HAPTICS_VI_TX" },
    { 0xA300000D, "ECHO_REF_TX" },
    { 0xA300000E, "CPS_TX" },
    { 0xA300000F, "CPS2_TX" },
    { 0xA3000010, "DUMMY_TX" },
};

#define DEVICETX_MAP_SIZE (sizeof(devicetx_map) / sizeof(devicetx_map[0]))

/* DevicePP_TX mapping table */
static const KeyMapping devicepp_tx_map[] = {
    { 0xAD000001, "DEVICEPP_TX_FLUENCE_FFECNS" },
    { 0xAD000002, "DEVICEPP_TX_AUDIO_FLUENCE_SMECNS" },
    { 0xAD000003, "DEVICEPP_TX_AUDIO_FLUENCE_ENDFIRE" },
    { 0xAD000004, "DEVICEPP_TX_AUDIO_FLUENCE_PRO" },
    { 0xAD000005, "DEVICEPP_TX_VOIP_FLUENCE_PRO" },
    { 0xAD000006, "DEVICEPP_TX_HFP_SINK_FLUENCE_SMECNS" },
    { 0xAD000007, "DEVICEPP_TX_VOIP_FLUENCE_SMECNS" },
    { 0xAD000008, "DEVICEPP_TX_VOICE_FLUENCE_SMECNS" },
    { 0xAD000009, "DEVICEPP_TX_VOICE_FLUENCE_ENDFIRE" },
    { 0xAD00000A, "DEVICEPP_TX_VOICE_FLUENCE_PRO" },
    { 0xAD00000B, "DEVICEPP_TX_FLUENCE_FFNS" },
    { 0xAD00000C, "DEVICEPP_TX_RAW_LPI" },
    { 0xAD00000D, "DEVICEPP_TX_VOIP_FLUENCE_ENDFIRE" },
    { 0xAD00000E, "DEVICEPP_TX_RAW_NLPI" },
    { 0xAD00000F, "DEVICEPP_TX_VOICE_FLUENCE_NN" },
    { 0xAD000010, "DEVICEPP_TX_VOIP_FLUENCE_NN" },
    { 0xAD000011, "DEVICEPP_TX_ULTRASOUND_DETECTOR" },
    { 0xAD000012, "DEVICEPP_TX_FLUENCE_FFEC" },
    { 0xAD000013, "DEVICEPP_TX_VOICE_FLUENCE_ENDFIRE_RVE" },
    { 0xAD000014, "DEVICEPP_TX_HPCM" },
    { 0xAD000015, "DEVICEPP_TX_HFP_SINK_FLUENCE_ENDFIRE" },
    { 0xAD000016, "DEVICEPP_TX_AUDIO_FLUENCE_NN" },
    { 0xAD000017, "DEVICEPP_TX_VOICE_RECOGNITION" },
    { 0xAD000018, "DEVICEPP_TX_HFP_SINK_FLUENCE_NN_SM" },
    { 0xAD000019, "DEVICEPP_TX_AAD" },
    { 0xAD00001A, "DEVICEPP_TX_FLUENCE_FFNS_AAD" },
    { 0xAD00001B, "DEVICEPP_TX_RAW_LPI_AAD" },
    { 0xAD00001C, "DEVICEPP_TX_VOICE_FLUENCE_NN_DM" },
    { 0xAD00001D, "DEVICEPP_TX_HFP_SINK_FLUENCE_NN_DM" },
    { 0xAD00001E, "DEVICEPP_TX_AUDIO_FLUENCE_NN_DM" },
    { 0xAD00001F, "DEVICEPP_TX_VOICE_FLUENCE_ENDFIRE_FNN_NS" },
    { 0xAD000020, "DEVICEPP_TX_VOICE_FLUENCE_PRO_FNN_NS" },
    { 0xAD000021, "DEVICEPP_TX_VOIP_FLUENCE_ENDFIRE_FNN_NS" },
    { 0xAD000022, "DEVICEPP_TX_VOIP_FLUENCE_PRO_FNN_NS" },
    { 0xAD000023, "DEVICEPP_TX_AUDIO_RECORD_ENQORE" },
    { 0xAD000024, "DEVICEPP_TX_VOICE_FLUENCE_NN_RVE" },
    { 0xAD000025, "DEVICEPP_TX_CUSTOM_ECNS" },
    { 0xAD000026, "DEVICEPP_TX_CUSTOM_NS" },
    { 0xAD000027, "DEVICEPP_TX_AUDIO_FLUENCE_SMECNS_EXT_EC" },
    { 0xAD000028, "DEVICEPP_TX_AUDIO_FLUENCE_ENDFIRE_EXT_EC" },
    { 0xAD000029, "DEVICEPP_TX_AUDIO_FLUENCE_PRO_EXT_EC" },
    { 0xAD00002A, "DEVICEPP_TX_VOIP_FLUENCE_SMECNS_EXT_EC" },
    { 0xAD00002B, "DEVICEPP_TX_VOIP_FLUENCE_ENDFIRE_EXT_EC" },
    { 0xAD00002C, "DEVICEPP_TX_VOIP_FLUENCE_PRO_EXT_EC" },
    { 0xAD00002D, "DEVICEPP_TX_VOICE_FLUENCE_SMECNS_EXT_EC" },
    { 0xAD00002E, "DEVICEPP_TX_VOICE_FLUENCE_ENDFIRE_EXT_EC" },
    { 0xAD00002F, "DEVICEPP_TX_VOICE_FLUENCE_PRO_EXT_EC" },
    { 0xAD000030, "DEVICEPP_TX_EC_REF" },
};

#define DEVICEPP_TX_MAP_SIZE (sizeof(devicepp_tx_map) / sizeof(devicepp_tx_map[0]))

/* StreamPP_TX mapping table */
static const KeyMapping streampp_tx_map[] = {
    { 0xB0000001, "STREAMPP_TX_DEFAULT" },
};

#define STREAMPP_TX_MAP_SIZE (sizeof(streampp_tx_map) / sizeof(streampp_tx_map[0]))

/* Tags definitions mapping table */
static const KeyMapping tags_map[] = {
    { 0xC0000001, "SHMEM_ENDPOINT" },
    { 0xC0000002, "STREAM_INPUT_MEDIA_FORMAT" },
    { 0xC0000003, "STREAM_OUTPUT_MEDIA_FORMAT" },
    { 0xC0000008, "DEVICE_SVA" },
    { 0xC0000009, "DEVICE_ADAM" },
    { 0xC000000C, "DEVICE_MFC" },
    { 0xC000000E, "STREAM_PCM_DECODER" },
    { 0xC000000F, "STREAM_PCM_ENCODER" },
    { 0xC0000010, "STREAM_PCM_CONVERTER" },
    { 0xC0000013, "STREAM_SPR" },
    { 0xC0000020, "BT_PLACEHOLDER_ENCODER" },
    { 0xC0000021, "COP_PACKETIZER_V0" },
    { 0xC0000022, "RAT_RENDER" },
    { 0xC0000023, "BT_PCM_CONVERTER" },
    { 0xC0000024, "BT_PLACEHOLDER_DECODER" },
    { 0xC0000028, "MODULE_VI" },
    { 0xC0000029, "MODULE_SP" },
    { 0xC000002A, "MODULE_GAPLESS" },
    { 0xC000002C, "WR_SHMEM_ENDPOINT" },
    { 0xC000002E, "RD_SHMEM_ENDPOINT" },
    { 0xC000002F, "COP_PACKETIZER_V2" },
    { 0xC0000030, "COP_DEPACKETIZER_V2" },
    { 0xC0000031, "CONTEXT_DETECTION_ENGINE" },
    { 0xC0000032, "ULTRASOUND_DETECTION_MODULE" },
    { 0xC000003A, "DEVICE_POP_SUPPRESSOR" },
    { 0xC000003D, "DEVICE_PP_MSIIR" },
    { 0xC000003E, "MODULE_HAPTICS_VI" },
    { 0xC000003F, "MODULE_HAPTICS_GEN" },
    { 0xC0000043, "TAG_MODULE_MSPP" },
    { 0xC0000044, "TAG_MODULE_CPS" },
    { 0xC0000045, "MODULE_CONGESTION_BUFFER" },
    { 0xC0000046, "MODULE_JITTER_BUFFER" },
    { 0xC0000047, "MODULE_VI2" },
    { 0xC0000048, "MODULE_SP2" },
    { 0xC0000049, "TAG_MODULE_CPS2" },
    { 0xC000004B, "TAG_MODULE_TSM" },
    { 0xC0000040, "TAG_DEVICE_MUX" },
    { 0xC0000004, "DEVICE_HW_ENDPOINT_RX" },
    { 0xC0000019, "PER_STREAM_PER_DEVICE_MFC" },
};

#define TAGS_MAP_SIZE (sizeof(tags_map) / sizeof(tags_map[0]))

/* Helper function to get AllKeyIds name from key */
static inline const char* get_key_name(uint32_t key) {
    for (size_t i = 0; i < ALLKEYIDS_MAP_SIZE; i++) {
        if (allkeyids_map[i].key == key) {
            return allkeyids_map[i].name;
        }
    }
    return NULL;
}

/* Helper function to get StreamRX name from key */
static inline const char* get_streamrx_name(uint32_t key) {
    for (size_t i = 0; i < STREAMRX_MAP_SIZE; i++) {
        if (streamrx_map[i].key == key) {
            return streamrx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get DeviceRX name from key */
static inline const char* get_devicerx_name(uint32_t key) {
    for (size_t i = 0; i < DEVICERX_MAP_SIZE; i++) {
        if (devicerx_map[i].key == key) {
            return devicerx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get Instance name from value */
static inline const char* get_instance_name(uint32_t value) {
    for (size_t i = 0; i < INSTANCE_MAP_SIZE; i++) {
        if (instance_map[i].key == value) {
            return instance_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get DevicePP_RX name from key */
static inline const char* get_devicepp_rx_name(uint32_t key) {
    for (size_t i = 0; i < DEVICEPP_RX_MAP_SIZE; i++) {
        if (devicepp_rx_map[i].key == key) {
            return devicepp_rx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get StreamPP_RX name from key */
static inline const char* get_streampp_rx_name(uint32_t key) {
    for (size_t i = 0; i < STREAMPP_RX_MAP_SIZE; i++) {
        if (streampp_rx_map[i].key == key) {
            return streampp_rx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get StreamTX name from key */
static inline const char* get_streamtx_name(uint32_t key) {
    for (size_t i = 0; i < STREAMTX_MAP_SIZE; i++) {
        if (streamtx_map[i].key == key) {
            return streamtx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get DeviceTX name from key */
static inline const char* get_devicetx_name(uint32_t key) {
    for (size_t i = 0; i < DEVICETX_MAP_SIZE; i++) {
        if (devicetx_map[i].key == key) {
            return devicetx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get DevicePP_TX name from key */
static inline const char* get_devicepp_tx_name(uint32_t key) {
    for (size_t i = 0; i < DEVICEPP_TX_MAP_SIZE; i++) {
        if (devicepp_tx_map[i].key == key) {
            return devicepp_tx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get StreamPP_TX name from key */
static inline const char* get_streampp_tx_name(uint32_t key) {
    for (size_t i = 0; i < STREAMPP_TX_MAP_SIZE; i++) {
        if (streampp_tx_map[i].key == key) {
            return streampp_tx_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Helper function to get tag name from key */
static inline const char* get_tag_name(uint32_t key) {
    for (size_t i = 0; i < TAGS_MAP_SIZE; i++) {
        if (tags_map[i].key == key) {
            return tags_map[i].name;
        }
    }
    return "UNKNOWN";
}

/* Generic helper function to get name from any AudioReach key/value */
static inline const char* get_audioreach_name(uint32_t key_or_value) {
    const char* name;
    
    /* First check if it's an exact AllKeyIds match */
    name = get_key_name(key_or_value);
    if (name != NULL) {
        return name;
    }
    
    /* Check if it's a StreamRX key (0xA1xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xA1000000) {
        return get_streamrx_name(key_or_value);
    }
    
    /* Check if it's a DeviceRX key (0xA2xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xA2000000) {
        return get_devicerx_name(key_or_value);
    }

    /* Check if it's a DeviceTX key (0xA3xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xA3000000) {
        return get_devicetx_name(key_or_value);
    }

    /* Check if it's a DevicePP_RX key (0xACxxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xAC000000) {
        return get_devicepp_rx_name(key_or_value);
    }

    /* Check if it's a DevicePP_TX key (0xADxxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xAD000000) {
        return get_devicepp_tx_name(key_or_value);
    }

    /* Check if it's a StreamPP_RX key (0xAFxxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xAF000000) {
        return get_streampp_rx_name(key_or_value);
    }

    /* Check if it's a StreamPP_TX key (0xB0xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xB0000000) {
        return get_streampp_tx_name(key_or_value);
    }

    /* Check if it's a StreamTX key (0xB1xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xB1000000) {
        return get_streamtx_name(key_or_value);
    }

    /* Check if it's a Tags key (0xC0xxxxxx) */
    if ((key_or_value & 0xFF000000) == 0xC0000000) {
        return get_tag_name(key_or_value);
    }

    /* Check if it's an Instance key (0x00000xxx) - instance values follow different logic*/
    if ((key_or_value & 0xFFFFF000) == 0x00000000) {
        /* Extract the instance value (lower bytes) and look it up */
        uint32_t instance_val = key_or_value & 0x00FFFFFF;
        return get_instance_name(instance_val);
    }
    
    return "UNKNOWN";
}

/* ============================================ */
/* REVERSE MAPPING FUNCTIONS - String to Value */
/* ============================================ */

/* Helper function to get StreamRX value from name */
static inline uint32_t get_streamrx_value(const char* name) {
    if (name == NULL) return 0;
    
    for (size_t i = 0; i < STREAMRX_MAP_SIZE; i++) {
        if (strcmp(streamrx_map[i].name, name) == 0) {
            return streamrx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get StreamPP_RX value from name */
static inline uint32_t get_streampp_rx_value(const char* name) {
    if (name == NULL) return 0;
    
    for (size_t i = 0; i < STREAMPP_RX_MAP_SIZE; i++) {
        if (strcmp(streampp_rx_map[i].name, name) == 0) {
            return streampp_rx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get DevicePP_RX value from name */
static inline uint32_t get_device_pp_rx_value(const char* name) {
    if (name == NULL) return 0;
    
    for (size_t i = 0; i < DEVICEPP_RX_MAP_SIZE; i++) {
        if (strcmp(devicepp_rx_map[i].name, name) == 0) {
            return devicepp_rx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get DeviceRX value from name */
static inline uint32_t get_device_rx_value(const char* name) {
    if (name == NULL) return 0;
    
    for (size_t i = 0; i < DEVICERX_MAP_SIZE; i++) {
        if (strcmp(devicerx_map[i].name, name) == 0) {
            return devicerx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get StreamTX value from name */
static inline uint32_t get_streamtx_value(const char* name) {
    if (name == NULL) return 0;

    for (size_t i = 0; i < STREAMTX_MAP_SIZE; i++) {
        if (strcmp(streamtx_map[i].name, name) == 0) {
            return streamtx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get StreamPP_TX value from name */
static inline uint32_t get_streampp_tx_value(const char* name) {
    if (name == NULL) return 0;

    for (size_t i = 0; i < STREAMPP_TX_MAP_SIZE; i++) {
        if (strcmp(streampp_tx_map[i].name, name) == 0) {
            return streampp_tx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get DevicePP_TX value from name */
static inline uint32_t get_device_pp_tx_value(const char* name) {
    if (name == NULL) return 0;

    for (size_t i = 0; i < DEVICEPP_TX_MAP_SIZE; i++) {
        if (strcmp(devicepp_tx_map[i].name, name) == 0) {
            return devicepp_tx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get DeviceTX value from name */
static inline uint32_t get_device_tx_value(const char* name) {
    if (name == NULL) return 0;

    for (size_t i = 0; i < DEVICETX_MAP_SIZE; i++) {
        if (strcmp(devicetx_map[i].name, name) == 0) {
            return devicetx_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

/* Helper function to get Instance value from name */
static inline uint32_t get_instance_value(const char* name) {
    if (name == NULL) return 0;
    
    for (size_t i = 0; i < INSTANCE_MAP_SIZE; i++) {
        if (strcmp(instance_map[i].name, name) == 0) {
            return instance_map[i].key;
        }
    }
    return 0; /* Return 0 if not found */
}

#endif /* AUDIOREACH_MAPPINGS_H */