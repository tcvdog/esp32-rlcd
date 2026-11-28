#pragma once
#include <stdint.h>

struct ClozeStatsData {
    uint32_t enters;
    uint32_t exits;
    uint32_t levelEnters[6];
    uint32_t pageVisits[6][12];
    uint32_t answerToggles;
};

namespace cloze_stats {
void load();
void set_user(int user);
void save();
void enter(int level);
void exitSession();
void enterLevel(int level);
void visitPage(int level, int page);
void toggleAnswer();
const ClozeStatsData &data();
void appendEvent(const char *time, const char *event, const char *group,
                 const char *title, int level, int page);
const char *csvPath();
}
