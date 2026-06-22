#ifndef CAMERA_MODE_H
#define CAMERA_MODE_H

#include <Arduino.h>
#include "config.h"

#if USE_CAMERA

void setupCameraMode();
void loopMusicPlayer();
void startCameraMode();
void stopCameraMode();
bool isCameraModeActive();
void loopCameraMode();

// カメラ専用ブートモード
void setupCameraBootMode();
void loopCameraBootMode();
void initCameraWebServer();

#endif // USE_CAMERA

extern volatile int g_faceDetectedCount;

#endif // CAMERA_MODE_H