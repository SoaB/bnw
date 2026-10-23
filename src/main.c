/* main.c — 程式進入點：視窗、虛擬畫布縮放、場景切換與轉場
 *
 * 命令列（測試用）：
 *   blackandwhite.exe --demo            雙電腦對戰示範
 *   blackandwhite.exe --shot out.png    於第 45 幅擷取畫面後結束
 */
#define DRAW_TTF_TEXT_IMPLEMENTATION
#include "ttfatlas.h"

#include "app.h"
#include "audio.h"
#include "ai.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- main.c 持有的全域 ---- */
Camera2D g_cam;
bool g_inputLocked = false;
static bool s_quit = false;

static Screen s_cur = SCREEN_MENU;
static Screen s_next = SCREEN_MENU;
static bool   s_transition = false;
static float  s_fade = 0.0f;

void RequestScreen(Screen s)
{
    if (!s_transition && s != s_cur) {
        s_next = s;
        s_transition = true;
    }
}

void RequestQuit(void)
{
    s_quit = true;
}

/* ---- 設定存取 ---- */
static void SaveSettings(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "sound=%d\nhints=%d\ncoords=%d\nanimSpeed=%d\ntheme=%d\n",
             g_settings.sound ? 1 : 0,
             g_settings.hints ? 1 : 0,
             g_settings.coords ? 1 : 0,
             g_settings.animSpeedIdx,
             g_settings.themeIdx);
    SaveFileText("settings.txt", buf);
}

static void LoadSettings(void)
{
    char *text = LoadFileText("settings.txt");
    if (!text) return;
    int v[5];
    if (sscanf(text, "sound=%d\nhints=%d\ncoords=%d\nanimSpeed=%d\ntheme=%d",
               &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
        g_settings.sound = v[0] != 0;
        g_settings.hints = v[1] != 0;
        g_settings.coords = v[2] != 0;
        g_settings.animSpeedIdx = (v[3] >= 0 && v[3] <= 2) ? v[3] : 1;
        g_settings.themeIdx = (v[4] >= 0 && v[4] <= 1) ? v[4] : 0;
    }
    UnloadFileText(text);
}

int main(int argc, char **argv)
{
    bool demo = false, easy = false;
    const char *shotPath = NULL;
    int shotFrame = 45;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) demo = true;
        else if (!strcmp(argv[i], "--easy")) easy = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
                shotFrame = atoi(argv[++i]);
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow((int)VW, (int)VH, "黑白棋 REVERSI");
    SetWindowMinSize(640, 400);
    SetExitKey(KEY_NULL);          /* ESC 留給遊戲內暫停 */
    SetTargetFPS(60);

    /* 視窗圖示：黑底白子 */
    Image icon = GenImageColor(32, 32, BLANK);
    ImageDrawCircle(&icon, 16, 16, 14, (Color){ 24, 24, 28, 255 });
    ImageDrawCircle(&icon, 16, 16, 7, (Color){ 244, 243, 238, 255 });
    SetWindowIcon(icon);
    UnloadImage(icon);

    /* 中文字型：優先使用 assets 下的字型，否則退回系統微軟正黑體 */
    static const char *fontCandidates[] = {
        "assets/font.ttf", "assets/msjh.ttc",
        "C:/Windows/Fonts/msjh.ttc", "C:/Windows/Fonts/msjhbd.ttc",
    };
    for (size_t i = 0; i < sizeof(fontCandidates) / sizeof(fontCandidates[0]); i++) {
        InitTTFAtlas(fontCandidates[i]);
        if (TTFAtlasReady()) break;
        TraceLog(LOG_WARNING, "Font not available: %s", fontCandidates[i]);
    }
    if (!TTFAtlasReady())
        TraceLog(LOG_ERROR, "No usable CJK font found; text will be invisible");

    audio_init();
    LoadSettings();
    theme_apply(g_settings.themeIdx);

    if (demo) {
        GameScene_Enter(MODE_PVE, easy ? DIFF_EASY : DIFF_NORMAL, -1);
        s_cur = SCREEN_GAME;
    }

    int frame = 0;
    while (!WindowShouldClose() && !s_quit) {
        /* 轉場：淡出 → 切換場景 → 淡入 */
        float dt = GetFrameTime();
        if (s_transition) {
            if (s_cur != s_next) {
                s_fade += dt / 0.14f;
                if (s_fade >= 1.0f) { s_fade = 1.0f; s_cur = s_next; }
            } else {
                s_fade -= dt / 0.18f;
                if (s_fade <= 0.0f) { s_fade = 0.0f; s_transition = false; }
            }
        }
        g_inputLocked = s_transition;

        /* 虛擬畫布：等比縮放置中（letterbox） */
        float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
        float scale = fminf(w / VW, h / VH);
        g_cam.zoom = scale;
        g_cam.target = (Vector2){ 0, 0 };
        g_cam.offset = (Vector2){ (w - VW * scale) * 0.5f, (h - VH * scale) * 0.5f };
        g_cam.rotation = 0.0f;

        audio_set_enabled(g_settings.sound);
        theme_apply(g_settings.themeIdx);

        BeginDrawing();
        ClearBackground(g_theme.bgBar);
        BeginMode2D(g_cam);
        DrawRectangle(0, 0, (int)VW, (int)VH, g_theme.bg);
        switch (s_cur) {
        case SCREEN_MENU:     MenuScene_Update();     MenuScene_Draw();     break;
        case SCREEN_GAME:     GameScene_Update();     GameScene_Draw();     break;
        case SCREEN_SETTINGS: SettingsScene_Update(); SettingsScene_Draw(); break;
        }
        EndMode2D();

        if (s_fade > 0.0f)
            DrawRectangle(0, 0, (int)VW, (int)VH,
                          (Color){ 0, 0, 0, (unsigned char)(255 * s_fade) });
        EndDrawing();

        if (shotPath && ++frame == shotFrame) {
            TakeScreenshot(shotPath);
            break;
        }
    }

    SaveSettings();
    ai_shutdown();
    if (IsAudioDeviceReady()) CloseAudioDevice();
    UnloadTTFAtlas();
    CloseWindow();
    return 0;
}
