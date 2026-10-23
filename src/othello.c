/* othello.c — 黑白棋規則引擎（bitboard 實作） */
#include "othello.h"

/* 各方向位移時防止跨列出界的來源遮罩 */
#define NOT_A 0xFEFEFEFEFEFEFEFEULL   /* 排除 A 欄（col 0） */
#define NOT_H 0x7F7F7F7F7F7F7F7FULL   /* 排除 H 欄（col 7） */

static inline uint64_t shift_m(uint64_t b, int sh, uint64_t ms)
{
    return sh >= 0 ? (b & ms) << sh : (b & ms) >> (-sh);
}

/* 單方向：掃過一段連續敵子後落到空格的著點（敵子段最長 6，故走 6 步） */
static inline uint64_t ray_moves(uint64_t P, uint64_t O, uint64_t empty, int sh, uint64_t ms)
{
    uint64_t t = shift_m(P, sh, ms) & O;
    uint64_t acc = t;
    for (int i = 0; i < 5; i++) {
        t = shift_m(t, sh, ms) & O;
        acc |= t;
    }
    return shift_m(acc, sh, ms) & empty;
}

/* 單方向：從 sq 出發收集夾在敵子之間、可翻轉的位圖 */
static inline uint64_t ray_flips(uint64_t P, uint64_t O, int sq, int sh, uint64_t ms)
{
    uint64_t t = 1ULL << sq, acc = 0;
    for (;;) {
        t = shift_m(t, sh, ms);
        if (t & O) { acc |= t; continue; }
        return (t & P) ? acc : 0;   /* t 為 0 時自然回傳 0 */
    }
}

#define FOR_EACH_DIR(SH, MS) ray_moves(P, O, empty, (SH), (MS))

uint64_t othello_moves(uint64_t P, uint64_t O)
{
    uint64_t empty = ~(P | O), m = 0;
    m |= FOR_EACH_DIR( 1, NOT_H);   /* 東 */
    m |= FOR_EACH_DIR(-1, NOT_A);   /* 西 */
    m |= FOR_EACH_DIR( 8, ~0ULL);   /* 南 */
    m |= FOR_EACH_DIR(-8, ~0ULL);   /* 北 */
    m |= FOR_EACH_DIR( 9, NOT_H);   /* 東南 */
    m |= FOR_EACH_DIR( 7, NOT_A);   /* 西南 */
    m |= FOR_EACH_DIR(-7, NOT_H);   /* 東北 */
    m |= FOR_EACH_DIR(-9, NOT_A);   /* 西北 */
    return m;
}

uint64_t othello_flips(uint64_t P, uint64_t O, int sq)
{
    uint64_t f = 0;
    f |= ray_flips(P, O, sq,  1, NOT_H);
    f |= ray_flips(P, O, sq, -1, NOT_A);
    f |= ray_flips(P, O, sq,  8, ~0ULL);
    f |= ray_flips(P, O, sq, -8, ~0ULL);
    f |= ray_flips(P, O, sq,  9, NOT_H);
    f |= ray_flips(P, O, sq,  7, NOT_A);
    f |= ray_flips(P, O, sq, -7, NOT_H);
    f |= ray_flips(P, O, sq, -9, NOT_A);
    return f;
}

bool othello_apply(Board *b, int sq, uint64_t *flipsOut)
{
    uint64_t bit = 1ULL << sq;
    uint64_t P = b->bb[b->turn], O = b->bb[b->turn ^ 1];

    uint64_t f = othello_flips(P, O, sq);
    if (!f) return false;   /* 沒有夾到任何敵子 → 非法 */

    b->bb[b->turn] |= bit | f;
    b->bb[b->turn ^ 1] &= ~f;
    b->lastMove = sq;
    b->turn ^= 1;
    if (flipsOut) *flipsOut = f;
    return true;
}

void board_init(Board *b)
{
    /* 標準開局：白 d4/e5，黑 d5/e4（列 1 在上） */
    b->bb[CBLACK] = (1ULL << 28) | (1ULL << 35);   /* e4, d5 */
    b->bb[CWHITE] = (1ULL << 27) | (1ULL << 36);   /* d4, e5 */
    b->turn = CBLACK;
    b->lastMove = INVALID_SQ;
}

int board_count(const Board *b, int color)
{
    return __builtin_popcountll(b->bb[color]);
}

const char *sq_name(int sq)
{
    static char names[64][3] = { 0 };
    static bool built = false;
    if (!built) {
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                names[r * 8 + c][0] = (char)('a' + c);
                names[r * 8 + c][1] = (char)('1' + r);
            }
        built = true;
    }
    return (sq >= 0 && sq < 64) ? names[sq] : "--";
}
