#pragma once
#include "poem_cards.h"

struct ClozeGroup {
    const char *name;
    const char *sub;
    const PoemCard *items;
    int count;
};

// Primary: first 8 of POEM_CARDS. Middle: last 6 (including extras).
static const ClozeGroup CLOZE_GROUPS[] = {
    {"小学古诗词", "08 首", &POEM_CARDS[0], 8},
    {"初中古诗词", "06 首", &POEM_CARDS[8], 6},
    {"文言文", "06 篇", CLASSICAL_CARDS, 6},
};
static const int CLOZE_GROUP_COUNT = 3;
