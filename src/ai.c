/* ai.c — 黑白棋 AI（Alpha-Beta 搜尋 + 迭代加深 + 終局精算）
 *
 * 於背景執行緒運算，與主執行緒僅透過 AIJob 結構溝通；
 * 執行緒內不呼叫任何 raylib 函式。
 */
#include "ai.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string.h>

#define INF       1000000
#define EG_SOLVE  13      /* 空格數 ≤ 此值時直接精算終局 */

/* 防跨列遮罩（與 othello.c 一致） */
#define NOT_A 0xFEFEFEFEFEFEFEFEULL
#define NOT_H 0x7F7F7F7F7F7F7F7FULL

/* ---- 時間源（背景執行緒使用，不碰 raylib） ---- */
static double now_sec(void)
{
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}

/* ---- 亂數（xorshift64，用於走子隨機化） ---- */
static uint64_t g_rng;
static inline uint32_t rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 32);
}

/* ---- 工作請求 ---- */
typedef struct {
    Board        board;
    Difficulty   diff;
    volatile LONG done;
    volatile LONG stop;
    int          best;
    int          score;
} AIJob;

static AIJob  g_job;
static HANDLE g_thread = NULL;

/* ---- 評分表（角 > 邊 > 中央，X/C 位為負） ---- */
static const int W[64] = {
    100, -25,  12,   6,   6,  12, -25, 100,
    -25, -45,  -4,  -2,  -2,  -4, -45, -25,
     12,  -4,   5,   2,   2,   5,  -4,  12,
      6,  -2,   2,   1,   1,   2,  -2,   6,
      6,  -2,   2,   1,   1,   2,  -2,   6,
     12,  -4,   5,   2,   2,   5,  -4,  12,
    -25, -45,  -4,  -2,  -2,  -4, -45, -25,
    100, -25,  12,   6,   6,  12, -25, 100,
};

#define CORNERS (0x8100000000000081ULL)

static uint64_t neighbors(uint64_t e)
{
    return ((e & NOT_A) >> 1) | ((e & NOT_H) << 1) | (e >> 8) | (e << 8)
         | ((e & NOT_A) >> 9) | ((e & NOT_H) << 9)
         | ((e & NOT_A) >> 7) | ((e & NOT_H) << 7);
}

/* 由 P（行動方）視角評分 */
static int evaluate(uint64_t P, uint64_t O, int empties)
{
    int pos = 0;
    for (int sq = 0; sq < 64; sq++) {
        uint64_t bit = 1ULL << sq;
        if (P & bit)      pos += W[sq];
        else if (O & bit) pos -= W[sq];
    }
    int mob = __builtin_popcountll(othello_moves(P, O))
            - __builtin_popcountll(othello_moves(O, P));

    uint64_t e = ~(P | O);
    uint64_t fr = neighbors(e) & (P | O);
    int front = __builtin_popcountll(fr & P) - __builtin_popcountll(fr & O);

    int corner = __builtin_popcountll(P & CORNERS) - __builtin_popcountll(O & CORNERS);
    int disc   = __builtin_popcountll(P) - __builtin_popcountll(O);

    int discW = empties <= 16 ? 5 : (empties <= 28 ? 1 : 0);
    return pos + 13 * mob - 6 * front + 28 * corner + discW * disc;
}

/* ---- 搜尋狀態 ---- */
static double g_deadline;
static int64_t g_nodes;
static volatile int g_stop;

static inline bool time_up(void)
{
    if ((++g_nodes & 4095) != 0) return false;
    return g_stop || now_sec() > g_deadline;
}

/* negamax；pass = 上一手是否為虛手（用於偵測終局） */
static int search(uint64_t P, uint64_t O, int empties, int depth, int alpha, int beta, bool pass)
{
    if (time_up()) return alpha;

    if (empties == 0)
        return 100 * (__builtin_popcountll(P) - __builtin_popcountll(O));

    uint64_t mv = othello_moves(P, O);
    if (!mv) {
        if (pass)   /* 雙方皆無著 → 終局 */
            return 100 * (__builtin_popcountll(P) - __builtin_popcountll(O));
        return -search(O, P, empties, depth, -beta, -alpha, true);
    }
    if (depth <= 0)
        return evaluate(P, O, empties);

    /* 依位置加權排序（簡單插入排序，著點 ≤ 20） */
    int sqs[24], n = 0;
    for (uint64_t m = mv; m; m &= m - 1)
        sqs[n++] = __builtin_ctzll(m);
    for (int i = 1; i < n; i++) {
        int s = sqs[i], j = i - 1;
        while (j >= 0 && W[sqs[j]] < W[s]) { sqs[j + 1] = sqs[j]; j--; }
        sqs[j + 1] = s;
    }

    int best = -INF;
    for (int i = 0; i < n; i++) {
        int sq = sqs[i];
        uint64_t bit = 1ULL << sq;
        uint64_t f = othello_flips(P, O, sq);
        int v = -search(O & ~f, P | bit | f, empties - 1, depth - 1, -beta, -alpha, false);
        if (g_stop) return best;
        if (v > best) best = v;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;
    }
    return best;
}

