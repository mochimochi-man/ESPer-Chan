#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <Arduino.h>

#if USE_CAMERA
    #include "camera_mode.h"
#endif

void preinitMusicPlayerSD();  // setup()内でinitDisplay()直後に呼ぶ
void setupMusicPlayer();
void loopMusicPlayer();
void startMusicMode();
void stopMusicMode(bool showPrompt = true);
bool isMusicModeActive();
void handleMusicSerialCommand(const String& cmd);

String getMusicStatusJSON();
void handleWebMusicCommand(const String& cmd);
#endif
