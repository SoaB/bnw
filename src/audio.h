/* audio.h — 程序化合成音效（無需外部音檔） */
#ifndef AUDIO_H
#define AUDIO_H

typedef enum {
    SND_PLACE,    /* 落子 */
    SND_FLIP,     /* 翻子 */
    SND_INVALID,  /* 無效點擊 */
    SND_CLICK,    /* UI 點擊 */
    SND_PASS,     /* 虛手 */
    SND_WIN,      /* 勝利 */
    SND_LOSE,     /* 失敗 */
    SND_COUNT
} SoundId;

void audio_init(void);
void audio_play(SoundId id);
void audio_set_enabled(bool on);

#endif /* AUDIO_H */
