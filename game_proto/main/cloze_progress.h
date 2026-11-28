#pragma once
#include <stdint.h>

struct ClozeProgressItem {
    uint16_t opens;
    uint8_t level;
    uint8_t page;
};

namespace cloze_progress {
void load();
void save();
void open(int group, int idx);
void state(int group, int idx, int level, int page);
const ClozeProgressItem *get(int group, int idx);
int opens(int group, int idx);
int completed(int group);
bool is_completed(int group, int idx);
}
