#include "ui_audio.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace ui_audio {
static uint8_t s_wave[16][8]; // [column][history row], 0..8

static void timecode(uint32_t ms, char *b, size_t n) {
    snprintf(b, n, "%u:%02u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60));
}

static void panelWithShadow(int x, int y, int w, int h) {
    ui::shadow(x, y, w, h);
    ui::setInk(ui::Ink::White);
    u8g2_DrawRBox(ui::g(), x, y, w, h, 4);
    ui::setInk(ui::Ink::Black);
    u8g2_DrawRFrame(ui::g(), x, y, w, h, 4);
    u8g2_DrawRFrame(ui::g(), x + 1, y + 1, w - 2, h - 2, 3);
}

static void updateWaterfall(const View &v) {
    int level = v.running ? v.level : 0;
    for (int c = 0; c < 16; ++c) {
        float f = 0.35f + 0.65f * fabsf(sinf(c * 0.61f + v.posMs / 620.0f));
        f *= 0.55f + 0.45f * fabsf(sinf(c * 0.27f + v.posMs / 1450.0f));
        float norm = level > 0 ? sqrtf((float)level / 32768.0f > 1.0f ? 1.0f : (float)level / 32768.0f) : 0.0f;
        float energy = 0.55f + 0.45f * norm;
        int target = v.running ? (1 + (int)(energy * 7.0f * f + 0.5f)) : 0;
        if (target > 8) target = 8;
        for (int r = 7; r > 0; --r) s_wave[c][r] = s_wave[c][r - 1];
        s_wave[c][0] = (uint8_t)target;
    }
}

static void drawWaterfall(int x, int y, int w, int h) {
    const int cols = 16, rows = 8;
    int gap = 2;
    int cw = (w - (cols - 1) * gap) / cols;
    int rh = (h - (rows - 1) * 1) / rows;
    for (int c = 0; c < cols; ++c) {
        int cx = x + c * (cw + gap);
        int lv = s_wave[c][0];
        for (int r = 0; r < rows; ++r) {
            int cy = y + r * (rh + 1);
            bool on = (r >= rows - lv);
            if (on) u8g2_DrawBox(ui::g(), cx, cy, cw, rh);
            else u8g2_DrawFrame(ui::g(), cx, cy, cw, rh);
        }
        // Light trailing dot above the column.
        if (lv > 0 && lv < rows)
            u8g2_DrawPixel(ui::g(), cx + cw / 2, y + (rows - lv) * (rh + 1) - 1);
    }
}

void draw(u8g2_t *g, const View &v) {
    ui::begin(g);
    ui::setInk(ui::Ink::Black);
    updateWaterfall(v);

    // Left: one large record / turntable, tonearm, platter shadow.
    const int wx = 8, wy = 32, ww = 132, wh = 188;
    panelWithShadow(wx, wy, ww, wh);

    const int cx = wx + 66, cy = wy + 86, R = 50;
    // Single large record: black disc, white grooves, center label.
    u8g2_DrawDisc(g, cx, cy, R, U8G2_DRAW_ALL);
    u8g2_DrawCircle(g, cx, cy, R - 2, U8G2_DRAW_ALL);
    ui::setInk(ui::Ink::White);
    for (int r = 18; r <= 44; r += 7) u8g2_DrawCircle(g, cx, cy, r, U8G2_DRAW_ALL);
    u8g2_DrawDisc(g, cx, cy, 11, U8G2_DRAW_ALL);
    // Rotating label mark.
    float a = v.running ? fmodf(v.posMs / 900.0f, 6.28318f) : 0.0f;
    int mx = cx + (int)(cosf(a) * 7), my = cy + (int)(sinf(a) * 7);
    u8g2_DrawLine(g, cx, cy, mx, my);
    ui::setInk(ui::Ink::Black);
    u8g2_DrawDisc(g, cx, cy, 3, U8G2_DRAW_ALL);

    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, wx + 8, wy + 20, "SIDE A");
    u8g2_DrawStr(g, wx + 8, wy + 154, "16k OUT");
    u8g2_DrawStr(g, wx + 8, wy + 180, v.format ? v.format : "---");
    if (v.running) u8g2_DrawDisc(g, wx + 116, wy + 20, 3, U8G2_DRAW_ALL);
    else u8g2_DrawCircle(g, wx + 116, wy + 20, 3, U8G2_DRAW_ALL);

    // Right: track, title, time, waterfall.
    const int rx = 148, ry = 32, rw = 244, rh = 188;
    panelWithShadow(rx, ry, rw, rh);
    ui::mono(rx + 10, ry + 18, "TRACK");
    char b[40];
    snprintf(b, sizeof(b), "%02d/%02d", v.track, v.total);
    ui::mono(rx + 58, ry + 18, b);
    ui::mono(rx + 126, ry + 18, v.format ? v.format : "---");
    ui::mono(rx + 180, ry + 18, v.running ? "PLAY" : "STOP");
    u8g2_DrawHLine(g, rx + 10, ry + 26, rw - 20);

    u8g2_SetFont(g, u8g2_font_wqy12_t_gb2312);
    char title[44];
    snprintf(title, sizeof(title), "%.34s", v.title ? v.title : "NO TRACK");
    u8g2_DrawUTF8(g, rx + 10, ry + 48, title);
    u8g2_SetFont(g, u8g2_font_7x13_tf);
    u8g2_DrawStr(g, rx + 10, ry + 68, v.state ? v.state : "STOP");
    char t1[16], t2[16];
    timecode(v.posMs, t1, sizeof(t1));
    timecode(v.durMs, t2, sizeof(t2));
    u8g2_DrawStr(g, rx + 10, ry + 90, t1);
    u8g2_DrawStr(g, rx + 56, ry + 90, "/");
    u8g2_DrawStr(g, rx + 68, ry + 90, t2);

    ui::mono(rx + 10, ry + 112, "LEVEL / WATERFALL");
    drawWaterfall(rx + 10, ry + 120, rw - 20, 64);

    // Segmented progress + loop / queue strip.
    const int py = 228;
    ui::segmentedBar(8, py, 384, 10, v.durMs ? (int)v.posMs : 0, v.durMs ? (int)v.durMs : 1, 40);
    ui::mono(8, py + 24, "A");
    ui::mono(376, py + 24, "B");
    u8g2_DrawHLine(g, 20, py + 20, 344);
    const char *mode = v.repeatMode == 2 ? "LOOP: ONE" : (v.repeatMode == 1 ? "LOOP: ALL" : "LOOP: OFF");
    ui::mono(8, py + 40, mode);
    snprintf(b, sizeof(b), "QUEUE %02d/%02d", v.track, v.total);
    ui::mono(264, py + 40, b);
}

} // namespace ui_audio
