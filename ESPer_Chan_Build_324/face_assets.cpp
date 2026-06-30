#include "face_assets.h"
#include "config.h"
#include "Web_Avatar_assets.h"
#include "Web_Avatar_assets_default.h"
#include "mbedtls/base64.h"
#include <esp_heap_caps.h>
#include <string.h>

static uint8_t* FACE_DATA[FACE_COUNT] = {nullptr};
static size_t FACE_SIZE[FACE_COUNT] = {0};
static int decodedForCustom = -1;  // -1=未デコード, 0=デフォルト顔, 1=オリジナル顔

// ESPer-Chanオリジナル顔 (Web_Avatar_assets.h)
static const char* const FACE_B64_CUSTOM[FACE_COUNT] = {
    GIF_BLINK_NEUTRAL_B64,     GIF_BLINK_HAPPY_B64,     GIF_BLINK_SAD_B64,
    GIF_BLINK_ANGRY_B64,       GIF_BLINK_PANIC_B64,     GIF_LIPSYNC_NEUTRAL_B64,
    GIF_LIPSYNC_SAD_B64,       GIF_LIPSYNC_ANGRY_B64,   GIF_LIPSYNC_PANIC_B64,
    GIF_SURPRISED_B64,         GIF_LIPSYNC_NEUTRAL_B64
};

// StackChanデフォルト顔 (Web_Avatar_assets_default.h)
static const char* const FACE_B64_DEFAULT[FACE_COUNT] = {
    GIF_BLINK_NEUTRAL_DEF_B64, GIF_BLINK_HAPPY_DEF_B64, GIF_BLINK_SAD_DEF_B64,
    GIF_BLINK_ANGRY_DEF_B64,   GIF_BLINK_PANIC_DEF_B64, GIF_LIPSYNC_NEUTRAL_DEF_B64,
    GIF_LIPSYNC_SAD_DEF_B64,   GIF_LIPSYNC_ANGRY_DEF_B64, GIF_LIPSYNC_PANIC_DEF_B64,
    GIF_SURPRISED_DEF_B64,     GIF_LIPSYNC_HAPPY_DEF_B64
};

static void freeFaceAssets() {
    for (int i = 0; i < FACE_COUNT; i++) {
        if (FACE_DATA[i]) { free(FACE_DATA[i]); FACE_DATA[i] = nullptr; }
        FACE_SIZE[i] = 0;
    }
}

void decodeFaceAssets(bool useCustomFace) {
    int want = useCustomFace ? 1 : 0;
    if (decodedForCustom == want) return;

    freeFaceAssets();
    const char* const* table = useCustomFace ? FACE_B64_CUSTOM : FACE_B64_DEFAULT;
    LOG_I("[FACE] Decoding %d faces from Base64 (custom=%d)...", FACE_COUNT, (int)useCustomFace);
    for (int i = 0; i < FACE_COUNT; i++) {
        const char* b64 = table[i];
        if (!b64) continue;
        size_t b64_len = strlen(b64);
        size_t alloc_len = (b64_len * 3) / 4 + 8;

        FACE_DATA[i] = (uint8_t*)heap_caps_malloc(alloc_len, MALLOC_CAP_SPIRAM);
        if (!FACE_DATA[i]) FACE_DATA[i] = (uint8_t*)malloc(alloc_len);
        if (!FACE_DATA[i]) {
            LOG_E("[FACE] Face %d malloc failed (%d bytes)", i + 1, (int)alloc_len);
            continue;
        }

        size_t out_len = 0;
        int ret = mbedtls_base64_decode(FACE_DATA[i], alloc_len, &out_len,
                                        (const unsigned char*)b64, b64_len);
        if (ret == 0 && out_len > 0) {
            FACE_SIZE[i] = out_len;
            LOG_I("[FACE] Face %d: OK (%d bytes)", i + 1, (int)out_len);
        } else {
            LOG_E("[FACE] Face %d decode failed (ret=%d)", i + 1, ret);
            free(FACE_DATA[i]); FACE_DATA[i] = nullptr; FACE_SIZE[i] = 0;
        }
    }
    decodedForCustom = want;
}

const uint8_t* getFaceGifData(int idx, size_t* outSize) {
    if (idx < 0 || idx >= FACE_COUNT || !FACE_DATA[idx]) {
        if (outSize) *outSize = 0;
        return nullptr;
    }
    if (outSize) *outSize = FACE_SIZE[idx];
    return FACE_DATA[idx];
}
