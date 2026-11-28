#pragma once
#include <stdint.h>
namespace cloze_eventlog {
void set_user(int user);
void clear_user(int user);
void begin(const char *clock);
void log(const char *clock, const char *event, const char *button,
         const char *group, const char *title, int level, int page,
         int tokens, const char *original, const char *masked);
const char *path();
void score(const char *clock, const char *group, const char *title,
           int level, int delta, int total, int reveals);
}
