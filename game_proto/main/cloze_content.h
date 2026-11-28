#pragma once
#include "poem_cards.h"

struct ClozeGroup {
    const char *name;
    const char *sub;
    const PoemCard *items;
    int count;
};

static const ClozeGroup CLOZE_GROUPS[] = {
    {"小学古诗", "08 首", &POEM_CARDS[0], 8},
    {"小学文言文", "02 篇", PRIMARY_CLASSICAL, 2},
    {"初中古诗", "06 首", &POEM_CARDS[8], 6},
    {"初中文言文", "06 篇", CLASSICAL_CARDS, 6},
    {"高中古诗", "06 首", HIGH_POEMS, 6},
    {"高中文言文", "06 篇", HIGH_CLASSICAL, 6},
};
static const int CLOZE_GROUP_COUNT = 6;
