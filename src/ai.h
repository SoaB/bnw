/* ai.h — 黑白棋 AI（Alpha-Beta 搜尋，於背景執行緒運算） */
#ifndef AI_H
#define AI_H

#include "othello.h"

/* 提交一筆運算請求（會自動等待/取消前一份未完成的工作）
 *   humanColor 僅供內部評估參考；hintMode = true 時以固定難度快速求解。 */
void ai_start(const Board *b, Difficulty diff);
bool ai_poll(int *bestSq, int *score);   /* 完成時回傳 true；未啟動回傳 false */
void ai_cancel(void);                    /* 請求中止（最多短暫等待） */
void ai_shutdown(void);                  /* 程式結束前呼叫 */

#endif /* AI_H */
