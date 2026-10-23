/* test_engine.c — 規則引擎驗證
 *
 * 以「逐格逐方向步行」的樸素實作作為對照組，
 * 與 bitboard 實作在大量隨機對局盤面上互相比較。
 *
 * 編譯與執行見 test.bat。
 */
#include "../src/othello.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ---- 樸素對照實作（邊界檢查版） ---- */

static const int DR[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
static const int DC[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };

static uint64_t naive_flips(uint64_t P, uint64_t O, int sq)
{
    int r = sq / 8, c = sq % 8;
    uint64_t out = 0;
    for (int d = 0; d < 8; d++) {
        uint64_t run = 0;
        int rr = r + DR[d], cc = c + DC[d];
        for (;;) {
            if (rr < 0 || rr > 7 || cc < 0 || cc > 7) { run = 0; break; }
            uint64_t bit = 1ULL << (rr * 8 + cc);
            if (O & bit) { run |= bit; rr += DR[d]; cc += DC[d]; continue; }
            if (P & bit) break;
            run = 0; break;
        }
        out |= run;
    }
    return out;
}

static uint64_t naive_moves(uint64_t P, uint64_t O)
{
    uint64_t m = 0;
    uint64_t empty = ~(P | O);
    while (empty) {
        int sq = __builtin_ctzll(empty);
        empty &= empty - 1;
        if (naive_flips(P, O, sq)) m |= 1ULL << sq;
    }
    return m;
}

/* ---- 隨機亂數 ---- */
static uint64_t rng = 0x123456789ABCDEFULL;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t)(rng >> 32);
}

int main(void)
{
    /* 1. 初始盤面：合法著點恰為 d3/c4/f5/e6 */
    Board b;
    board_init(&b);
    CHECK(b.bb[CBLACK] == ((1ULL << 28) | (1ULL << 35)), "black init bits");
    CHECK(b.bb[CWHITE] == ((1ULL << 27) | (1ULL << 36)), "white init bits");
    uint64_t mv = othello_moves(b.bb[CBLACK], b.bb[CWHITE]);
    uint64_t expect = (1ULL << 19) | (1ULL << 26) | (1ULL << 37) | (1ULL << 44);
    CHECK(mv == expect, "initial moves: got %llx want %llx",
          (unsigned long long)mv, (unsigned long long)expect);

    /* 2. 開局 d3 恰翻 d4 一顆 */
    Board t = b;
    uint64_t f = 0;
    CHECK(othello_apply(&t, 19, &f), "apply d3");
    CHECK(f == (1ULL << 27), "d3 flips d4: got %llx", (unsigned long long)f);
    CHECK(t.bb[CBLACK] == ((1ULL << 28) | (1ULL << 35) | (1ULL << 19) | (1ULL << 27)),
          "after d3 black bits");
    CHECK(t.turn == CWHITE, "turn switched");

    /* 3. 大量隨機對局：bitboard 與樸素實作互相比較 + 盤面不變量 */
    int games = 300, grandTotal = 0, passPairs = 0;
    for (int g = 0; g < games; g++) {
        Board bd;
        board_init(&bd);
        int lastPass = 0;
        int totalMoves = 0;

        for (;;) {
            uint64_t P = bd.bb[bd.turn], O = bd.bb[bd.turn ^ 1];
            uint64_t mvbb = othello_moves(P, O);
            uint64_t mvnv = naive_moves(P, O);
            CHECK(mvbb == mvnv, "moves mismatch game %d", g);

            /* 每個空格逐一比較翻轉位圖 */
            uint64_t empty = ~(P | O);
            while (empty) {
                int sq = __builtin_ctzll(empty);
                empty &= empty - 1;
                uint64_t fbb = othello_flips(P, O, sq);
                uint64_t fnv = naive_flips(P, O, sq);
                CHECK(fbb == fnv, "flips mismatch game %d sq %d: %llx vs %llx",
                      g, sq, (unsigned long long)fbb, (unsigned long long)fnv);
            }

            if (!mvbb) {
                uint64_t omv = othello_moves(O, P);
                if (!omv) break;   /* 終局 */
                bd.turn ^= 1;      /* 虛手 */
                if (lastPass) break;
                lastPass = 1;
                passPairs++;
                continue;
            }
            lastPass = 0;

            /* 隨機挑一個合法著點 */
            int pick = (int)(rnd() % (uint32_t)__builtin_popcountll(mvbb));
            uint64_t m = mvbb;
            for (int i = 0; i < pick; i++) m &= m - 1;
            int sq = __builtin_ctzll(m);

            uint64_t fl = 0;
            CHECK(othello_apply(&bd, sq, &fl), "apply legal move must succeed");
            CHECK(fl == naive_flips(P, O, sq), "apply flips mismatch");
            totalMoves++;
            grandTotal++;

            /* 不變量：雙方位圖不重疊，總子數 = 4 + 手數 */
            CHECK((bd.bb[CBLACK] & bd.bb[CWHITE]) == 0, "overlap");
            CHECK(__builtin_popcountll(bd.bb[CBLACK] | bd.bb[CWHITE]) == 4 + totalMoves,
                  "disc count drift");
        }
        CHECK(totalMoves <= 60, "too many moves");
    }
    printf("engine test: %d games, %d moves, %d pass-pairs, %d failures\n",
           games, grandTotal, passPairs, failures);
    return failures ? 1 : 0;
}
