#pragma once
#include "poem_cards.h"
#include "cloze_text.h"
struct ClozeGroup { const char *name; const char *sub; const PoemCard *items; int count; };
static const ClozeGroup CLOZE_GROUPS[] = {
    {"小学低年级", "14 条", CLOZE_LOW_CARDS, 14},
    {"小学中高年级", "62 条", CLOZE_MIDHIGH_CARDS, 62},
    {"初中", "63 条", CLOZE_MIDDLE_CARDS, 63},
    {"高中", "59 条", CLOZE_HIGH_CARDS, 59},
};
static const int CLOZE_GROUP_COUNT = 4;
