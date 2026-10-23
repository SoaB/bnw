/* 使用方式：

#define DRAW_TTF_TEXT_IMPLEMENTATION
#include "drawttfatlas.h"

int main(void) {
    InitWindow(800, 450, "Raylib Multi-Size Text");   // 必須先建立視窗 (GL context)
    InitTTFAtlas("assets/your_font.ttf");

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawTTFTextEx("這是 60 級的大字", 50, 50, 60, MAROON);
            DrawTTFTextEx("這是 30 級的中字\n支援換行", 50, 150, 30, DARKBLUE);
            DrawFPS(10, 10);
        EndDrawing();
    }
    UnloadTTFAtlas();
    CloseWindow();
    return 0;
}
*/
#ifndef DRAW_TTF_ATLAS_H
#define DRAW_TTF_ATLAS_H

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

void InitTTFAtlas(const char *fontPath);
void DrawTTFText(const char *text, int posX, int posY, Color tint);                 // 預設大小 32
void DrawTTFTextEx(const char *text, int posX, int posY, int fontSize, Color tint);
float MeasureTTFTextEx(const char *text, int fontSize);                             // 量測寬度（px），支援 \n 取最寬行
bool TTFAtlasReady(void);
void UnloadTTFAtlas(void);

#ifdef __cplusplus
}
#endif

#endif /* DRAW_TTF_ATLAS_H */

#ifdef DRAW_TTF_TEXT_IMPLEMENTATION
#undef DRAW_TTF_TEXT_IMPLEMENTATION

#include "rlgl.h"
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//#define STBTT_STATIC                       // 避免與 raylib 內部的 stb_truetype 符號衝突
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define TTF_ATLAS_SIZE  1024
#define TTF_PAD         1
#define TTF_TABLE_BITS  12
#define TTF_TABLE_SIZE  (1 << TTF_TABLE_BITS)          // 4096，必須是 2 的次方
#define TTF_TABLE_MASK  (TTF_TABLE_SIZE - 1)
#define TTF_MAX_GLYPHS  (TTF_TABLE_SIZE * 3 / 4)       // 負載率上限 75%，維持 probe 短

typedef struct {
    uint64_t key;                // 0 代表空槽；key = (fontSize << 32) | codepoint
    unsigned short x, y, w, h;   // 在圖集中的位置
    short bearingX, bearingY;
    float advance;
} TTFGlyph;

typedef struct {
    unsigned char *fontData;
    stbtt_fontinfo info;

    TTFGlyph table[TTF_TABLE_SIZE];
    int count;

    unsigned char *pixels;       // CPU 端圖集 (GRAY_ALPHA, 2 bytes/pixel)
    Texture2D texture;
    int dirtyY0, dirtyY1;        // 尚未上傳 GPU 的列範圍

    int shelfX, shelfY, rowH;
    unsigned resetCount;
    bool isLoaded;
} TTFState;

static TTFState g_ttf;

static inline unsigned TTF_Hash(uint64_t k) {
    return (unsigned)((k * 0x9E3779B97F4A7C15ULL) >> (64 - TTF_TABLE_BITS));
}

static void TTF_FlushDirty(void) {
    if (g_ttf.dirtyY1 < g_ttf.dirtyY0) return;
    Rectangle r = { 0, (float)g_ttf.dirtyY0, TTF_ATLAS_SIZE,
                    (float)(g_ttf.dirtyY1 - g_ttf.dirtyY0 + 1) };
    // 整列連續，直接從 CPU buffer 的對應偏移上傳
    UpdateTextureRec(g_ttf.texture, r,
                     g_ttf.pixels + (size_t)g_ttf.dirtyY0 * TTF_ATLAS_SIZE * 2);
    g_ttf.dirtyY0 = INT_MAX;
    g_ttf.dirtyY1 = -1;
}

static void TTF_ResetAtlas(void) {
    TTF_FlushDirty();
    rlDrawRenderBatchActive();   // 關鍵：先把已排隊、仍引用舊圖集位置的字畫出去

    memset(g_ttf.table, 0, sizeof(g_ttf.table));
    memset(g_ttf.pixels, 0, (size_t)TTF_ATLAS_SIZE * TTF_ATLAS_SIZE * 2);
    g_ttf.count = 0;
    g_ttf.shelfX = TTF_PAD;
    g_ttf.shelfY = TTF_PAD;
    g_ttf.rowH = 0;
    g_ttf.dirtyY0 = 0;
    g_ttf.dirtyY1 = TTF_ATLAS_SIZE - 1;
    g_ttf.resetCount++;
}

