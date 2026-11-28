#include "cloze_progress.h"
#include <string.h>
#include <nvs.h>

namespace cloze_progress {
static ClozeProgressItem p[8][64];
static bool loaded = false;

static void ensure() {
    if (loaded) return;
    loaded = true;
    nvs_handle_t h;
    if (nvs_open("cloze_p", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(p);
        if (nvs_get_blob(h, "progress", p, &n) != ESP_OK) memset(p, 0, sizeof(p));
        nvs_close(h);
    } else {
        memset(p, 0, sizeof(p));
    }
}

void load() { ensure(); }

void save() {
    ensure();
    nvs_handle_t h;
    if (nvs_open("cloze_p", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "progress", p, sizeof(p));
        nvs_commit(h);
        nvs_close(h);
    }
}

void open(int group, int idx) {
    ensure();
    if (group < 0 || group >= 8 || idx < 0 || idx >= 64) return;
    if (p[group][idx].opens < 65535) ++p[group][idx].opens;
    save();
}

void state(int group, int idx, int level, int page) {
    ensure();
    if (group < 0 || group >= 8 || idx < 0 || idx >= 64) return;
    if (level > p[group][idx].level) p[group][idx].level = (uint8_t)level;
    p[group][idx].page = (uint8_t)page;
}

const ClozeProgressItem *get(int group, int idx) {
    ensure();
    if (group < 0 || group >= 8 || idx < 0 || idx >= 64) return nullptr;
    return &p[group][idx];
}

int opens(int group, int idx) {
    const ClozeProgressItem *q = get(group, idx);
    return q ? q->opens : 0;
}
}
