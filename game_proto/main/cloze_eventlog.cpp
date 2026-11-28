#include "cloze_eventlog.h"
#include <stdio.h>
#include <string.h>
#include <esp_timer.h>
#include <esp_random.h>

namespace cloze_eventlog {
static uint32_t seq = 0;
static uint32_t session = 0;
static int activeUser = 0;
static char PATH[48] = "/sdcard/cloze_events.jsonl";

static void jstr(FILE *f, const char *s) {
    fputc('"', f);
    if (s) for (const unsigned char *p=(const unsigned char*)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p == '\n') fputs("\\n", f);
        else if (*p == '\r') fputs("\\r", f);
        else if (*p == '\t') fputs("\\t", f);
        else fputc(*p, f);
    }
    fputc('"', f);
}

void set_user(int user) {
    if (user < 0) user = 0;
    if (user > 2) user = 2;
    activeUser = user;
    if (user == 0) snprintf(PATH, sizeof(PATH), "/sdcard/cloze_events.jsonl");
    else snprintf(PATH, sizeof(PATH), "/sdcard/cloze_events_u%d.jsonl", user);
    session = 0;
    seq = 0;
}

void begin(const char *clock) {
    if (session) return;
    session = (uint32_t)(esp_timer_get_time() / 1000) ^ esp_random();
    if (!session) session = 1;
    seq = 0;
    FILE *f = fopen(PATH, "a");
    if (!f) return;
    fputs("{\"type\":\"session\",\"session\":", f);
    fprintf(f, "%lu", (unsigned long)session);
    fputs(",\"clock\":", f); jstr(f, clock ? clock : "");
    fprintf(f, ",\"uptime_ms\":%llu}\n", (unsigned long long)(esp_timer_get_time()/1000));
    fclose(f);
}

void log(const char *clock, const char *event, const char *button,
         const char *group, const char *title, int level, int page,
         int tokens, const char *original, const char *masked) {
    if (!session) begin(clock);
    FILE *f = fopen(PATH, "a");
    if (!f) return;
    fputs("{\"type\":\"event\",\"session\":", f);
    fprintf(f, "%lu", (unsigned long)session);
    fprintf(f, ",\"seq\":%lu,\"clock\":", (unsigned long)(++seq));
    jstr(f, clock ? clock : "");
    fprintf(f, ",\"uptime_ms\":%llu,\"event\":", (unsigned long long)(esp_timer_get_time()/1000));
    jstr(f, event ? event : "");
    fputs(",\"button\":", f); jstr(f, button ? button : "");
    fputs(",\"group\":", f); jstr(f, group ? group : "");
    fputs(",\"title\":", f); jstr(f, title ? title : "");
    fprintf(f, ",\"level\":%d,\"page\":%d,\"tokens\":%d,\"original\":", level, page, tokens);
    jstr(f, original ? original : "");
    fputs(",\"masked\":", f); jstr(f, masked ? masked : "");
    fputs("}\n", f);
    fclose(f);
}

const char *path() { return PATH; }
}
