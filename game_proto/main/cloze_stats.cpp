#include "cloze_stats.h"
#include <stdio.h>
#include <string.h>
#include <nvs.h>

namespace cloze_stats {
static ClozeStatsData s;
static bool s_loaded = false;
static int activeUser = 0;
static char CSV[48] = "/sdcard/cloze_stats.csv";

static const char *stats_ns_for(int user, char *ns, size_t n) {
    if (user == 0) return "cloze";
    snprintf(ns, n, "cloze%d", user);
    return ns;
}

static const char *stats_ns() {
    static char ns[10];
    return stats_ns_for(activeUser, ns, sizeof(ns));
}

static void ensure_loaded() {
    if (s_loaded) return;
    s_loaded = true;
    nvs_handle_t h;
    if (nvs_open(stats_ns(), NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s);
        if (nvs_get_blob(h, "stats", &s, &len) != ESP_OK) memset(&s, 0, sizeof(s));
        nvs_close(h);
    } else {
        memset(&s, 0, sizeof(s));
    }
}

void load() { ensure_loaded(); }

void clear_user(int user) {
    if (user < 1) return;
    char ns[12];
    stats_ns_for(user, ns, sizeof(ns));
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    char csv[48];
    snprintf(csv, sizeof(csv), "/sdcard/cloze_stats_u%d.csv", user);
    remove(csv);
    if (activeUser == user) {
        memset(&s, 0, sizeof(s));
        s_loaded = true;
    }
}

void set_user(int user) {
    if (user < 0) user = 0;
    if (user > 2) user = 2;
    activeUser = user;
    s_loaded = false;
    memset(&s, 0, sizeof(s));
    if (user == 0) snprintf(CSV, sizeof(CSV), "/sdcard/cloze_stats.csv");
    else snprintf(CSV, sizeof(CSV), "/sdcard/cloze_stats_u%d.csv", user);
    ensure_loaded();
}

void save() {
    ensure_loaded();
    nvs_handle_t h;
    if (nvs_open(stats_ns(), NVS_READWRITE, &h) == ESP_OK) {
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
