// settings.h - 実行時設定管理 (Preferences/NVS)

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Preferences.h>
#include <WebServer.h>

extern WebServer webServer;

struct AppSettings {
    char wifiSsid[32];
    char wifiPassword[64];
    char lmHost[32];
    int  lmPort;
    char whisperHost[32];
    int  whisperPort;
    char ttsHost[32];
    int  ttsPort;
    char modelName[32];
    int  ttsSpeakerId;
    char systemPrompt[256];
    char weatherCity[32];
    int  bootMode;        // 0=normal, 1=camera
    int  faceType;        // 0=StackChan, 1=ESPer-Chan
    int  displayType;     // DISPLAY_SSD1306/DISPLAY_ST7789_240X320/DISPLAY_ST7735S_128X128/DISPLAY_ST7789_240X240
    int  faceThreshold;   // 顔認識率しきい値(%)。これを超えると物理ディスプレイの顔が笑顔になる
    int  musicAutoPlay;   // 0=何もしない, 1=曲リスト順, 2=ランダム, 3=アーティスト順
};

extern AppSettings appSettings;
extern bool apModeActive;

void loadSettings();
void saveSettings();
bool isSettingsConfigured();  // NVSに設定が保存済みかチェック（初回起動判定）
void startAPMode();
void handleWebServerClient();
String generateConfigPage();

#endif

