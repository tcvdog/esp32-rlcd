#include "cloze_progress.h"
#include <string.h>
#include <nvs.h>
#include <stdio.h>

namespace cloze_progress {
static ClozeProgressItem p[8][64];
static bool loaded = false;
static int activeUser = 0;

static const char *ns_name() {
    static char ns[10];
    if (activeUser == 0) return "cloze_p";
    snprintf(ns, sizeof(ns), "cloze_p%d", activeUser);
    return ns;
}

static void ensure() {
    if (loaded) return;
    loaded = true;
    nvs_handle_t h;
    if (nvs_open(ns_name(), NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(p);
        if (nvs_get_blob(h, "progress", p, &n) != ESP_OK) memset(p, 0, sizeof(p));
        nvs_close(h);
    } else {
        memset(p, 0, sizeof(p));
    }
}

void load() { ensure(); }

void set_user(int user) {
    if (user < 0) user = 0;
    if (user > 2) user = 2;
    activeUser = user;
    loaded = false;
    memset(p, 0, sizeof(p));
    ensure();
}

void save() {
    ensure();
    nvs_handle_t h;
    if (nvs_open(ns_name(), NVS_READWRITE, &h) == ESP_OK) {
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

void update(int group, int idx, int level, int page, int seen, int reveals, int done) {
    ensure();
    if (group < 0 || group >= 8 || idx < 0 || idx >= 64) return;
    ClozeProgressItem &q = p[group][idx];
    // Levels are monotonic: a stale/older caller must never lower progress.
    if (level > q.level) q.level = (uint8_t)level;
    q.page = (uint8_t)page;
    q.seen = (uint16_t)seen;
    q.reveals = (uint8_t)reveals;
    q.done = (uint8_t)(done ? 1 : q.done);
    save();
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

int completed(int group) {
    ensure();
    if (group < 0 || group >= 8) return 0;
    int n = 0;
    for (int i = 0; i < 64; ++i) if (p[group][i].done) ++n;
    return n;
}

bool is_completed(int group, int idx) {
    const ClozeProgressItem *q = get(group, idx);
    return q && q->done;
}
}
