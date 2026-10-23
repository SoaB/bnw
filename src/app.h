/* app.h — 全域狀態、主題與共用 UI 元件 */
#ifndef APP_H
#define APP_H

#include "raylib.h"
#include "othello.h"

#define VW 1280.0f   /* 虛擬畫布寬 */
#define VH 800.0f    /* 虛擬畫布高 */

typedef enum { SCREEN_MENU, SCREEN_GAME, SCREEN_SETTINGS } Screen;
typedef enum { MODE_PVE, MODE_PVP } GameMode;

/* ---- 設定（保存於 settings.txt） ---- */
typedef struct {
    bool sound;        /* 音效 */
    bool hints;        /* 顯示合法著點 */
    bool coords;       /* 顯示座標 */
    int  animSpeedIdx; /* 0 慢 / 1 正常 / 2 快 */
    int  themeIdx;     /* 主題索引 */
} Settings;

extern Settings g_settings;

/* ---- 主題 ---- */
typedef struct {
    const char *name;
    Color bg, bgBar, panel, panelEdge, textMain, textDim, accent,
          boardFrame, boardFelt, gridLine, starDot, coordText,
          blackDisc, blackDiscHi, whiteDisc, whiteDiscHi, discRim,
          shadow, lastMoveRing, ghost;
} Theme;

extern Theme g_theme;
void theme_apply(int idx);

/* main.c 提供 */
extern Camera2D g_cam;
extern bool g_inputLocked;   /* 轉場中禁止輸入 */
void RequestScreen(Screen s);
void RequestQuit(void);

/* ---- 場景 ---- */
void MenuScene_Update(void);  void MenuScene_Draw(void);
void SettingsScene_Update(void); void SettingsScene_Draw(void);
void GameScene_Enter(GameMode mode, Difficulty diff, int humanColor); /* humanColor = -1 代表雙方皆 AI（示範模式） */
void GameScene_Update(void);  void GameScene_Draw(void);

/* ---- 元件（widgets.c） ---- */
Vector2 MouseV(void);                        /* 虛擬座標系滑鼠位置 */
float   AnimSpeed(void);                     /* 動畫速度倍率 */
void    DrawPanelV(Rectangle r);
bool    ButtonV(Rectangle r, const char *label, int fontSize, bool disabled);
bool    SegmentV(Rectangle r, const char *const labels[], int count, int *value, int fontSize);
bool    ToggleV(Rectangle row, const char *label, bool *value); /* row = 整列範圍 */
void    TextV(const char *text, int cx, int cy, int size, Color c);              /* 置中 */
void    TextVL(const char *text, int x, int cy, int size, Color c);              /* 靠左垂直置中 */
void    TextVR(const char *text, int x, int cy, int size, Color c);              /* 靠右垂直置中 */
float   TextW(const char *text, int size);

#endif /* APP_H */
