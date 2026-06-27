// CustomFace.h
// M5 Avatar 用カスタム顔（8表情）
//
// 【Face.h 修正が必要】
// Arduino/libraries/M5Stack_Avatar/src/Face.h を以下のように修正してください：
//   1. void draw(DrawContext *ctx); → virtual void draw(DrawContext *ctx);
//   2. private: → protected:
//
// 修正しない場合は、CustomFace::draw() が呼ばれず、
// デフォルトのスタックチャン顔が表示されます。

#ifndef CUSTOM_FACE_H
#define CUSTOM_FACE_H

#include <M5Unified.h>
#include <Avatar.h>
#include "config.h"

using namespace m5avatar;

enum CustomExpression {
  EXPR_NEUTRAL = 0,
  EXPR_HAPPY,
  EXPR_SCHEMING,
  EXPR_SURPRISED,
  EXPR_ANGRY,
  EXPR_SAD,
  EXPR_PANIC,
  EXPR_DISPLEASED
};

class CustomFace : public Face {
private:
  uint8_t _customExpr;
  float _mouthOpenRatio = 0.0f;

  void drawEyeWithEyelash(M5Canvas* spi, float eyeX, float eyeY, float w, float h, float r, float openRatio, float lineWidth);
  void drawLine(M5Canvas* spi, float x1, float y1, float x2, float y2, float sw);
  void drawRectLine(M5Canvas* spi, float x1, float y1, float x2, float y2, float sw);
  void drawBezierCurve(M5Canvas* spi, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float sw);
  void drawSweatDrop(M5Canvas* spi, float cx, float cy);

public:
  CustomFace();
  ~CustomFace();

  void setCustomExpression(uint8_t expr) { _customExpr = expr; }
  uint8_t getCustomExpression() const { return _customExpr; }

  void setMouthOpenRatio(float ratio) { _mouthOpenRatio = ratio; }
  float getMouthOpenRatio() const { return _mouthOpenRatio; }



  virtual void draw(DrawContext *ctx);
};


#if USE_CUSTOM_FACE

CustomExpression detectEmotion(const String& text);

#endif

#endif
