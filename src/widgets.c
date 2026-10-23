/* widgets.c — 共用 UI 元件與主題 */
#include "app.h"
#include "ttfatlas.h"
#include "audio.h"

Theme g_theme;
Settings g_settings = { true, true, true, 1, 0 };

/* ---- 主題定義 ---- */
static const Theme THEMES[] = {
    {   /* 曜石（深色） */
        "曜石",
        .bg         = { 16, 18, 24, 255 },
        .bgBar      = { 8, 9, 12, 255 },
        .panel      = { 30, 33, 43, 255 },
        .panelEdge  = { 52, 57, 72, 255 },
        .textMain   = { 232, 234, 240, 255 },
        .textDim    = { 140, 146, 162, 255 },
        .accent     = { 108, 196, 145, 255 },
        .boardFrame = { 62, 47, 34, 255 },
        .boardFelt  = { 24, 98, 58, 255 },
        .gridLine   = { 14, 66, 40, 255 },
        .starDot    = { 12, 52, 32, 255 },
        .coordText  = { 214, 202, 178, 160 },
        .blackDisc  = { 22, 22, 26, 255 },
        .blackDiscHi= { 82, 84, 96, 255 },
        .whiteDisc  = { 244, 243, 238, 255 },
        .whiteDiscHi= { 178, 178, 188, 255 },
        .discRim    = { 10, 10, 14, 255 },
        .shadow     = { 0, 0, 0, 70 },
        .lastMoveRing = { 255, 208, 84, 255 },
        .ghost      = { 255, 255, 255, 46 },
    },
    {   /* 晨光（淺色） */
        "晨光",
        .bg         = { 238, 231, 218, 255 },
        .bgBar      = { 222, 213, 197, 255 },
        .panel      = { 250, 246, 238, 255 },
        .panelEdge  = { 210, 198, 178, 255 },
        .textMain   = { 62, 58, 52, 255 },
        .textDim    = { 142, 134, 120, 255 },
        .accent     = { 58, 142, 96, 255 },
        .boardFrame = { 128, 96, 62, 255 },
        .boardFelt  = { 62, 148, 92, 255 },
        .gridLine   = { 40, 110, 66, 255 },
        .starDot    = { 30, 88, 52, 255 },
        .coordText  = { 70, 58, 42, 170 },
        .blackDisc  = { 30, 30, 34, 255 },
        .blackDiscHi= { 96, 96, 108, 255 },
        .whiteDisc  = { 250, 249, 244, 255 },
        .whiteDiscHi= { 190, 190, 198, 255 },
        .discRim    = { 60, 56, 50, 255 },
        .shadow     = { 80, 60, 40, 60 },
        .lastMoveRing = { 232, 160, 44, 255 },
        .ghost      = { 255, 255, 255, 60 },
    },
};

void theme_apply(int idx)
{
    if (idx < 0 || idx >= (int)(sizeof(THEMES) / sizeof(THEMES[0]))) idx = 0;
    g_theme = THEMES[idx];
}

/* ---- 基礎 ---- */

Vector2 MouseV(void)
{
    extern Camera2D g_cam;   /* main.c 定義 */
    return GetScreenToWorld2D(GetMousePosition(), g_cam);
}

float AnimSpeed(void)
{
    static const float spd[3] = { 0.65f, 1.0f, 1.6f };
    return spd[g_settings.animSpeedIdx >= 0 && g_settings.animSpeedIdx <= 2 ? g_settings.animSpeedIdx : 1];
}

float TextW(const char *text, int size)
{
    return MeasureTTFTextEx(text, size);
}

void TextV(const char *text, int cx, int cy, int size, Color c)
{
    float w = MeasureTTFTextEx(text, size);
    DrawTTFTextEx(text, (int)(cx - w / 2), (int)(cy - size * 0.52f), size, c);
}

