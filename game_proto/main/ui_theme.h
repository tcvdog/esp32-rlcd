#pragma once
#include <stdint.h>
#include "u8g2.h"

namespace ui {
constexpr int W = 400;
constexpr int H = 300;
constexpr int TOP_H = 24;
constexpr int CONTENT_Y = 24;
constexpr int CONTENT_H = 252;
constexpr int BOTTOM_Y = 276;
constexpr int BOTTOM_H = 24;

struct Status {
    bool wifi = false;
    bool sd = false;
    int batteryPct = 0;
    bool charging = false;
    const char *time = "--:--";
};

enum class Ink { Black, White };

void begin(u8g2_t *g, bool inverted = false);
void setInk(Ink ink);

void topBar(const char *path, const Status &st);
void bottomBar();
void keycap(int x, int y, const char *key, const char *action);

void card(int x, int y, int w, int h, bool selected);
void panel(int x, int y, int w, int h);
void ditherRect(int x, int y, int w, int h, int density);
void hatchRect(int x, int y, int w, int h);
void shadow(int x, int y, int w, int h);
void progress(int x, int y, int w, int value, int max);

void label12(int x, int y, const char *utf8);
void label16(int x, int y, const char *utf8);
void mono(int x, int y, const char *ascii);
int textWidth12(const char *utf8);
int textWidth16(const char *utf8);
float easeShort(float t);
}