void InitTTFAtlas(const char *fontPath) {
    if (g_ttf.isLoaded) UnloadTTFAtlas();

    int dataSize = 0;
    g_ttf.fontData = LoadFileData(fontPath, &dataSize);
    if (!g_ttf.fontData) return;

    int offset = stbtt_GetFontOffsetForIndex(g_ttf.fontData, 0);   // 支援 .ttc
    if (offset < 0 || !stbtt_InitFont(&g_ttf.info, g_ttf.fontData, offset)) {
        TraceLog(LOG_ERROR, "TTFAtlas: failed to parse font '%s'", fontPath);
        UnloadFileData(g_ttf.fontData);
        memset(&g_ttf, 0, sizeof(g_ttf));
        return;
    }

    g_ttf.pixels = (unsigned char *)calloc((size_t)TTF_ATLAS_SIZE * TTF_ATLAS_SIZE * 2, 1);
    if (!g_ttf.pixels) {
        UnloadFileData(g_ttf.fontData);
        memset(&g_ttf, 0, sizeof(g_ttf));
        return;
    }

    Image img = { 0 };
    img.data = g_ttf.pixels;
    img.width = TTF_ATLAS_SIZE;
    img.height = TTF_ATLAS_SIZE;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA;
    g_ttf.texture = LoadTextureFromImage(img);   // 資料會複製到 GPU，CPU buffer 仍由我們持有
    SetTextureFilter(g_ttf.texture, TEXTURE_FILTER_BILINEAR);   // 縮放時避免鋸齒

    g_ttf.shelfX = TTF_PAD;
    g_ttf.shelfY = TTF_PAD;
    g_ttf.rowH = 0;
    g_ttf.dirtyY0 = INT_MAX;
    g_ttf.dirtyY1 = -1;
    g_ttf.isLoaded = true;
}

// 查詢字形；未命中時光柵化到 CPU 圖集（不立即上傳 GPU）
static TTFGlyph *TTF_GetGlyph(int codepoint, int fontSize) {
    uint64_t key = ((uint64_t)(uint32_t)fontSize << 32) | (uint32_t)codepoint;

    unsigned idx = TTF_Hash(key);
    while (g_ttf.table[idx].key) {
        if (g_ttf.table[idx].key == key) return &g_ttf.table[idx];
        idx = (idx + 1) & TTF_TABLE_MASK;
    }

    // ---- 未命中：光柵化 ----
    float scale = stbtt_ScaleForPixelHeight(&g_ttf.info, (float)fontSize);
    int glyphIndex = stbtt_FindGlyphIndex(&g_ttf.info, codepoint);

    int advance, lsb;
    stbtt_GetGlyphHMetrics(&g_ttf.info, glyphIndex, &advance, &lsb);

    int w = 0, h = 0, xo = 0, yo = 0;
    unsigned char *bmp = stbtt_GetGlyphBitmap(&g_ttf.info, scale, scale, glyphIndex,
                                              &w, &h, &xo, &yo);   // 空字形（如空白）回傳 NULL

    // 單一字形比整張圖集還大：放棄，避免無限重置
    if (w + 2 * TTF_PAD > TTF_ATLAS_SIZE || h + 2 * TTF_PAD > TTF_ATLAS_SIZE) {
        if (bmp) stbtt_FreeBitmap(bmp, NULL);
        return NULL;
    }

    // Shelf packing：換行
    if (g_ttf.shelfX + w + TTF_PAD > TTF_ATLAS_SIZE) {
        g_ttf.shelfX = TTF_PAD;
        g_ttf.shelfY += g_ttf.rowH + TTF_PAD;
        g_ttf.rowH = 0;
    }
    // 圖集滿或 hash 表過載：重置（不遞迴；重置後保證放得下）
    if (g_ttf.shelfY + h + TTF_PAD > TTF_ATLAS_SIZE || g_ttf.count >= TTF_MAX_GLYPHS) {
        TTF_ResetAtlas();
        idx = TTF_Hash(key);   // 表已清空，第一個槽就是空的
    }

    // 寫入 CPU 圖集 (gray=255, alpha=coverage)
    for (int row = 0; row < h; row++) {
        unsigned char *dst = g_ttf.pixels +
            ((size_t)(g_ttf.shelfY + row) * TTF_ATLAS_SIZE + g_ttf.shelfX) * 2;
        const unsigned char *src = bmp + (size_t)row * w;
        for (int col = 0; col < w; col++) {
            dst[col * 2 + 0] = 255;
            dst[col * 2 + 1] = src[col];
        }
    }
    if (h > 0) {
        if (g_ttf.shelfY < g_ttf.dirtyY0) g_ttf.dirtyY0 = g_ttf.shelfY;
        if (g_ttf.shelfY + h - 1 > g_ttf.dirtyY1) g_ttf.dirtyY1 = g_ttf.shelfY + h - 1;
    }

    TTFGlyph *g = &g_ttf.table[idx];
    g->key = key;
    g->x = (unsigned short)g_ttf.shelfX;
    g->y = (unsigned short)g_ttf.shelfY;
    g->w = (unsigned short)w;
    g->h = (unsigned short)h;
    g->bearingX = (short)xo;
    g->bearingY = (short)yo;
    g->advance = advance * scale;       // 保留浮點，避免累積誤差
    g_ttf.count++;

    g_ttf.shelfX += w + TTF_PAD;
    if (h > g_ttf.rowH) g_ttf.rowH = h;

    if (bmp) stbtt_FreeBitmap(bmp, NULL);
    return g;
}

