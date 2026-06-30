#ifndef WEB_AVATAR_SERVER_H
#define WEB_AVATAR_SERVER_H

extern volatile bool g_webVoiceRequested;

#include <WiFi.h>

void initGifServer();
void setGifFaceIndex(int index);
int getGifFaceIndex();

#endif
