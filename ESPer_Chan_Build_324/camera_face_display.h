// camera_face_display.h - カメラモード専用の物理ディスプレイ顔表示
//
// カメラモード中はAvatarライブラリ(連続描画タスク+大きなスプライト合成)を使わず、
// WEB顔用に内蔵されたGIFアセット(face_assets.h)をAnimatedGIFライブラリで直接デコードし、
// 専用タスクでループ再生する。Avatarで起きていたカメラとの干渉を避けるのが目的。

#ifndef CAMERA_FACE_DISPLAY_H
#define CAMERA_FACE_DISPLAY_H

#include <Arduino.h>
#include "config.h"

#if USE_CAMERA

// ディスプレイ初期化 + GIF再生タスク起動。
// setupCameraBootMode() 内で initCamera() より前に呼ぶこと。
// 内部でM5.begin()を呼び(ESP32-S3では内部I2C(In_I2C)としてI2C_NUM_1を確保する)、
// 直後にM5.In_I2C.release()で解放するため、この後にinitCamera()のSCCB初期化を
// 呼んでもI2Cドライバー競合は起きない。
void initCameraFaceDisplay();

// true=笑顔(index1) / false=通常顔(index0) に切り替える
void setCameraFaceExpression(bool smile);

#endif // USE_CAMERA

#endif // CAMERA_FACE_DISPLAY_H