void TextVL(const char *text, int x, int cy, int size, Color c)
{
    DrawTTFTextEx(text, x, (int)(cy - size * 0.52f), size, c);
}

void TextVR(const char *text, int x, int cy, int size, Color c)
{
    float w = MeasureTTFTextEx(text, size);
    DrawTTFTextEx(text, (int)(x - w), (int)(cy - size * 0.52f), size, c);
}

void DrawPanelV(Rectangle r)
{
    DrawRectangleRounded(r, 0.06f, 8, g_theme.panel);
    DrawRectangleRoundedLinesEx(r, 0.06f, 8, 1.5f, g_theme.panelEdge);
}

/* ---- 元件 ---- */

static bool hoverRing(Rectangle r)
{
    if (g_inputLocked) return false;
    Vector2 m = MouseV();
    return CheckCollisionPointRec(m, r);
}

bool ButtonV(Rectangle r, const char *label, int fontSize, bool disabled)
{
    bool hovered = !disabled && hoverRing(r);
    bool clicked = hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    Color base = g_theme.panel;
    Color edge = g_theme.panelEdge;
    Color txt  = g_theme.textMain;
    if (hovered) {
        base = g_theme.accent;
        base.a = 36;
        edge = g_theme.accent;
        txt = g_theme.textMain;
    }
    if (disabled) {
        txt = g_theme.textDim;
        txt.a = 110;
    }

    DrawRectangleRounded(r, 0.22f, 8, base);
    DrawRectangleRoundedLinesEx(r, 0.22f, 8, hovered ? 2.0f : 1.5f, edge);
    TextV(label, (int)(r.x + r.width / 2), (int)(r.y + r.height / 2), fontSize, txt);

    if (clicked) audio_play(SND_CLICK);
    return clicked;
}

bool SegmentV(Rectangle r, const char *const labels[], int count, int *value, int fontSize)
{
    float w = r.width / count;
    bool changed = false;
    for (int i = 0; i < count; i++) {
        Rectangle cell = { r.x + w * i, r.y, w, r.height };
        bool on = (*value == i);
        bool hovered = hoverRing(cell);
        if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            *value = i;
            changed = true;
            audio_play(SND_CLICK);
        }
        Color bg = on ? g_theme.accent : g_theme.panel;
        bg.a = on ? 60 : 255;
        DrawRectangleRounded(cell, 0.28f, 6, bg);
        if (i == 0)
            DrawRectangleRoundedLinesEx(r, 0.28f, 6, 1.5f, g_theme.panelEdge);
        TextV(labels[i], (int)(cell.x + w / 2), (int)(cell.y + cell.height / 2),
              fontSize, on ? g_theme.accent : g_theme.textDim);
    }
    return changed;
}

bool ToggleV(Rectangle row, const char *label, bool *value)
{
    Rectangle pill = { row.x + row.width - 78, row.y + row.height / 2 - 15, 62, 30 };
    bool hovered = hoverRing(pill) || hoverRing((Rectangle){ row.x, row.y, row.width - 90, row.height });
    if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        *value = !*value;
        audio_play(SND_CLICK);
    }

    TextVL(label, (int)row.x + 8, (int)(row.y + row.height / 2), 24, g_theme.textMain);

    /* 開關本體 */
    Color on = g_theme.accent, off = g_theme.panelEdge;
    DrawRectangleRounded(pill, 0.5f, 8, *value ? on : off);
    float knobX = *value ? pill.x + pill.width - 16 : pill.x + 16;
    DrawCircleV((Vector2){ knobX, pill.y + pill.height / 2 }, 11,
                *value ? (Color){ 16, 20, 18, 255 } : g_theme.textDim);
    /* 標籤畫在旋鈕對側 */
    TextVL(*value ? "開" : "關", (int)(*value ? pill.x + 10 : pill.x + pill.width - 26),
           (int)(pill.y + pill.height / 2), 16,
           *value ? g_theme.textMain : g_theme.bg);
    return hovered;
}
