#include "ui_theme.h"
#include "ui_icons.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace ui {
static u8g2_t *s = nullptr;

void begin(u8g2_t *g) {
    s = g;
    u8g2_SetDrawColor(s, 1);
    u8g2_SetBitmapMode(s, 0);
}

void setInk(Ink ink) { u8g2_SetDrawColor(s, ink == Ink::Black ? 1 : 0); }

void label12(int x, int y, const char *utf8) {
    u8g2_SetFont(s, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(s, x, y, utf8);
}
void label16(int x, int y, const char *utf8) {
    u8g2_SetFont(s, u8g2_font_wqy16_t_gb2312);
    u8g2_DrawUTF8(s, x, y, utf8);
}
void mono(int x, int y, const char *ascii) {
    u8g2_SetFont(s, u8g2_font_7x13_tf);
    u8g2_DrawStr(s, x, y, ascii);
}
int textWidth12(const char *utf8) { u8g2_SetFont(s, u8g2_font_wqy12_t_gb2312); return u8g2_GetUTF8Width(s, utf8); }
int textWidth16(const char *utf8) { u8g2_SetFont(s, u8g2_font_wqy16_t_gb2312); return u8g2_GetUTF8Width(s, utf8); }

void ditherRect(int x, int y, int w, int h, int density) {
    static const uint8_t bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            if (bayer[yy & 3][xx & 3] < density) u8g2_DrawPixel(s, x + xx, y + yy);
}

void hatchRect(int x, int y, int w, int h) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx)
            if (((xx + yy) & 3) == 0) u8g2_DrawPixel(s, x + xx, y + yy);
}

void shadow(int x, int y, int w, int h) {
    setInk(Ink::Black);
    u8g2_DrawRBox(s, x + 2, y + 3, w, h, 4);
    setInk(Ink::Black);
}

void card(int x, int y, int w, int h, bool selected) {
    setInk(Ink::Black);
    shadow(x, y, w, h);
    if (selected) {
        setInk(Ink::Black);
        u8g2_DrawRBox(s, x, y, w, h, 4);
        setInk(Ink::White);
    } else {
        // Shadow is black; clear the card face to white before framing.
        setInk(Ink::White);
        u8g2_DrawRBox(s, x, y, w, h, 4);
        setInk(Ink::Black);
        u8g2_DrawRFrame(s, x, y, w, h, 4);
        u8g2_DrawRFrame(s, x + 1, y + 1, w - 2, h - 2, 3);
    }
}

void panel(int x, int y, int w, int h) {
    setInk(Ink::Black);
    u8g2_DrawRFrame(s, x, y, w, h, 4);
    u8g2_DrawRFrame(s, x + 1, y + 1, w - 2, h - 2, 3);
}

void progress(int x, int y, int w, int value, int max) {
    if (max <= 0) return;
    setInk(Ink::Black);
    u8g2_DrawRFrame(s, x, y, w, 8, 3);
    int fw = (w - 4) * value / max;
    if (fw > 0) u8g2_DrawRBox(s, x + 2, y + 2, fw, 4, 2);
}

void topBar(const char *path, const Status &st) {
    setInk(Ink::Black);
    u8g2_DrawBox(s, 0, 0, W, TOP_H);
    setInk(Ink::White);
    u8g2_SetFont(s, u8g2_font_7x13_tf);
    u8g2_DrawStr(s, 8, 16, path);
    // Right-side status: WiFi / SD / battery text + icon.
    u8g2_SetBitmapMode(s, 1);
    u8g2_DrawXBMP(s, 292, 7, 12, 12, UI_ICON_WIFI);
    u8g2_DrawXBMP(s, 312, 7, 9, 9, UI_ICON_SD);
    u8g2_SetBitmapMode(s, 0);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", st.batteryPct);
    int bw = u8g2_GetStrWidth(s, b);
    u8g2_DrawStr(s, 330 - bw, 17, b);
    u8g2_DrawFrame(s, 336, 8, 28, 12);
    int fw = 24 * st.batteryPct / 100;
    if (fw > 0) u8g2_DrawBox(s, 338, 10, fw, 8);
    u8g2_DrawBox(s, 365, 11, 3, 6);
    if (st.charging) u8g2_DrawLine(s, 350, 9, 344, 14);
    setInk(Ink::White);
}

void keycap(int x, int y, const char *key, const char *action) {
    setInk(Ink::Black);
    u8g2_DrawRFrame(s, x, y, 24, 14, 3);
    u8g2_SetFont(s, u8g2_font_7x13_tf);
    int kw = u8g2_GetStrWidth(s, key);
    u8g2_DrawStr(s, x + (24 - kw) / 2, y + 11, key);
    u8g2_DrawStr(s, x + 28, y + 11, action);
}

void bottomBar() {
    setInk(Ink::Black);
    for (int x = 8; x < 392; x += 8) u8g2_DrawPixel(s, x, BOTTOM_Y);
    u8g2_SetFont(s, u8g2_font_7x13_tf);
}

float easeShort(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    return t * t * (3.0f - 2.0f * t);
}
}
