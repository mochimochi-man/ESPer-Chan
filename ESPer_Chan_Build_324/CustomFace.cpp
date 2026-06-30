#include "CustomFace.h"

// Defined here so it exists whenever CustomFace is compiled.
// music_player.cpp sets this true before avatar.suspend() to guarantee the
// drawLoop exits draw() before any startWrite(), preventing _transaction_count
// underflow after M5.Display.init() resets the counter to 0 on SPI reinit.
volatile bool g_musicModeSuspending = false;

CustomFace::CustomFace() : Face(), _customExpr(EXPR_NEUTRAL), _mouthOpenRatio(0.0f) {
  b = new Balloon();
  h = new Effect();
  battery = new BatteryIcon();
}

CustomFace::~CustomFace() {}

// --- 描画ヘルパー ---

void CustomFace::drawEyeWithEyelash(M5Canvas* spi, float eyeX, float eyeY, float w, float h, float r, float openRatio, float lineWidth) {
  int ix = (int)eyeX, iy = (int)eyeY, iw = (int)w, ih = (int)(h * openRatio), ir = (int)r;
  if (iw < 1) iw = 1; if (ih < 1) ih = 1; if (ir < 0) ir = 0;

  if (openRatio > 0.15f) {
    int yOffset = (int)(h * (1.0f - openRatio) / 2.0f);
    spi->fillRoundRect(ix, iy + yOffset, iw, ih, ir, 1);

    float lashX1 = eyeX - 7.0f;
    float lashY1 = eyeY - 7.0f;
    float lashX2 = eyeX + 15.0f;
    float lashY2 = eyeY + 15.0f;
    float dx = lashX2 - lashX1;
    float dy = lashY2 - lashY1;
    float len = sqrt(dx*dx + dy*dy);
    if (len > 0.0f) {
      float perpX = -dy / len;
      float perpY =  dx / len;
      const float halfW = 3.5f;
      float x0 = lashX1 + perpX * halfW;
      float y0 = lashY1 + perpY * halfW;
      float x1 = lashX1 - perpX * halfW;
      float y1 = lashY1 - perpY * halfW;
      float x2 = lashX2 - perpX * halfW;
      float y2 = lashY2 - perpY * halfW;
      float x3 = lashX2 + perpX * halfW;
      float y3 = lashY2 + perpY * halfW;
      spi->fillTriangle((int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, 1);
      spi->fillTriangle((int)x0, (int)y0, (int)x2, (int)y2, (int)x3, (int)y3, 1);
    }
  } else {
    int eyelidY = iy + (int)(h * 0.7f);
    int s = (int)lineWidth;
    if (s < 1) s = 1;
    for (int i = 0; i < s; i++) {
      int offset = i - s / 2;
      spi->drawLine(ix, eyelidY + offset, ix + iw, eyelidY + offset, 1);
    }
  }
}

void CustomFace::drawLine(M5Canvas* spi, float x1, float y1, float x2, float y2, float sw) {
  int ix1 = (int)x1, iy1 = (int)y1, ix2 = (int)x2, iy2 = (int)y2;
  int s = (int)sw;
  if (s < 1) s = 1;
  for (int i = 0; i < s; i++) {
    int offset = i - s / 2;
    spi->drawLine(ix1, iy1 + offset, ix2, iy2 + offset, 1);
  }
}

void CustomFace::drawRectLine(M5Canvas* spi, float x1, float y1, float x2, float y2, float sw) {
  float lx = x2 - x1, ly = y2 - y1;
  float len = sqrt(lx*lx + ly*ly);
  if (len <= 0.0f) return;
  float px = -ly / len, py = lx / len;
  float hw = sw * 0.5f;
  float ax = x1 + px*hw, ay = y1 + py*hw;
  float bx = x1 - px*hw, by = y1 - py*hw;
  float cx = x2 - px*hw, cy = y2 - py*hw;
  float ex = x2 + px*hw, ey = y2 + py*hw;
  spi->fillTriangle((int)ax, (int)ay, (int)bx, (int)by, (int)cx, (int)cy, 1);
  spi->fillTriangle((int)ax, (int)ay, (int)cx, (int)cy, (int)ex, (int)ey, 1);
}

void CustomFace::drawBezierCurve(M5Canvas* spi, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float sw) {
  int segments = 24;
  float prevX = x0, prevY = y0;
  int s = (int)sw;
  if (s < 1) s = 1;
  for (int i = 1; i <= segments; i++) {
    float t = i / (float)segments;
    float mt = 1.0f - t;
    float x = mt*mt*mt*x0 + 3*mt*mt*t*x1 + 3*mt*t*t*x2 + t*t*t*x3;
    float y = mt*mt*mt*y0 + 3*mt*mt*t*y1 + 3*mt*t*t*y2 + t*t*t*y3;
    int ix0 = (int)prevX, iy0 = (int)prevY;
    int ix1 = (int)x, iy1 = (int)y;
    for (int j = 0; j < s; j++) {
      int offset = j - s / 2;
      spi->drawLine(ix0, iy0 + offset, ix1, iy1 + offset, 1);
    }
    prevX = x; prevY = y;
  }
}

void CustomFace::drawSweatDrop(M5Canvas* spi, float cx, float cy) {
  int x = (int)cx, y = (int)cy;
  spi->fillTriangle(x, y - 20, x - 12, y + 4, x + 12, y + 4, 1);
  spi->fillCircle(x, y + 8, 12, 1);
}

// --- メイン描画 ---

void CustomFace::draw(DrawContext *ctx) {
  if (g_musicModeSuspending) return;  // safe: before createSprite() / startWrite()
  sprite->createSprite(boundingRect->getWidth(), boundingRect->getHeight());
  sprite->setColorDepth(ctx->getColorDepth());
  sprite->setBitmapColor(ctx->getColorPalette()->get(COLOR_PRIMARY),
                         ctx->getColorPalette()->get(COLOR_BACKGROUND));
  if (ctx->getColorDepth() != 1) {
    sprite->fillSprite(ctx->getColorPalette()->get(COLOR_BACKGROUND));
  } else {
    sprite->fillSprite(0);
  }

  float breath = ctx->getBreath();
  int breathY = (int)(breath * 3.0f);

  Gaze leftGaze = ctx->getLeftGaze();
  int offsetX = (int)(leftGaze.getHorizontal() * 6.0f);
  int offsetY = (int)(leftGaze.getVertical() * 3.0f) + breathY;

  float leftOpen = ctx->getLeftEyeOpenRatio();
  float rightOpen = ctx->getRightEyeOpenRatio();

  const float SW = 4.0f;

  // === 目＋まつ毛 ===
  drawEyeWithEyelash(sprite, 68.0f + offsetX, 68.0f + offsetY, 40.0f, 72.0f, 20.0f, leftOpen, SW);
  drawEyeWithEyelash(sprite, 212.0f + offsetX, 68.0f + offsetY, 40.0f, 72.0f, 20.0f, rightOpen, SW);

  // === 口（表情ごと）===
  switch (_customExpr) {
    case EXPR_NEUTRAL:
      if (_mouthOpenRatio <= 0.0f) {
        drawLine(sprite, 128.0f, 184.0f + breathY, 192.0f, 184.0f + breathY, SW);
      }
      break;
    case EXPR_HAPPY:
      if (_mouthOpenRatio <= 0.0f) {
        drawBezierCurve(sprite, 124.0f, 176.0f + breathY, 132.0f, 188.0f + breathY, 188.0f, 188.0f + breathY, 196.0f, 176.0f + breathY, SW);
      }
      break;
    case EXPR_SCHEMING:
      if (_mouthOpenRatio <= 0.0f) {
        drawLine(sprite, 128.0f, 184.0f + breathY, 176.0f, 184.0f + breathY, SW);
        drawBezierCurve(sprite, 176.0f, 184.0f + breathY, 176.0f, 184.0f + breathY, 184.0f, 184.0f + breathY, 200.0f, 176.0f + breathY, SW);
        drawLine(sprite, 188.0f, 164.0f + breathY, 208.0f, 184.0f + breathY, SW);
      }
      break;
    case EXPR_SURPRISED:
      if (_mouthOpenRatio <= 0.0f) {
        sprite->fillRoundRect(140, 152 + breathY, 40, 52, 4, 1);
      }
      break;
    case EXPR_ANGRY:
      if (_mouthOpenRatio <= 0.0f) {
        drawLine(sprite, 128.0f, 184.0f + breathY, 192.0f, 184.0f + breathY, SW);
      }
      drawRectLine(sprite, 200.0f, 32.0f + breathY, 176.0f, 56.0f + breathY, 7.0f);
      drawRectLine(sprite, 136.0f, 56.0f + breathY, 112.0f, 32.0f + breathY, 7.0f);
      break;
    case EXPR_SAD:
      if (_mouthOpenRatio <= 0.0f) {
        drawLine(sprite, 128.0f, 184.0f + breathY, 192.0f, 184.0f + breathY, SW);
      }
      drawRectLine(sprite, 132.0f, 20.0f + breathY, 108.0f, 44.0f + breathY, 7.0f);
      drawRectLine(sprite, 212.0f, 44.0f + breathY, 188.0f, 20.0f + breathY, 7.0f);
      break;
    case EXPR_PANIC:
      if (_mouthOpenRatio <= 0.0f) {
        drawLine(sprite, 128.0f, 184.0f + breathY, 192.0f, 184.0f + breathY, SW);
      }
      drawSweatDrop(sprite, 296.0f, 124.0f + breathY);
      break;
    case EXPR_DISPLEASED:
      if (_mouthOpenRatio <= 0.0f) {
        drawBezierCurve(sprite, 196.0f, 190.0f + breathY, 188.0f, 178.0f + breathY, 132.0f, 178.0f + breathY, 124.0f, 190.0f + breathY, SW);
      }
      break;
    default:
      drawLine(sprite, 128.0f, 184.0f + breathY, 192.0f, 184.0f + breathY, SW);
      break;
  }

  // === リップシンク: 口を動的に上書き ===
  if (_mouthOpenRatio > 0.0f) {
    int cx = 160;
    int cy = 184 + breathY;
    int rw = 56;
    int rh = (int)(32 * _mouthOpenRatio);
    if (rh < 2) rh = 2;
    sprite->fillRoundRect(cx - rw/2, cy - rh/2, rw, rh, 4, 1);
  }

  float scale    = ctx->getScale();
  float rotation = ctx->getRotation();
  M5.Display.startWrite();
  sprite->pushRotateZoom(&M5.Display,
      (M5.Display.width()  >> 1) + AVATAR_X_OFFSET,
      (M5.Display.height() >> 1) + AVATAR_Y_OFFSET,
      rotation, scale, scale);
  M5.Display.endWrite();

  sprite->deleteSprite();
}
