// Trusted plugins: registry, config access and running instances (see plugin.h).
#include "mods/plugin.h"

#include "mods/mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const bb_plugin_t* registry[PLUGIN_MAX];
static int registry_count;

bool plugin_name_valid(const char* name) {
    if (!name || !name[0] || strlen(name) >= MOD_ID_MAX) return false;
    for (const char* c = name; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) return false;
    }
    return true;
}

bool plugin_register(const bb_plugin_t* p) {
    if (!p || p->api != BB_PLUGIN_API || !plugin_name_valid(p->name)) return false;
    if (registry_count >= PLUGIN_MAX || plugin_find(p->name)) return false;
    registry[registry_count++] = p;
    return true;
}

const bb_plugin_t* plugin_find(const char* name) {
    for (int i = 0; i < registry_count; i++) {
        if (!strcmp(registry[i]->name, name)) return registry[i];
    }
    return NULL;
}

int plugin_count(void) {
    return registry_count;
}

const bb_plugin_t* plugin_get(int index) {
    return index >= 0 && index < registry_count ? registry[index] : NULL;
}

void plugin_unregister_all(void) {
    registry_count = 0;
}

// ---- config -----------------------------------------------------------------------

static const char* cfg_value(const bb_plugin_config_t* c, const char* key) {
    if (!c) return NULL;
    for (int i = 0; i < c->count; i++) {
        if (!strcmp(c->entries[i].plugin, c->plugin) && !strcmp(c->entries[i].key, key)) return c->entries[i].value;
    }
    return NULL;
}

const char* bb_plugin_config_string(const bb_plugin_config_t* c, const char* key, const char* def) {
    const char* v = cfg_value(c, key);
    return v ? v : def;
}

int64_t bb_plugin_config_int(const bb_plugin_config_t* c, const char* key, int64_t def) {
    const char* v = cfg_value(c, key);
    if (!v || !v[0]) return def;
    char* end;
    const long long n = strtoll(v, &end, 0);    // 0x.. works for string values too
    return *end ? def : (int64_t)n;
}

double bb_plugin_config_float(const bb_plugin_config_t* c, const char* key, double def) {
    const char* v = cfg_value(c, key);
    if (!v || !v[0]) return def;
    char* end;
    const double d = strtod(v, &end);
    return *end ? def : d;
}

bool bb_plugin_config_bool(const bb_plugin_config_t* c, const char* key, bool def) {
    const char* v = cfg_value(c, key);
    if (!v) return def;
    if (!strcmp(v, "true")) return true;
    if (!strcmp(v, "false")) return false;
    return def;
}

// ---- running instances ------------------------------------------------------------

typedef struct {
    const bb_plugin_t* plugin;
    void* self;
} instance_t;

struct pluginset {
    instance_t run[PLUGIN_MAX_RUNNING];
    int count;
};

pluginset_t* pluginset_create(void) {
    return calloc(1, sizeof(pluginset_t));
}

void pluginset_destroy(pluginset_t* s, core_t* core) {
    if (!s) return;
    pluginset_clear(s, core);
    free(s);
}

static void stop_last(pluginset_t* s, core_t* core) {
    instance_t* in = &s->run[--s->count];
    if (in->plugin->stop) in->plugin->stop(in->self, core);
}

void pluginset_clear(pluginset_t* s, core_t* core) {
    while (s->count > 0) stop_last(s, core);    // newest first
}

int pluginset_count(const pluginset_t* s) {
    return s->count;
}

bool pluginset_add_mod(pluginset_t* s, core_t* core, const struct mod* m, char* err, size_t err_size) {
    const int first = s->count;
    for (int i = 0; i < m->plugin_count; i++) {
        const bb_plugin_t* p = plugin_find(m->plugins[i]);
        const char* why = NULL;
        char perr[200] = "";
        void* self = NULL;
        if (!p) why = "isn't built into this game";
        else if (s->count >= PLUGIN_MAX_RUNNING) why = "can't start: too many plugins are running";
        else if (p->start) {
            const bb_plugin_config_t cfg = { m->plugin_cfg, m->plugin_cfg_count, p->name };
            self = p->start(core, &cfg, perr, sizeof(perr));
            if (!self) why = perr[0] ? perr : "failed to start";
        }
        if (why) {
            snprintf(err, err_size, "plugin \"%s\": %s", m->plugins[i], why);
            while (s->count > first) stop_last(s, core);
            return false;
        }
        s->run[s->count++] = (instance_t){ p, self };
    }
    return true;
}

void pluginset_before_frame(pluginset_t* s, core_t* core, uint8_t joy[2]) {
    const bb_plugin_frame_t f = { core, joy };
    for (int i = 0; i < s->count; i++) {
        if (s->run[i].plugin->before_frame) s->run[i].plugin->before_frame(s->run[i].self, &f);
    }
}

void pluginset_after_frame(pluginset_t* s, core_t* core) {
    const bb_plugin_frame_t f = { core, NULL };
    for (int i = 0; i < s->count; i++) {
        if (s->run[i].plugin->after_frame) s->run[i].plugin->after_frame(s->run[i].self, &f);
    }
}
