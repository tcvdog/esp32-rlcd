#include "cloze_stats.h"
#include <stdio.h>
#include <string.h>
#include <nvs.h>

namespace cloze_stats {
static ClozeStatsData s;
static bool s_loaded = false;
static const char *CSV = "/sdcard/cloze_stats.csv";

static void ensure_loaded() {
    if (s_loaded) return;
    s_loaded = true;
    nvs_handle_t h;
    if (nvs_open("cloze", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s);
        if (nvs_get_blob(h, "stats", &s, &len) != ESP_OK) memset(&s, 0, sizeof(s));
        nvs_close(h);
    } else {
        memset(&s, 0, sizeof(s));
    }
}

void load() { ensure_loaded(); }

void save() {
    ensure_loaded();
    nvs_handle_t h;
    if (nvs_open("cloze", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "stats", &s, sizeof(s));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void append(const char *time, const char *event, const char *group,
                   const char *title, int level, int page) {
    FILE *f = fopen(CSV, "a");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
        fputs("time,event,group,title,level,page\n", f);
    fprintf(f, "%s,%s,%s,%s,%d,%d\n", time, event,
            group ? group : "-", title ? title : "-", level, page);
    fclose(f);
}

void enter(int level) {
    ensure_loaded();
    ++s.enters;
    if (level >= 1 && level <= 6) ++s.levelEnters[level - 1];
    save();
}

void exitSession() {
    ensure_loaded();
    ++s.exits;
    save();
}

void enterLevel(int level) {
    ensure_loaded();
    if (level >= 1 && level <= 6) ++s.levelEnters[level - 1];
    save();
}

void visitPage(int level, int page) {
    ensure_loaded();
    if (level < 1 || level > 6 || page < 0 || page >= 12) return;
    ++s.pageVisits[level - 1][page];
}

void toggleAnswer() {
    ensure_loaded();
    ++s.answerToggles;
}

const ClozeStatsData &data() { ensure_loaded(); return s; }
const char *csvPath() { return CSV; }

void appendEvent(const char *time, const char *event, const char *group,
                 const char *title, int level, int page) {
    append(time, event, group, title, level, page);
}
}