// 量測文字寬度；未快取的字形會先光柵化（與繪製流程共用快取）
float MeasureTTFTextEx(const char *text, int fontSize) {
    if (!g_ttf.isLoaded || !text || fontSize < 1) return 0.0f;

    float scale = stbtt_ScaleForPixelHeight(&g_ttf.info, (float)fontSize);
    float maxW = 0, cursorX = 0;
    int prev = 0;

    for (int i = 0; text[i] != '\0';) {
        int bytes = 1;
        int cp = GetCodepointNext(&text[i], &bytes);
        i += bytes;

        if (cp == '\n') {
            if (cursorX > maxW) maxW = cursorX;
            cursorX = 0; prev = 0; continue;
        }
        if (cp == '\r' || cp == '\t') continue;

        TTFGlyph *g = TTF_GetGlyph(cp, fontSize);
        if (!g) continue;
        if (prev) cursorX += stbtt_GetCodepointKernAdvance(&g_ttf.info, prev, cp) * scale;
        cursorX += g->advance;
        prev = cp;
    }
    return cursorX > maxW ? cursorX : maxW;
}

bool TTFAtlasReady(void) {
    return g_ttf.isLoaded;
}

void DrawTTFTextEx(const char *text, int posX, int posY, int fontSize, Color tint) {
    if (!g_ttf.isLoaded || !text) return;
    if (fontSize < 1) fontSize = 1;

    float scale = stbtt_ScaleForPixelHeight(&g_ttf.info, (float)fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_ttf.info, &ascent, &descent, &lineGap);
    float lineHeight = (ascent - descent + lineGap) * scale;

    // ---- Pass 1：確保所有字形都已光柵化，之後一次上傳 ----
    // 若中途發生圖集重置，前面確保好的字形會失效，從頭重跑一次
    for (int attempt = 0; attempt < 2; attempt++) {
        unsigned rc = g_ttf.resetCount;
        for (int i = 0; text[i] != '\0';) {
            int bytes = 1;
            int cp = GetCodepointNext(&text[i], &bytes);
            if (cp != '\n' && cp != '\r' && cp != '\t') TTF_GetGlyph(cp, fontSize);
            i += bytes;
        }
        if (rc == g_ttf.resetCount) break;
    }
    TTF_FlushDirty();                     // 整個字串最多一次 GPU 上傳

    // ---- Pass 2：繪製 ----
    float cursorX = (float)posX;
    float baseline = (float)posY + roundf(ascent * scale);
    int prev = 0;

    for (int i = 0; text[i] != '\0';) {
        int bytes = 1;
        int cp = GetCodepointNext(&text[i], &bytes);
        i += bytes;

        if (cp == '\n') { cursorX = (float)posX; baseline += lineHeight; prev = 0; continue; }
        if (cp == '\r' || cp == '\t') continue;

        TTFGlyph *g = TTF_GetGlyph(cp, fontSize);   // 通常是 hash 命中；極端情況會補光柵化
        TTF_FlushDirty();                           // 若剛好補了字形，確保 GPU 已有資料
        if (!g) continue;

        if (prev) cursorX += stbtt_GetCodepointKernAdvance(&g_ttf.info, prev, cp) * scale;

        if (g->w > 0 && g->h > 0) {
            Rectangle src = { g->x, g->y, g->w, g->h };
            Vector2 pos = { roundf(cursorX + g->bearingX), roundf(baseline + g->bearingY) };
            DrawTextureRec(g_ttf.texture, src, pos, tint);
        }
        cursorX += g->advance;
        prev = cp;
    }
}

void DrawTTFText(const char *text, int posX, int posY, Color tint) {
    DrawTTFTextEx(text, posX, posY, 32, tint);
}

void UnloadTTFAtlas(void) {
    if (g_ttf.texture.id) UnloadTexture(g_ttf.texture);
    if (g_ttf.fontData) UnloadFileData(g_ttf.fontData);
    free(g_ttf.pixels);
    memset(&g_ttf, 0, sizeof(g_ttf));
}
#endif
