#pragma once
#include <stdint.h>

struct ClozeProgressItem {
    uint16_t opens;
    uint8_t level;
    uint8_t page;
    uint8_t done;
    uint8_t reveals;
    uint16_t seen;
    uint16_t score;
};

namespace cloze_progress {
void load();
void set_user(int user);
void clear_user(int user);
void save();
void open(int group, int idx);
void state(int group, int idx, int level, int page);
void update(int group, int idx, int level, int page, int seen, int reveals, int done);
const ClozeProgressItem *get(int group, int idx);
int opens(int group, int idx);
int completed(int group);
bool is_completed(int group, int idx);
void add_score(int group, int idx, int delta);
int total_score();
}