/* 根節點：回傳最佳著點；prevBest 排在最前以加速迭代加深 */
static int root_search(uint64_t P, uint64_t O, int empties, int depth, int prevBest)
{
    int sqs[24], n = 0;
    for (uint64_t m = othello_moves(P, O); m; m &= m - 1)
        sqs[n++] = __builtin_ctzll(m);
    if (n == 0) return INVALID_SQ;

    /* 同權重內隨機打散，讓對局有變化 */
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        int t = sqs[i]; sqs[i] = sqs[j]; sqs[j] = t;
    }
    for (int i = 1; i < n; i++) {
        int s = sqs[i], j = i - 1;
        while (j >= 0 && W[sqs[j]] < W[s]) { sqs[j + 1] = sqs[j]; j--; }
        sqs[j + 1] = s;
    }
    if (prevBest >= 0) {
        for (int i = 0; i < n; i++)
            if (sqs[i] == prevBest) {
                int t = sqs[0]; sqs[0] = sqs[i]; sqs[i] = t;
                break;
            }
    }

    int best = -INF, bestSq = sqs[0];
    int alpha = -INF, beta = INF;
    for (int i = 0; i < n; i++) {
        int sq = sqs[i];
        uint64_t bit = 1ULL << sq;
        uint64_t f = othello_flips(P, O, sq);
        int v = -search(O & ~f, P | bit | f, empties - 1, depth - 1, -beta, -alpha, false);
        if (g_stop) break;
        if (v > best) { best = v; bestSq = sq; }
        if (best > alpha) alpha = best;
    }
    return bestSq;
}

/* 簡單難度：貪心 + 隨機性 */
static int easy_move(uint64_t P, uint64_t O, int empties)
{
    int sqs[24], n = 0;
    for (uint64_t m = othello_moves(P, O); m; m &= m - 1)
        sqs[n++] = __builtin_ctzll(m);

    if (rnd() % 100 < 30) return sqs[rnd() % (uint32_t)n];

    int best = -INF, bestSq = sqs[0];
    for (int i = 0; i < n; i++) {
        int sq = sqs[i];
        uint64_t bit = 1ULL << sq;
        uint64_t f = othello_flips(P, O, sq);
        int v = evaluate(P | bit | f, O & ~f, empties - 1);
        if (v > best) { best = v; bestSq = sq; }
    }
    return bestSq;
}

static DWORD WINAPI ai_thread(LPVOID arg)
{
    (void)arg;
    const Board *b = &g_job.board;
    uint64_t P = b->bb[b->turn], O = b->bb[b->turn ^ 1];
    int empties = 64 - __builtin_popcountll(P | O);

    double budget = g_job.diff == DIFF_EASY   ? 0.0
                  : g_job.diff == DIFF_NORMAL ? 0.30
                  : g_job.diff == DIFF_HARD   ? 1.10
                  :                             0.12;   /* DIFF_HINT */
    g_deadline = now_sec() + budget;
    g_nodes = 0;
    g_stop = 0;

    int best = INVALID_SQ, score = 0;
    if (g_job.diff == DIFF_EASY) {
        best = easy_move(P, O, empties);
    } else {
        /* 空格少時直接精算：深度設得比空格大，完成即為精確解 */
        int maxd = empties <= EG_SOLVE ? empties + 4
                 : g_job.diff == DIFF_NORMAL ? 6 : 12;
        for (int d = 1; d <= maxd; d++) {
            int sq = root_search(P, O, empties, d, best);
            if (g_stop) break;
            best = sq;
            score = 0;   /* 分數僅供顯示，這裡不另外取出 */
        }
    }
    g_job.best = best;
    g_job.score = score;
    InterlockedExchange(&g_job.done, 1);
    return 0;
}

/* ---- 對外介面 ---- */

void ai_start(const Board *b, Difficulty diff)
{
    ai_cancel();   /* 確保前一份工作已結束 */

    g_job.board = *b;
    g_job.diff = diff;
    g_job.best = INVALID_SQ;
    g_job.score = 0;
    g_job.done = 0;
    g_job.stop = 0;
    g_rng = (uint64_t)GetTickCount64() ^ 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uintptr_t)&g_job;

    g_thread = CreateThread(NULL, 0, ai_thread, NULL, 0, NULL);
}

bool ai_poll(int *bestSq, int *score)
{
    if (!g_thread) return false;
    if (!g_job.done) return false;
    CloseHandle(g_thread);
    g_thread = NULL;
    if (bestSq) *bestSq = g_job.best;
    if (score)  *score = g_job.score;
    return true;
}

void ai_cancel(void)
{
    if (!g_thread) return;
    InterlockedExchange(&g_job.stop, 1);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = NULL;
}

void ai_shutdown(void)
{
    ai_cancel();
}
