/* scene_menu.c — 主選單與設定畫面 */
#include "app.h"
#include "ttfatlas.h"
#include "audio.h"
#include <math.h>

/* 選單上的目前選擇（保留到下一次啟動） */
static GameMode  s_mode  = MODE_PVE;
static Difficulty s_diff = DIFF_NORMAL;
static int       s_color = CBLACK;   /* 人機對戰時玩家執子 */

extern void RequestScreen(Screen s);   /* main.c */
extern void RequestQuit(void);         /* main.c */

/* 裝飾：緩慢漂浮的棋子 */
typedef struct { Vector2 pos; float r, phase, speed; int color; } Floater;
static Floater s_floaters[10];
static bool s_floatersInit = false;

static void InitFloaters(void)
{
    unsigned seed = 7;
    for (int i = 0; i < 10; i++) {
        seed = seed * 1103515245u + 12345u;
        s_floaters[i] = (Floater){
            .pos   = { 80.0f + (seed >> 8) % 1120, 120.0f + (seed >> 16) % 560 },
            .r     = 14.0f + (seed >> 24) % 22,
            .phase = (seed % 628) / 100.0f,
            .speed = 0.5f + (seed >> 4) % 8 / 10.0f,
            .color = i % 2,
        };
    }
    s_floatersInit = true;
}

static void DrawFloaters(void)
{
    double t = GetTime();
    for (int i = 0; i < 10; i++) {
        Floater *f = &s_floaters[i];
        float y = f->pos.y + sinf((float)t * f->speed + f->phase) * 14.0f;
        Color c = f->color ? g_theme.whiteDisc : g_theme.blackDisc;
        c.a = 26;
        DrawCircleV((Vector2){ f->pos.x, y }, f->r, c);
        c.a = 14;
        DrawCircleV((Vector2){ f->pos.x - f->r * 0.25f, y - f->r * 0.25f }, f->r * 0.7f, c);
    }
    (void)s_floatersInit;
}

/* ============ 主選單 ============ */

void MenuScene_Update(void) {}

void MenuScene_Draw(void)
{
    if (!s_floatersInit) InitFloaters();
    DrawFloaters();

    /* 標題 */
    TextV("黑白棋", (int)VW / 2, 176, 108, g_theme.textMain);
    TextV("R  E  V  E  R  S  I", (int)VW / 2, 252, 26, g_theme.textDim);

    /* 設定面板 */
    Rectangle panel = { VW / 2 - 290, 320, 580, 330 };
    DrawPanelV(panel);
    float cx = VW / 2;

    TextVL("對戰模式", (int)panel.x + 36, 368, 22, g_theme.textDim);
    static const char *modes[] = { "人機對戰", "雙人對戰" };
    SegmentV((Rectangle){ panel.x + 200, 344, 344, 48 }, modes, 2, (int *)&s_mode, 22);

    if (s_mode == MODE_PVE) {
        TextVL("電腦難度", (int)panel.x + 36, 432, 22, g_theme.textDim);
        static const char *diffs[] = { "簡單", "普通", "困難" };
        int d = (int)s_diff;
        SegmentV((Rectangle){ panel.x + 200, 408, 344, 48 }, diffs, 3, &d, 22);
        s_diff = (Difficulty)d;

        TextVL("玩家執子", (int)panel.x + 36, 496, 22, g_theme.textDim);
        static const char *cols[] = { "執黑（先手）", "執白（後手）" };
        SegmentV((Rectangle){ panel.x + 200, 472, 344, 48 }, cols, 2, &s_color, 22);
    } else {
        TextVL("雙人同機輪流落子", (int)panel.x + 36, 452, 22, g_theme.textDim);
        TextVL("黑方先行", (int)panel.x + 36, 496, 22, g_theme.textDim);
    }

    if (ButtonV((Rectangle){ cx - 160, 560, 320, 58 }, "開始遊戲", 26, false)) {
        GameScene_Enter(s_mode, s_diff, s_mode == MODE_PVE ? s_color : CBLACK);
        RequestScreen(SCREEN_GAME);
    }

    /* 底部小按鈕 */
    if (ButtonV((Rectangle){ cx - 190, 660, 180, 50 }, "設定", 22, false))
        RequestScreen(SCREEN_SETTINGS);
    if (ButtonV((Rectangle){ cx + 10, 660, 180, 50 }, "離開遊戲", 22, false))
        RequestQuit();
}

/* ============ 設定畫面 ============ */

void SettingsScene_Update(void) {}

void SettingsScene_Draw(void)
{
    TextV("設定", (int)VW / 2, 150, 48, g_theme.textMain);

    Rectangle panel = { VW / 2 - 330, 220, 660, 420 };
    DrawPanelV(panel);

    Rectangle row1 = { panel.x + 40, panel.y + 36,  panel.width - 80, 56 };
    Rectangle row2 = { panel.x + 40, row1.y + 72,   panel.width - 80, 56 };
    Rectangle row3 = { panel.x + 40, row2.y + 72,   panel.width - 80, 56 };
    Rectangle row4 = { panel.x + 40, row3.y + 72,   panel.width - 80, 56 };
    Rectangle row5 = { panel.x + 40, row4.y + 72,   panel.width - 80, 56 };

    ToggleV(row1, "音效", &g_settings.sound);
    ToggleV(row2, "顯示合法著點", &g_settings.hints);
    ToggleV(row3, "顯示座標", &g_settings.coords);

    TextVL("動畫速度", (int)row4.x + 8, (int)(row4.y + row4.height / 2), 24, g_theme.textMain);
    static const char *spds[] = { "慢", "正常", "快" };
    SegmentV((Rectangle){ row4.x + row4.width - 300, row4.y + 6, 300, 44 }, spds, 3,
             &g_settings.animSpeedIdx, 20);

    TextVL("主題", (int)row5.x + 8, (int)(row5.y + row5.height / 2), 24, g_theme.textMain);
    static const char *thms[] = { "曜石（深色）", "晨光（淺色）" };
    SegmentV((Rectangle){ row5.x + row5.width - 300, row5.y + 6, 300, 44 }, thms, 2,
             &g_settings.themeIdx, 20);

    if (ButtonV((Rectangle){ VW / 2 - 120, panel.y + panel.height + 28, 240, 52 },
                "回到主選單", 22, false))
        RequestScreen(SCREEN_MENU);
}
