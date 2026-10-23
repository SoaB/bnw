/* audio.c — 程序化合成音效
 *
 * 所有音效皆於啟動時以數學合成產生（44.1kHz / 16bit / mono），
 * 不依賴任何外部音檔。音訊裝置初始化失敗時靜默停用。
 */
#include "audio.h"
#include "raylib.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 44100

static Sound g_sounds[SND_COUNT];
static bool  g_ok = false;
static bool  g_enabled = true;

/* ---- 波形產生輔助 ---- */

typedef struct {
    float *buf;      /* 幅度 [-1,1] */
    int    n;
} Bus;

static Bus bus_new(float seconds)
{
    Bus b = { (float *)calloc((int)(seconds * SAMPLE_RATE) + 1, sizeof(float)),
              (int)(seconds * SAMPLE_RATE) };
    return b;
}

/* 在時間 at 加入一個帶指數衰減的掃頻音 */
static void bus_tone(Bus *b, float at, float dur, float f0, float f1, float vol, float decay)
{
    int i0 = (int)(at * SAMPLE_RATE);
    float phase = 0.0f;
    for (int i = 0; i < (int)(dur * SAMPLE_RATE); i++) {
        int idx = i0 + i;
        if (idx >= b->n) break;
        float t = (float)i / SAMPLE_RATE;
        float f = f0 + (f1 - f0) * (t / dur);
        phase += 2.0f * 3.14159265f * f / SAMPLE_RATE;
        float env = vol * expf(-t * decay);
        b->buf[idx] += sinf(phase) * env;
    }
}

/* 加入一個短暫的雜訊脈衝 */
static void bus_noise(Bus *b, float at, float dur, float vol, float decay)
{
    int i0 = (int)(at * SAMPLE_RATE);
    unsigned seed = 12345;
    for (int i = 0; i < (int)(dur * SAMPLE_RATE); i++) {
        int idx = i0 + i;
        if (idx >= b->n) break;
        float t = (float)i / SAMPLE_RATE;
        seed = seed * 1103515245u + 12345u;
        float white = ((int)(seed >> 16) % 2000) / 1000.0f - 1.0f;
        b->buf[idx] += white * vol * expf(-t * decay);
    }
}

static Sound bus_to_sound(Bus *b)
{
    /* 軟性限幅，避免疊加後破音 */
    for (int i = 0; i < b->n; i++) {
        float v = tanhf(b->buf[i] * 1.2f);
        b->buf[i] = v;
    }
    short *data = (short *)malloc((size_t)b->n * sizeof(short));
    for (int i = 0; i < b->n; i++)
        data[i] = (short)(b->buf[i] * 32000.0f);

    Wave w = { 0 };
    w.frameCount = b->n;
    w.sampleRate = SAMPLE_RATE;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;

    Sound s = LoadSoundFromWave(w);
    free(data);
    free(b->buf);
    return s;
}

void audio_init(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    g_ok = true;

    {   /* 落子：低頻叩擊 + 高頻瞬態 */
        Bus b = bus_new(0.16f);
        bus_tone(&b, 0.00f, 0.14f, 420, 150, 0.9f, 34);
        bus_noise(&b, 0.00f, 0.012f, 0.5f, 260);
        g_sounds[SND_PLACE] = bus_to_sound(&b);
    }
    {   /* 翻子：輕快的木琴短音 */
        Bus b = bus_new(0.10f);
        bus_tone(&b, 0.0f, 0.08f, 760, 620, 0.45f, 46);
        g_sounds[SND_FLIP] = bus_to_sound(&b);
    }
    {   /* 無效操作：低鳴 */
        Bus b = bus_new(0.18f);
        bus_tone(&b, 0.0f, 0.15f, 130, 110, 0.5f, 18);
        g_sounds[SND_INVALID] = bus_to_sound(&b);
    }
    {   /* UI 點擊 */
        Bus b = bus_new(0.05f);
        bus_tone(&b, 0.0f, 0.04f, 1150, 900, 0.35f, 90);
        g_sounds[SND_CLICK] = bus_to_sound(&b);
    }
    {   /* 虛手：雙短音 */
        Bus b = bus_new(0.22f);
        bus_tone(&b, 0.00f, 0.05f, 520, 520, 0.4f, 60);
        bus_tone(&b, 0.10f, 0.05f, 390, 390, 0.4f, 60);
        g_sounds[SND_PASS] = bus_to_sound(&b);
    }
    {   /* 勝利：上行琶音 C5-E5-G5-C6 */
        Bus b = bus_new(0.85f);
        static const float f[4] = { 523.25f, 659.25f, 783.99f, 1046.50f };
        for (int i = 0; i < 4; i++)
            bus_tone(&b, i * 0.13f, 0.5f, f[i], f[i], 0.42f, 7);
        g_sounds[SND_WIN] = bus_to_sound(&b);
    }
    {   /* 失敗：下行三音 */
        Bus b = bus_new(0.9f);
        static const float f[3] = { 392.0f, 329.63f, 261.63f };
        for (int i = 0; i < 3; i++)
            bus_tone(&b, i * 0.17f, 0.55f, f[i], f[i], 0.4f, 6);
        g_sounds[SND_LOSE] = bus_to_sound(&b);
    }
}

void audio_play(SoundId id)
{
    if (!g_ok || !g_enabled) return;
    if (g_sounds[id].frameCount > 0) PlaySound(g_sounds[id]);
}

void audio_set_enabled(bool on)
{
    g_enabled = on;
}
