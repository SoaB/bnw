/* othello.h — 黑白棋規則引擎（bitboard 實作）
 *
 * 棋盤位元布局：sq = row * 8 + col，row 0 在上、col 0 在左（A 欄）。
 * bit(sq) = 1ULL << sq。
 */
#ifndef OTHELLO_H
#define OTHELLO_H

#include <stdint.h>
#include <stdbool.h>

/* 棋子顏色（不與 raylib 的 BLACK/WHITE 巨集同名） */
enum { CBLACK = 0, CWHITE = 1 };
#define INVALID_SQ (-1)

/* AI 難度（DIFF_HINT 供「提示」按鈕使用：中速、無隨機性） */
typedef enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_HINT } Difficulty;

typedef struct {
    uint64_t bb[2];   /* [BLACK] / [WHITE] 各自的佔領位圖 */
    int      turn;    /* BLACK 或 WHITE */
    int      lastMove;/* 上一手位置，無則 INVALID_SQ */
} Board;

void      board_init(Board *b);
uint64_t  othello_moves(uint64_t P, uint64_t O);          /* P 方所有合法著點 */
uint64_t  othello_flips(uint64_t P, uint64_t O, int sq);  /* 在 sq 落子會翻轉的位圖 */
bool      othello_apply(Board *b, int sq, uint64_t *flipsOut); /* 非法落子回傳 false */
int       board_count(const Board *b, int color);
const char *sq_name(int sq); /* 0 -> "a1"（左上），顯示用 */

#endif /* OTHELLO_H */
