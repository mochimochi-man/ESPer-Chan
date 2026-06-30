#include "camera_face_display.h"

#if USE_CAMERA

#include "settings.h"
#include "face_assets.h"
#include "display_driver.h"
#include <M5Unified.h>
#include <AnimatedGIF.h>

// WEB顔GIFアセットの実寸 (Web_Avatar_assets*.h のGIFヘッダから確認済み)
#define FACE_CANVAS_W 320
#define FACE_CANVAS_H 240

static M5Canvas faceCanvas(&M5.Display);
static AnimatedGIF gif;
static TaskHandle_t camFaceTaskHandle = nullptr;

volatile int g_camFaceGifIndex = FACE_GIF_NEUTRAL;

// ============================================
// AnimatedGIF描画コールバック: 1スキャンラインずつfaceCanvasへ書き込む
// (AnimatedGIFライブラリ付属 ESP32-LGFX-SDCard-GifPlayer の例に準拠)
// ============================================
static void GIFDrawCallback(GIFDRAW* pDraw) {
    uint16_t usTemp[FACE_CANVAS_W];
    uint16_t* usPalette = pDraw->pPalette;
    int y = pDraw->iY + pDraw->y;
    int iWidth = pDraw->iWidth;
    if (iWidth > FACE_CANVAS_W) iWidth = FACE_CANVAS_W;

    uint8_t* s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) {
        for (int x = 0; x < iWidth; x++) {
            if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
        }
        pDraw->ucHasTransparency = 0;
    }

    if (pDraw->ucHasTransparency) {
        uint8_t* pEnd = s + iWidth;
        uint8_t ucTransparent = pDraw->ucTransparent;
        int x = 0, iCount = 0;
        uint16_t* d;
        uint8_t c;
        while (x < iWidth) {
            c = ucTransparent - 1;
            d = usTemp;
            while (c != ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) {
                    s--;
                } else {
                    *d++ = usPalette[c];
                    iCount++;
                }
            }
            if (iCount) {
                faceCanvas.pushImage(pDraw->iX + x, y, iCount, 1, usTemp);
                x += iCount;
                iCount = 0;
            }
            c = ucTransparent;
            while (c == ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) iCount++;
                else s--;
            }
            if (iCount) { x += iCount; iCount = 0; }
        }
    } else {
        for (int x = 0; x < iWidth; x++) usTemp[x] = usPalette[s[x]];
        faceCanvas.pushImage(pDraw->iX, y, iWidth, 1, usTemp);
    }
}

// ============================================
// GIF再生タスク (Core0にピン留め。カメラ/メインloop()はCore1)
// ============================================
static void cameraFaceDisplayTask(void*) {
    int playing = -1;

    for (;;) {
        int want = g_camFaceGifIndex;
        if (want != playing) {
            size_t sz = 0;
            const uint8_t* data = getFaceGifData(want, &sz);
            if (data && sz > 0) {
                gif.close();
                if (gif.open((uint8_t*)data, (int)sz, GIFDrawCallback)) {
                    playing = want;
                    faceCanvas.fillSprite(TFT_BLACK);  // GIF切り替え時のみ全消去
                }
            }
        }

        if (playing < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // GIFは差分(瞬きなら目の周辺だけ等)のみ再描画する形式が多いため、
        // 毎フレームの全消去はしない(口など差分に含まれない部分が消えてしまう)。
        int delayMs = 0;
        if (!gif.playFrame(false, &delayMs)) {
            // 1ループ再生終了。先頭から再生し直す
            size_t sz = 0;
            const uint8_t* data = getFaceGifData(playing, &sz);
            gif.close();
            if (data && sz > 0) gif.open((uint8_t*)data, (int)sz, GIFDrawCallback);
            delayMs = 10;
        }

        float scale = (USE_CUSTOM_FACE && appSettings.faceType == 1) ? AVATAR_SCALE_ORIGINAL : AVATAR_SCALE_DEFAULT;
        faceCanvas.pushRotateZoom(&M5.Display, M5.Display.width() / 2, M5.Display.height() / 2, 0, scale, scale);

        vTaskDelay(pdMS_TO_TICKS(delayMs > 0 ? delayMs : 50));
    }
}

void initCameraFaceDisplay() {
    decodeFaceAssets(USE_CUSTOM_FACE && (appSettings.faceType == 1));

    initDisplay();

    // M5.begin()(initDisplay内)はESP32-S3では内部I2C(In_I2C)としてI2C_NUM_1を確保する
    // (IMU/RTC等は無効化済みで未使用のバスのため解放して問題ない)。
    // 解放しないと、この後のinitCamera()のSCCB用i2c_driver_installがI2C_NUM_1で
    // 衝突しESP_ERR_INVALID_STATEで失敗する。
    // 注: 新I2Cドライバー(driver_ng)と旧API(i2c_driver_delete等)は混在できず併用するとabortする
    // ため、M5Unified自身の解放API(M5.In_I2C.release())を使うこと。
    M5.In_I2C.release();

    faceCanvas.setPsram(true);
    faceCanvas.setColorDepth(16);
    faceCanvas.createSprite(FACE_CANVAS_W, FACE_CANVAS_H);
    faceCanvas.fillSprite(TFT_BLACK);

    gif.begin(LITTLE_ENDIAN_PIXELS);

    g_camFaceGifIndex = FACE_GIF_NEUTRAL;
    xTaskCreatePinnedToCore(cameraFaceDisplayTask, "camFace", 8192, nullptr, 1, &camFaceTaskHandle, 0);
}

void setCameraFaceExpression(bool smile) {
    g_camFaceGifIndex = smile ? FACE_GIF_HAPPY : FACE_GIF_NEUTRAL;
}

#endif // USE_CAMERA
