/* scene_game.c — 對局場景：棋盤渲染、動畫、回合流程與 AI 銜接 */
#include "app.h"
#include "ttfatlas.h"
#include "audio.h"
#include "ai.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

extern void RequestScreen(Screen s);   /* main.c */
extern void RequestQuit(void);

/* ---- 版面常數 ---- */
#define CELL     78.0f
#define BOARD_X  56.0f
#define BOARD_Y  120.0f
#define SIDE_X   712.0f
#define SIDE_W   512.0f

/* ---- 回合狀態 ---- */
typedef enum { PH_ANIM, PH_HUMAN, PH_AI, PH_PASS, PH_OVER } Phase;

typedef struct {
    Board pre;    /* 落子前盤面（復原用） */
    int   sq;
    int   mover;
    int   flips;
} Hist;

typedef struct {
    bool   active;
    double start;     /* 開始時間（含漣漪延遲） */
    float  dur;
    int    from, to;
    float  lastT;     /* 供翻子音效偵測中點 */
} FlipAnim;

static GameMode  s_mode;
static Difficulty s_diff;
static int       s_human;          /* PVE 玩家執子；-1 = 雙方皆 AI（示範） */
static Board     s_board;
static Hist      s_hist[72];
static int       s_histLen;
static Phase     s_phase;
static bool      s_paused;

static FlipAnim  s_flip[64];
static double    s_pop[64];        /* 落子彈跳動畫起始時間，0 = 無 */
static double    s_introT0;
static double    s_passT0;
static double    s_overT0;
static bool      s_overSoundDone;

static char      s_banner[64];
static double    s_bannerT0, s_bannerDur;

static int       s_hintSq;
static double    s_hintT0;
static bool      s_hintReq;
static bool      s_aiPending;

static float     s_scoreShown[2];
static float     s_histScroll;     /* 距最新一筆的列數 */
static int       s_hoverSq;

static double now(void) { return GetTime(); }

