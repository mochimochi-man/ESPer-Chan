#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "config.h"

void initDisplay();
void reinitDisplaySPI();  // MusicMode終了時にSPI2 IDF DMAバスを再構築する

// ============================================
// FixedFace: TFT デフォルト顔の中心座標修正
// sprite->createSprite(320,240) が M5.Display.height() を破壊するバグを回避。
// SSD1306 とカスタム顔では不要。
// ============================================
#if USE_AVATAR
#include <Avatar.h>
#include <DrawContext.h>

namespace m5avatar {
class FixedFace : public Face {
    int32_t _cx, _cy;
public:
    FixedFace(int32_t cx, int32_t cy) : Face(), _cx(cx), _cy(cy) {}

    void draw(DrawContext *ctx) override {
        sprite->createSprite(boundingRect->getWidth(), boundingRect->getHeight());
        sprite->setColorDepth(ctx->getColorDepth());
        sprite->setBitmapColor(
            ctx->getColorPalette()->get(COLOR_PRIMARY),
            ctx->getColorPalette()->get(COLOR_BACKGROUND));
        if (ctx->getColorDepth() != 1) {
            sprite->fillSprite(ctx->getColorPalette()->get(COLOR_BACKGROUND));
        } else {
            sprite->fillSprite(0);
        }
        float breath = ctx->getBreath();
        if (breath > 1.0f) breath = 1.0f;

        BoundingRect rect;
        rect = *mouthPos;
        rect.setPosition(rect.getTop() + breath * 3, rect.getLeft());
        mouth->draw(sprite, rect, ctx);

        rect = *eyeRPos;
        rect.setPosition(rect.getTop() + breath * 3, rect.getLeft());
        eyeR->draw(sprite, rect, ctx);

        rect = *eyeLPos;
        rect.setPosition(rect.getTop() + breath * 3, rect.getLeft());
        eyeL->draw(sprite, rect, ctx);

        rect = *eyeblowRPos;
        rect.setPosition(rect.getTop() + breath * 3, rect.getLeft());
        eyeblowR->draw(sprite, rect, ctx);

        rect = *eyeblowLPos;
        rect.setPosition(rect.getTop() + breath * 3, rect.getLeft());
        eyeblowL->draw(sprite, rect, ctx);

        BoundingRect br;
        b->draw(sprite, br, ctx);
        h->draw(sprite, br, ctx);
        battery->draw(sprite, br, ctx);

        M5.Display.startWrite();
        sprite->pushRotateZoom(&M5.Display,
            _cx, _cy,
            ctx->getRotation(), ctx->getScale(), ctx->getScale());
        M5.Display.endWrite();

        sprite->deleteSprite();
    }
};
} // namespace m5avatar
#endif // USE_AVATAR

#endif // DISPLAY_DRIVER_H
