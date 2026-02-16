/*
 * Copyright 2026 Victor Zappi
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hw_mixer.h"
#include <tinyalsa/asoundlib.h>
#include <expat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CTLS 128
#define MAX_PATHS 32
#define MAX_NAME 64
#define MAX_VALUE 32

struct ctl_setting {
    char name[MAX_NAME];
    char value[MAX_VALUE];
};

struct path {
    char name[MAX_NAME];
    struct ctl_setting ctls[MAX_CTLS];
    int ctl_count;
};

static struct mixer *g_mixer = NULL;
static struct ctl_setting g_defaults[MAX_CTLS];
static int g_default_count = 0;
static struct path g_paths[MAX_PATHS];
static int g_path_count = 0;

// XML parsing state
static int in_path = 0;
static struct path *current_path = NULL;

static void apply_ctl(const char *name, const char *value)
{
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(g_mixer, name);
    if (!ctl) {
        //fprintf(stderr, "mixer: ctl '%s' not found\n", name);
        return;
    }

    enum mixer_ctl_type type = mixer_ctl_get_type(ctl);
    if (type == MIXER_CTL_TYPE_ENUM) {
        if (mixer_ctl_set_enum_by_string(ctl, value) < 0) {
            // fprintf(stderr, "mixer: failed to set enum '%s' to '%s'\n", name, value);
        }
    } else {
        // INT, BOOL
        mixer_ctl_set_value(ctl, 0, atoi(value));
    }
}

static void XMLCALL xml_start(void *data, const char *el, const char **attr)
{
    (void)data;

    if (strcmp(el, "path") == 0) {
        if (g_path_count >= MAX_PATHS) return;
        current_path = &g_paths[g_path_count++];
        current_path->ctl_count = 0;
        current_path->name[0] = '\0';
        
        for (int i = 0; attr[i]; i += 2) {
            if (strcmp(attr[i], "name") == 0) {
                strncpy(current_path->name, attr[i+1], MAX_NAME - 1);
            }
        }
        in_path = 1;
    }
    else if (strcmp(el, "ctl") == 0) {
        const char *name = NULL;
        const char *value = NULL;
        
        for (int i = 0; attr[i]; i += 2) {
            if (strcmp(attr[i], "name") == 0) name = attr[i+1];
            else if (strcmp(attr[i], "value") == 0) value = attr[i+1];
        }
        
        if (name && value) {
            if (in_path && current_path) {
                if (current_path->ctl_count < MAX_CTLS) {
                    struct ctl_setting *s = &current_path->ctls[current_path->ctl_count++];
                    strncpy(s->name, name, MAX_NAME - 1);
                    strncpy(s->value, value, MAX_VALUE - 1);
                }
            } else {
                if (g_default_count < MAX_CTLS) {
                    struct ctl_setting *s = &g_defaults[g_default_count++];
                    strncpy(s->name, name, MAX_NAME - 1);
                    strncpy(s->value, value, MAX_VALUE - 1);
                }
            }
        }
    }
}

static void XMLCALL xml_end(void *data, const char *el)
{
    (void)data;
    if (strcmp(el, "path") == 0) {
        in_path = 0;
        current_path = NULL;
    }
}

int init_hw_mixer(unsigned int card, const char *mixer_path_xml)
{
    g_mixer = mixer_open(card);
    if (!g_mixer) {
        fprintf(stderr, "mixer: failed to open card %u\n", card);
        return -1;
    }

    // Parse XML
    FILE *f = fopen(mixer_path_xml, "r");
    if (!f) {
        fprintf(stderr, "mixer: failed to open %s\n", mixer_path_xml);
        mixer_close(g_mixer);
        g_mixer = NULL;
        return -1;
    }

    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetElementHandler(parser, xml_start, xml_end);

    char buf[4096];
    int done = 0;
    while (!done) {
        size_t len = fread(buf, 1, sizeof(buf), f);
        done = len < sizeof(buf);
        if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
            fprintf(stderr, "mixer: XML parse error at line %lu\n",
                    XML_GetCurrentLineNumber(parser));
            XML_ParserFree(parser);
            fclose(f);
            mixer_close(g_mixer);
            g_mixer = NULL;
            return -1;
        }
    }

    XML_ParserFree(parser);
    fclose(f);

    // Apply defaults
    for (int i = 0; i < g_default_count; i++) {
        apply_ctl(g_defaults[i].name, g_defaults[i].value);
    }

    return 0;
}

int set_hw_mixer_path(const char *path_name)
{
    if (!g_mixer) return -1;

    for (int i = 0; i < g_path_count; i++) {
        if (strcmp(g_paths[i].name, path_name) == 0) {
            for (int j = 0; j < g_paths[i].ctl_count; j++) {
                apply_ctl(g_paths[i].ctls[j].name, g_paths[i].ctls[j].value);
            }
            return 0;
        }
    }

    //fprintf(stderr, "mixer: path '%s' not found\n", path_name);
    return -1;
}

void cleanup_hw_mixer(void)
{
    if (!g_mixer) return;

    for (int i = g_default_count - 1; i >= 0; i--) {
        apply_ctl(g_defaults[i].name, g_defaults[i].value);
    }

    mixer_close(g_mixer);
    g_mixer = NULL;
    g_default_count = 0;
    g_path_count = 0;
}