/* ---- 小工具 ---- */
static float easeOutBack(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

static Vector2 SqCenter(int sq)
{
    return (Vector2){ BOARD_X + (sq % 8) * CELL + CELL / 2,
                      BOARD_Y + (sq / 8) * CELL + CELL / 2 };
}

static bool IsAITurn(void)
{
    return s_mode == MODE_PVE && (s_human < 0 || s_board.turn != s_human);
}

static const char *DiffName(void)
{
    return s_diff == DIFF_EASY ? "簡單" : s_diff == DIFF_NORMAL ? "普通" : "困難";
}

static const char *ColorName(int c)
{
    return c == CBLACK ? "黑方" : "白方";
}

static void ShowBanner(const char *text, double dur)
{
    strncpy(s_banner, text, sizeof(s_banner) - 1);
    s_bannerT0 = now();
    s_bannerDur = dur;
}

static void ClearAnims(void)
{
    for (int i = 0; i < 64; i++) { s_flip[i].active = false; s_pop[i] = 0; }
}

/* ---- 回合推進 ---- */

static void EvaluateTurn(void)
{
    int mover = s_board.turn;
    uint64_t mv = othello_moves(s_board.bb[mover], s_board.bb[mover ^ 1]);

    if (mv) {
        s_hintSq = -1;
        s_phase = IsAITurn() ? PH_AI : PH_HUMAN;
        return;
    }
    uint64_t other = othello_moves(s_board.bb[mover ^ 1], s_board.bb[mover]);
    if (!other) {   /* 雙方皆無著 → 終局 */
        s_phase = PH_OVER;
        s_overT0 = now();
        s_overSoundDone = false;
        return;
    }
    ShowBanner(TextFormat("%s無子可下，跳過回合", ColorName(mover)), 1.15);
    audio_play(SND_PASS);
    s_passT0 = now();
    s_phase = PH_PASS;
}

static void DoMove(int sq)
{
    Board pre = s_board;
    int mover = s_board.turn;
    uint64_t f = 0;
    othello_apply(&s_board, sq, &f);

    if (s_histLen < (int)(sizeof(s_hist) / sizeof(s_hist[0])))
        s_hist[s_histLen++] = (Hist){ pre, sq, mover, __builtin_popcountll(f) };
    s_histScroll = 0;

    double t = now();
    s_pop[sq] = t;
    float stagger = 0.05f / AnimSpeed();
    float dur = 0.34f / AnimSpeed();
    for (uint64_t m = f; m; m &= m - 1) {
        int i = __builtin_ctzll(m);
        int dist = abs(i / 8 - sq / 8);
        if (dist < abs(i % 8 - sq % 8)) dist = abs(i % 8 - sq % 8);
        s_flip[i] = (FlipAnim){
            .active = true, .start = t + dist * stagger, .dur = dur,
            .from = mover ^ 1, .to = mover, .lastT = -1,
        };
    }
    audio_play(SND_PLACE);
    s_hintSq = -1;
    s_phase = PH_ANIM;
}

/* ---- 復原 ---- */
static void Undo(void)
{
    if (s_histLen == 0 || s_phase == PH_OVER || s_paused) return;
    ai_cancel();
    s_aiPending = false;
    s_hintReq = false;
    do {
        s_histLen--;
        s_board = s_hist[s_histLen].pre;
    } while (s_mode == MODE_PVE && s_human >= 0 && s_histLen > 0
             && s_board.turn != s_human);
    ClearAnims();
    s_hintSq = -1;
    s_phase = IsAITurn() ? PH_AI : PH_HUMAN;
    audio_play(SND_CLICK);
}

/* ---- 提示 ---- */
static void DoHint(void)
{
    if (s_phase != PH_HUMAN || s_aiPending || s_paused) return;
    s_hintReq = true;
    s_aiPending = true;
    ai_start(&s_board, DIFF_HINT);
}

/* ---- 場景生命週期 ---- */

void GameScene_Enter(GameMode mode, Difficulty diff, int humanColor)
{
    s_mode = mode;
    s_diff = diff;
    s_human = humanColor;
    s_paused = false;
    s_histLen = 0;
    s_histScroll = 0;
    s_hintSq = -1;
    s_hintReq = false;
    s_aiPending = false;
    s_hoverSq = -1;
    s_overSoundDone = true;   /* 開局不播音 */
    ClearAnims();

    board_init(&s_board);
    s_scoreShown[0] = 2;
    s_scoreShown[1] = 2;

    s_introT0 = now();
    s_pop[27] = s_introT0;
    s_pop[28] = s_introT0 + 0.07;
    s_pop[35] = s_introT0 + 0.14;
    s_pop[36] = s_introT0 + 0.21;

    if (humanColor < 0)            ShowBanner("電腦對戰示範", 1.4);
    else if (mode == MODE_PVP)     ShowBanner("黑方先行", 1.4);
    else if (humanColor == CBLACK)  ShowBanner("你執黑先行", 1.4);
    else                           ShowBanner("你執白後手", 1.4);

    s_phase = PH_ANIM;
}

/* ---- 更新 ---- */

static void UpdateHumanInput(void)
{
    /* 滑鼠懸停 */
    s_hoverSq = -1;
    Vector2 m = MouseV();
    if (m.x >= BOARD_X && m.x < BOARD_X + 8 * CELL &&
        m.y >= BOARD_Y && m.y < BOARD_Y + 8 * CELL) {
        int c = (int)((m.x - BOARD_X) / CELL);
        int r = (int)((m.y - BOARD_Y) / CELL);
        s_hoverSq = r * 8 + c;
    }

    /* 合法著點 */
    int mover = s_board.turn;
    uint64_t mv = othello_moves(s_board.bb[mover], s_board.bb[mover ^ 1]);

    if (s_hoverSq >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (mv & (1ULL << s_hoverSq)) {
            DoMove(s_hoverSq);
        } else {
            audio_play(SND_INVALID);
        }
    }
}

static void UpdateAIPoll(void)
{
    if (!s_aiPending) return;
    int sq;
    if (!ai_poll(&sq, NULL)) return;
    s_aiPending = false;

    if (s_hintReq) {          /* 提示請求 */
        s_hintReq = false;
        s_hintSq = sq;
        s_hintT0 = now();
        return;
    }
    if (sq != INVALID_SQ) DoMove(sq);
    else EvaluateTurn();
}

void GameScene_Update(void)
{
    if (g_inputLocked) return;   /* 轉場中凍結對局 */

    float dt = GetFrameTime();
    double t = now();

    /* 分數動畫 */
    for (int i = 0; i < 2; i++) {
        float target = (float)board_count(&s_board, i);
        s_scoreShown[i] += (target - s_scoreShown[i]) * (dt * 10.0f > 1 ? 1 : dt * 10.0f);
    }

    if (IsKeyPressed(KEY_ESCAPE) && s_phase != PH_OVER)
        s_paused = !s_paused;

    if (s_paused) return;

    /* 鍵盤快捷鍵 */
    if (IsKeyPressed(KEY_U)) Undo();
    if (IsKeyPressed(KEY_H)) DoHint();

    switch (s_phase) {
    case PH_ANIM: {
        bool done = (t - s_introT0) > 0.55;
        for (int i = 0; i < 64; i++)
            if (s_flip[i].active && (t - s_flip[i].start) > s_flip[i].dur)
                s_flip[i].active = false;
        for (int i = 0; i < 64 && done; i++)
            if (s_flip[i].active) done = false;
        if (done) EvaluateTurn();
        break;
    }
    case PH_HUMAN:
        UpdateAIPoll();
        if (s_phase == PH_HUMAN && !s_aiPending) UpdateHumanInput();
        break;
    case PH_AI:
        if (!s_aiPending) {
            s_aiPending = true;
            ai_start(&s_board, s_diff);
        }
        UpdateAIPoll();
        break;
    case PH_PASS:
        if (t - s_passT0 > 1.15) {
            s_board.turn ^= 1;
            EvaluateTurn();
        }
        break;
    case PH_OVER:
        break;
    }
}

/* ---- 繪製 ---- */

static void DrawDiscAt(Vector2 c, float r, int color, float sy, float extraAlpha)
{
    /* 陰影 */
    DrawEllipseV((Vector2){ c.x + 2, c.y + 4 }, r * 0.95f, r * 0.55f * sy,
                 (Color){ 0, 0, 0, (unsigned char)(g_theme.shadow.a * extraAlpha) });

    Color rim   = g_theme.discRim;
    Color body  = color == CBLACK ? g_theme.blackDisc : g_theme.whiteDisc;
    Color hi    = color == CBLACK ? g_theme.blackDiscHi : g_theme.whiteDiscHi;
    rim.a = body.a = hi.a = (unsigned char)(255 * extraAlpha);

    DrawEllipseV(c, r, r * sy, rim);
    DrawEllipseV(c, r - 1.5f, (r - 1.5f) * sy, body);
    if (sy > 0.35f)   /* 高光：翻到側面時不畫 */
        DrawEllipseV((Vector2){ c.x - r * 0.22f, c.y - r * 0.30f * sy },
                     r * 0.52f, r * 0.34f * sy,
                     (Color){ hi.r, hi.g, hi.b, (unsigned char)(hi.a * 0.55f) });
}

static void DrawBoardBase(void)
{
    Rectangle frame = { BOARD_X - 22, BOARD_Y - 22, 8 * CELL + 44, 8 * CELL + 44 };
    DrawRectangleRounded(frame, 0.035f, 10, g_theme.boardFrame);
    DrawRectangleRoundedLinesEx(frame, 0.035f, 10, 2.0f,
                                (Color){ 0, 0, 0, 60 });
    DrawRectangle((int)BOARD_X, (int)BOARD_Y, (int)(8 * CELL), (int)(8 * CELL),
                  g_theme.boardFelt);

    /* 格線 */
    for (int i = 0; i <= 8; i++) {
        float p = i * CELL;
        DrawLineEx((Vector2){ BOARD_X + p, BOARD_Y },
                   (Vector2){ BOARD_X + p, BOARD_Y + 8 * CELL }, 2, g_theme.gridLine);
        DrawLineEx((Vector2){ BOARD_X, BOARD_Y + p },
                   (Vector2){ BOARD_X + 8 * CELL, BOARD_Y + p }, 2, g_theme.gridLine);
    }

    /* 星位 */
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            DrawCircleV((Vector2){ BOARD_X + (2 + i * 4) * CELL,
                                   BOARD_Y + (2 + j * 4) * CELL }, 4.5f, g_theme.starDot);

    /* 座標 */
    if (g_settings.coords) {
        for (int c = 0; c < 8; c++)
            TextV(TextFormat("%c", 'a' + c),
                  (int)(BOARD_X + c * CELL + CELL / 2), (int)BOARD_Y - 11, 15,
                  g_theme.coordText);
        for (int r = 0; r < 8; r++)
            TextV(TextFormat("%d", r + 1),
                  (int)BOARD_X - 11, (int)(BOARD_Y + r * CELL + CELL / 2), 15,
                  g_theme.coordText);
    }
}

static void DrawDiscs(void)
{
    double t = now();
    int flipSndCount = 0;
    float popDur = 0.28f / AnimSpeed();

    for (int sq = 0; sq < 64; sq++) {
        int color = (s_board.bb[CBLACK] >> sq) & 1 ? CBLACK
                  : (s_board.bb[CWHITE] >> sq) & 1 ? CWHITE : -1;
        Vector2 c = SqCenter(sq);

        /* 彈跳動畫 */
        float scale = 1.0f;
        if (s_pop[sq] > 0) {
            float pt = (float)((t - s_pop[sq]) / popDur);
            if (pt < 0) continue;                    /* 開局尚未出現 */
            if (pt >= 1) s_pop[sq] = 0;
            else scale = easeOutBack(pt < 0 ? 0 : pt);
        }
        if (color < 0) continue;

        FlipAnim *fa = &s_flip[sq];
        if (fa->active) {
            float ft = (float)((t - fa->start) / fa->dur);
            if (ft < 0) {                            /* 尚未輪到：維持原色 */
                DrawDiscAt(c, CELL * 0.42f * scale, fa->from, 1, 1);
                continue;
            }
            if (ft >= 1) { fa->active = false; }
            else {
                float sy = fabsf(cosf(PI * ft));
                int col = ft < 0.5f ? fa->from : fa->to;
                DrawDiscAt(c, CELL * 0.42f * scale, col, sy < 0.12f ? 0.12f : sy, 1);
                if (sy < 0.92f)   /* 翻轉中加深，模擬側面受光 */
                    DrawEllipseV(c, CELL * 0.42f * scale, CELL * 0.42f * sy,
                                 (Color){ 0, 0, 0, (unsigned char)(sinf(PI * ft) * 70) });
                if (fa->lastT < 0.5f && ft >= 0.5f && flipSndCount < 3) {
                    audio_play(SND_FLIP);
                    flipSndCount++;
                }
                fa->lastT = ft;
                continue;
            }
        }
        DrawDiscAt(c, CELL * 0.42f * scale, color, 1, 1);
    }
}

static void DrawMarkers(void)
{
    double t = now();

    /* 上一手標記 */
    if (s_board.lastMove != INVALID_SQ && !s_flip[s_board.lastMove].active) {
        DrawEllipseLinesV(SqCenter(s_board.lastMove), CELL * 0.42f + 5,
                          CELL * 0.42f + 5, g_theme.lastMoveRing);
    }

    /* 合法著點與懸停預覽 */
    if (s_phase == PH_HUMAN && !s_paused) {
        int mover = s_board.turn;
        uint64_t mv = othello_moves(s_board.bb[mover], s_board.bb[mover ^ 1]);

        for (uint64_t m = mv; m; m &= m - 1) {
            int sq = __builtin_ctzll(m);
            if (sq == s_hoverSq) continue;
            float r = 6.5f + 1.8f * sinf((float)t * 4.0f);
            Color c = g_theme.accent;
            c.a = 165;
            DrawCircleV(SqCenter(sq), r, c);
        }

        /* 懸停：幽靈子 + 翻轉數預覽 */
        if (s_hoverSq >= 0 && (mv & (1ULL << s_hoverSq))) {
            uint64_t f = othello_flips(s_board.bb[mover], s_board.bb[mover ^ 1], s_hoverSq);
            Color ghost = mover == CBLACK ? g_theme.blackDisc : g_theme.whiteDisc;
            ghost.a = g_theme.ghost.a;
            DrawEllipseV(SqCenter(s_hoverSq), CELL * 0.42f, CELL * 0.42f, ghost);
            DrawEllipseLinesV(SqCenter(s_hoverSq), CELL * 0.42f, CELL * 0.42f,
                              (Color){ g_theme.textMain.r, g_theme.textMain.g,
                                       g_theme.textMain.b, 110 });
            if (__builtin_popcountll(f) > 0)
                TextV(TextFormat("×%d", __builtin_popcountll(f)),
                      (int)SqCenter(s_hoverSq).x, (int)(SqCenter(s_hoverSq).y + CELL / 2 - 12),
                      15, (Color){ g_theme.textMain.r, g_theme.textMain.g,
                                   g_theme.textMain.b, 190 });
        }
    }

    /* 提示標記 */
    if (s_hintSq >= 0 && t - s_hintT0 < 2.2) {
        float pulse = 0.5f + 0.5f * sinf((float)t * 6.0f);
        DrawEllipseLinesV(SqCenter(s_hintSq), CELL * 0.42f + 8 + pulse * 4,
                          CELL * 0.42f + 8 + pulse * 4, g_theme.accent);
    }
}

static void DrawPlayerCard(Rectangle r, int color)
{
    DrawPanelV(r);
    bool active = s_board.turn == color && s_phase != PH_OVER;

    if (active) {
        Rectangle bar = { r.x + 2, r.y + 10, 4, r.height - 20 };
        DrawRectangleRounded(bar, 0.5f, 4, g_theme.accent);
    }

    /* 棋子圖示 */
    Vector2 ic = { r.x + 52, r.y + r.height / 2 };
    DrawDiscAt(ic, 26, color, 1, 1);

    /* 名稱 */
    const char *name;
    if (s_mode == MODE_PVP)      name = ColorName(color);
    else if (s_human < 0)        name = TextFormat("電腦（%s）", ColorName(color));
    else if (color == s_human)   name = TextFormat("你（%s）", ColorName(color));
    else                         name = TextFormat("電腦（%s）", DiffName());
    TextVL(name, (int)r.x + 100, (int)(r.y + 34), 24, g_theme.textMain);

    /* 狀態 */
    if (active && (s_phase == PH_AI || (s_phase == PH_ANIM && IsAITurn()))) {
        /* 旋轉指示 */
        float a = (float)(GetTime() * 300.0);
        DrawRing((Vector2){ r.x + 112, r.y + 74 }, 7, 10, a, a + 250, 0, g_theme.accent);
        TextVL("思考中…", (int)r.x + 132, (int)(r.y + 74), 18, g_theme.textDim);
    } else if (active) {
        TextVL("行動中", (int)r.x + 100, (int)(r.y + 74), 18, g_theme.accent);
    } else {
        TextVL("等待", (int)r.x + 100, (int)(r.y + 74), 18, g_theme.textDim);
    }

    /* 分數 */
    TextVR(TextFormat("%d", (int)(s_scoreShown[color] + 0.5f)),
           (int)(r.x + r.width - 36), (int)(r.y + r.height / 2), 44, g_theme.textMain);
}

static void DrawHistory(Rectangle r)
{
    DrawPanelV(r);
    TextVL(TextFormat("對局紀錄（%d 手）", s_histLen), (int)r.x + 20, (int)(r.y + 26),
           20, g_theme.textDim);

    float viewY = r.y + 52;
    float viewH = r.height - 64;
    int visible = (int)(viewH / 26);
    if (visible < 1) visible = 1;

    /* 滾輪（放大範圍方便捲動） */
    Vector2 m = MouseV();
    if (CheckCollisionPointRec(m, (Rectangle){ r.x, r.y, r.width, r.height })) {
        float wheel = GetMouseWheelMove();
        int maxOff = s_histLen > visible ? s_histLen - visible : 0;
        s_histScroll -= wheel;
        if (s_histScroll < 0) s_histScroll = 0;
        if (s_histScroll > maxOff) s_histScroll = (float)maxOff;
    }

    BeginScissorMode((int)r.x + 8, (int)viewY, (int)r.width - 16, (int)viewH);
    for (int k = 0; k < visible; k++) {
        int idx = s_histLen - (int)s_histScroll - 1 - k;
        if (idx < 0) break;
        float y = viewY + 13 + k * 26;
        const Hist *h = &s_hist[idx];

        TextVR(TextFormat("%d.", idx + 1), (int)r.x + 66, (int)y, 17, g_theme.textDim);
        DrawCircleV((Vector2){ r.x + 88, y }, 7,
                    h->mover == CBLACK ? g_theme.blackDisc : g_theme.whiteDisc);
        DrawCircleLinesV((Vector2){ r.x + 88, y }, 7, g_theme.panelEdge);
        TextVL(sq_name(h->sq), (int)r.x + 108, (int)y, 18, g_theme.textMain);
        if (h->flips > 0)
            TextVL(TextFormat("翻 %d 顆", h->flips), (int)r.x + 168, (int)y, 16,
                   g_theme.textDim);
    }
    EndScissorMode();
}

static void DrawSidebar(void)
{
    Rectangle card1 = { SIDE_X, 120, SIDE_W, 110 };
    Rectangle card2 = { SIDE_X, 246, SIDE_W, 110 };
    DrawPlayerCard(card1, CBLACK);
    DrawPlayerCard(card2, CWHITE);

    Rectangle hist = { SIDE_X, 372, SIDE_W, 248 };
    DrawHistory(hist);

    bool busy = s_paused || s_phase == PH_OVER;
    bool canUndo = !busy && s_histLen > 0 && (s_phase == PH_HUMAN || s_phase == PH_PASS || s_phase == PH_ANIM);
    bool canHint = !busy && s_phase == PH_HUMAN && !s_aiPending;

    if (ButtonV((Rectangle){ SIDE_X, 640, 160, 56 }, "復原 (U)", 20, !canUndo)) Undo();
    if (ButtonV((Rectangle){ SIDE_X + 176, 640, 160, 56 }, "提示 (H)", 20, !canHint)) DoHint();
    if (ButtonV((Rectangle){ SIDE_X + 352, 640, 160, 56 }, "暫停", 20, busy))
        s_paused = true;
}

static void DrawHeader(void)
{
    if (ButtonV((Rectangle){ 28, 26, 130, 46 }, "‹ 選單", 20, false))
        RequestScreen(SCREEN_MENU);

    const char *title;
    if (s_mode == MODE_PVP)     title = "雙人對戰";
    else if (s_human < 0)       title = "電腦示範";
    else                        title = TextFormat("人機對戰 · %s · 你執%s",
                                                   DiffName(), s_human == CBLACK ? "黑" : "白");
    TextV(title, (int)VW / 2, 49, 26, g_theme.textDim);
}

static void DrawBanner(void)
{
    double t = now();
    if (t - s_bannerT0 >= s_bannerDur) return;
    float alpha = (float)fmin(1.0, fmin((t - s_bannerT0) / 0.15,
                                        (s_bannerDur - (t - s_bannerT0)) / 0.3));
    float w = TextW(s_banner, 24);
    Rectangle pill = { VW / 2 - w / 2 - 28, 38, w + 56, 50 };
    Color bg = g_theme.panel, edge = g_theme.accent, txt = g_theme.textMain;
    bg.a = edge.a = (unsigned char)(255 * alpha);
    txt.a = (unsigned char)(255 * alpha);
    DrawRectangleRounded(pill, 0.5f, 10, bg);
    DrawRectangleRoundedLinesEx(pill, 0.5f, 10, 2, edge);
    TextV(s_banner, (int)VW / 2, 63, 24, txt);
}

static void DrawOverlayDim(void)
{
    DrawRectangle(0, 0, (int)VW, (int)VH, (Color){ 0, 0, 0, 150 });
}

static void DrawPauseOverlay(void)
{
    DrawOverlayDim();
    Rectangle p = { VW / 2 - 210, 260, 420, 280 };
    DrawPanelV(p);
    TextV("暫停", (int)VW / 2, (int)p.y + 56, 36, g_theme.textMain);
    if (ButtonV((Rectangle){ p.x + 60, p.y + 110, 300, 54 }, "繼續對局", 22, false))
        s_paused = false;
    if (ButtonV((Rectangle){ p.x + 60, p.y + 178, 300, 54 }, "回主選單", 22, false))
        RequestScreen(SCREEN_MENU);
}

static void DrawOverOverlay(void)
{
    /* 進場縮放 */
    float t = (float)(now() - s_overT0);
    float s = easeOutBack(t < 0 ? 0 : (t > 0.45f ? 1 : t / 0.45f));

    DrawOverlayDim();
    Rectangle p = {
        VW / 2 - 270 * s, 400 - 190 * s, 540 * s, 380 * s,
    };
    if (p.width < 1) return;

    /* 結算音效：進場後 0.3 秒 */
    if (!s_overSoundDone && t > 0.3) {
        s_overSoundDone = true;
        int b = board_count(&s_board, CBLACK), w = board_count(&s_board, CWHITE);
        if (s_mode == MODE_PVE && s_human >= 0)
            audio_play((s_human == CBLACK && b > w) || (s_human == CWHITE && w > b)
                       ? SND_WIN : SND_LOSE);
        else
            audio_play(SND_WIN);
    }

    DrawPanelV(p);
    int alpha = (int)(255 * (t < 0.2f ? t / 0.2f : 1));
    Color main = g_theme.textMain; main.a = (unsigned char)alpha;
    Color dim  = g_theme.textDim;  dim.a  = (unsigned char)alpha;
    Color acc  = g_theme.accent;   acc.a  = (unsigned char)alpha;

    TextV("對局結束", (int)(p.x + p.width / 2), (int)(p.y + 58), 30, dim);

    int b = board_count(&s_board, CBLACK), w = board_count(&s_board, CWHITE);
    const char *result;
    if (s_mode == MODE_PVE && s_human >= 0) {
        bool humanWin = (s_human == CBLACK && b > w) || (s_human == CWHITE && w > b);
        result = humanWin ? "你獲勝了！" : "電腦獲勝";
    } else {
        result = b > w ? "黑方獲勝" : w > b ? "白方獲勝" : "平手";
    }
    TextV(result, (int)(p.x + p.width / 2), (int)(p.y + 130), 52,
          (b == w) ? dim : acc);

    TextV(TextFormat("黑 %d  —  %d 白", b, w),
          (int)(p.x + p.width / 2), (int)(p.y + 208), 34, main);

    if (t > 0.35f) {
        if (ButtonV((Rectangle){ p.x + 70, p.y + 268, 190, 54 }, "再來一局", 22, false))
            GameScene_Enter(s_mode, s_diff, s_human);
        if (ButtonV((Rectangle){ p.x + 280, p.y + 268, 190, 54 }, "回主選單", 22, false))
            RequestScreen(SCREEN_MENU);
    }
}

void GameScene_Draw(void)
{
    DrawHeader();
    DrawBoardBase();
    DrawMarkers();
    DrawDiscs();
    DrawSidebar();
    DrawBanner();

    if (s_paused)              { DrawPauseOverlay(); return; }
    if (s_phase == PH_OVER)    { DrawOverOverlay(); return; }
}
