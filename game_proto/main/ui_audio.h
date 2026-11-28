#pragma once
#include <stdint.h>
#include "u8g2.h"

namespace ui_audio {
struct View {
    const char *title = "";
    const char *format = "---";
    const char *state = "STOP";
    uint32_t posMs = 0;
    uint32_t durMs = 0;
    int track = 0;
    int total = 0;
    int volume = 0;
    int level = 0;       // 0..32768
    int repeatMode = 0;  // 0 off, 1 all, 2 one
    bool running = false;
    bool empty = true;
};

void draw(u8g2_t *g, const View &v);
}
