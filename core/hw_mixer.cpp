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
#define MAX_ITEMS 128
#define MAX_PATHS 64
#define MAX_NAME 64
#define MAX_VALUE 32

struct ctl_setting {
    char name[MAX_NAME];
    char value[MAX_VALUE];
};

// one entry in a path body, in document order: either an inline <ctl> or a
// <path name="X"/> include. Includes are resolved lazily (after the whole file
// is parsed) so forward references work.
struct path_item {
    bool is_include;
    char name[MAX_NAME];    // ctl name, or included path name
    char value[MAX_VALUE];  // ctl value (unused when is_include)
};

struct path {
    char name[MAX_NAME];
    struct path_item items[MAX_ITEMS];  // raw body, in order (parse phase)
    int item_count;
    struct ctl_setting ctls[MAX_CTLS];  // flattened controls (resolve phase)
    int ctl_count;
    int resolved;                       // ctls[] filled? (memoize resolution)
    int resolving;                      // on the current recursion stack (cycle guard)
};

static struct mixer *g_mixer = NULL;
static struct ctl_setting g_defaults[MAX_CTLS];
static int g_default_count = 0;
static struct path g_paths[MAX_PATHS];
static int g_path_count = 0;

// XML parsing state
static int path_depth = 0;
static struct path *current_path = NULL;

// find a parsed top-level path by name. Called after parsing completes, so both
// backward and forward includes resolve.
static struct path *find_path(const char *name)
{
    for (int i = 0; i < g_path_count; i++) {
        if (strcmp(g_paths[i].name, name) == 0)
            return &g_paths[i];
    }
    return NULL;
}

static void path_add_ctl(struct path *p, const char *name, const char *value)
{
    if (p->ctl_count >= MAX_CTLS) {
        fprintf(stderr, "hw_mixer: path '%s' exceeds %d ctls; truncated\n", p->name, MAX_CTLS);
        return;
    }
    struct ctl_setting *s = &p->ctls[p->ctl_count++];
    strncpy(s->name, name, MAX_NAME - 1);
    strncpy(s->value, value, MAX_VALUE - 1);
}

// flatten a path's body into ctls[], expanding includes in order. Recursive so
// transitive includes work; memoized via ->resolved; cycle-guarded via ->resolving.
// Warnings here only surface for paths actually resolved (i.e. actually applied).
static void resolve_path(struct path *p)
{
    if (p->resolved)
        return;
    if (p->resolving) {
        fprintf(stderr, "hw_mixer: include cycle at path '%s'; skipped\n", p->name);
        return;
    }
    p->resolving = 1;
    p->ctl_count = 0;

    for (int i = 0; i < p->item_count; i++) {
        struct path_item *it = &p->items[i];
        if (!it->is_include) {
            path_add_ctl(p, it->name, it->value);
            continue;
        }
        struct path *src = find_path(it->name);
        if (!src || src == p) {
            fprintf(stderr, "hw_mixer: path '%s' includes unknown path '%s'; skipped\n",
                    p->name, it->name);
            continue;
        }
        resolve_path(src);
        for (int j = 0; j < src->ctl_count; j++)
            path_add_ctl(p, src->ctls[j].name, src->ctls[j].value);
    }

    p->resolving = 0;
    p->resolved = 1;
}

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
        path_depth++;
        if (path_depth == 1) {
            if (g_path_count >= MAX_PATHS) return;
            current_path = &g_paths[g_path_count++];
            current_path->item_count = 0;
            current_path->ctl_count = 0;
            current_path->resolved = 0;
            current_path->resolving = 0;
            current_path->name[0] = '\0';
            for (int i = 0; attr[i]; i += 2) {
                if (strcmp(attr[i], "name") == 0)
                    strncpy(current_path->name, attr[i+1], MAX_NAME - 1);
            }
        }
        // a nested <path name="X"/> is an include: record it as an item; it is
        // resolved later (after the whole file is parsed) so forward references work
        else if (path_depth == 2 && current_path) {
            const char *inc = NULL;
            for (int i = 0; attr[i]; i += 2) {
                if (strcmp(attr[i], "name") == 0) inc = attr[i+1];
            }
            if (inc && current_path->item_count < MAX_ITEMS) {
                struct path_item *it = &current_path->items[current_path->item_count++];
                it->is_include = true;
                strncpy(it->name, inc, MAX_NAME - 1);
            }
        }
    }
    else if (strcmp(el, "ctl") == 0) {
        const char *name = NULL;
        const char *value = NULL;

        for (int i = 0; attr[i]; i += 2) {
            if (strcmp(attr[i], "name") == 0) name = attr[i+1];
            else if (strcmp(attr[i], "value") == 0) value = attr[i+1];
        }

        if (name && value) {
            if (path_depth > 0 && current_path) {
                if (current_path->item_count < MAX_ITEMS) {
                    struct path_item *it = &current_path->items[current_path->item_count++];
                    it->is_include = false;
                    strncpy(it->name, name, MAX_NAME - 1);
                    strncpy(it->value, value, MAX_VALUE - 1);
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
        path_depth--;
        if (path_depth == 0)
            current_path = NULL;
    }
}

int init_hw_mixer(const char *mixer_path_xml, unsigned int card)
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

    struct path *p = find_path(path_name);
    if (!p) {
        fprintf(stderr, "hw_mixer: path '%s' not found\n", path_name);
        return -1;
    }

    // flatten includes now (only for the path we actually apply, so unresolved-
    // include warnings surface only for paths in use)
    resolve_path(p);

    // an empty path applies nothing: the failure would otherwise surface far away
    // (e.g. at pcm_start), so flag it loudly here instead
    if (p->ctl_count == 0) {
        fprintf(stderr, "hw_mixer: warning: path '%s' has no controls "
                "(empty or unresolved include); nothing applied\n", path_name);
        return 0;
    }

    for (int j = 0; j < p->ctl_count; j++)
        apply_ctl(p->ctls[j].name, p->ctls[j].value);

    printf("hw_mixer: applied path '%s' (%d ctls)\n\n", path_name, p->ctl_count);
    return 0;
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