#include "settings.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>

AppSettings appSettings;
bool apModeActive = false;

String htmlEscape(const String& raw) {
    String out = "";
    for (size_t i = 0; i < raw.length(); i++) {
        char c = raw.charAt(i);
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == 0x22) out += "&quot;";
        else if (c == 0x27) out += "&#39;";
        else out += c;
    }
    return out;
}

void loadSettings() {
    Preferences prefs;
    prefs.begin("twentychan", true);
    strlcpy(appSettings.wifiSsid,     prefs.getString("ssid",     WIFI_SSID).c_str(),     sizeof(appSettings.wifiSsid));
    strlcpy(appSettings.wifiPassword, prefs.getString("pass",     WIFI_PASSWORD).c_str(), sizeof(appSettings.wifiPassword));
    strlcpy(appSettings.lmHost,       prefs.getString("lmhost",   LMSTUDIO_HOST).c_str(), sizeof(appSettings.lmHost));
    appSettings.lmPort       = prefs.getInt("lmport",   LMSTUDIO_PORT);
    strlcpy(appSettings.whisperHost,  prefs.getString("whost",    WHISPER_HOST).c_str(),  sizeof(appSettings.whisperHost));
    appSettings.whisperPort  = prefs.getInt("wport",    WHISPER_PORT);
    strlcpy(appSettings.ttsHost,      prefs.getString("ttshost",  TTS_HOST).c_str(),      sizeof(appSettings.ttsHost));
    appSettings.ttsPort      = prefs.getInt("ttsport",  TTS_PORT);
    strlcpy(appSettings.modelName,    prefs.getString("model",    MODEL_NAME).c_str(),    sizeof(appSettings.modelName));
    appSettings.ttsSpeakerId = prefs.getInt("speaker",  TTS_SPEAKER_ID);
    strlcpy(appSettings.systemPrompt, prefs.getString("prompt",   SYSTEM_PROMPT).c_str(), sizeof(appSettings.systemPrompt));
    strlcpy(appSettings.weatherCity,  prefs.getString("wcity",    WEATHER_CITY).c_str(),  sizeof(appSettings.weatherCity));
    appSettings.bootMode     = prefs.getInt("boot",     BOOT_MODE_NORMAL);
    appSettings.faceType     = prefs.getInt("facetype", 1);           // デフォルト: ESPer-Chan
    appSettings.displayType  = prefs.getInt("disptype", DISPLAY_TYPE); // デフォルト: コンパイル時設定
    appSettings.faceThreshold = prefs.getInt("facethr", DEFAULT_FACE_THRESHOLD);
    prefs.end();
}

void saveSettings() {
    Preferences prefs;
    prefs.begin("twentychan", false);
    prefs.putString("ssid",     appSettings.wifiSsid);
    prefs.putString("pass",     appSettings.wifiPassword);
    prefs.putString("lmhost",   appSettings.lmHost);
    prefs.putInt("lmport",     appSettings.lmPort);
    prefs.putString("whost",    appSettings.whisperHost);
    prefs.putInt("wport",      appSettings.whisperPort);
    prefs.putString("ttshost",  appSettings.ttsHost);
    prefs.putInt("ttsport",    appSettings.ttsPort);
    prefs.putString("model",    appSettings.modelName);
    prefs.putInt("speaker",    appSettings.ttsSpeakerId);
    prefs.putString("prompt",   appSettings.systemPrompt);
    prefs.putString("wcity",    appSettings.weatherCity);
    prefs.putInt("boot",       appSettings.bootMode);
    prefs.putInt("facetype",   appSettings.faceType);
    prefs.putInt("disptype",   appSettings.displayType);
    prefs.putInt("facethr",    appSettings.faceThreshold);
    prefs.putBool("setup",     true);   // 設定保存済みフラグ（初回起動判定用）
    prefs.end();
}

bool isSettingsConfigured() {
    Preferences prefs;
    prefs.begin("twentychan", true);
    bool configured = prefs.getBool("setup", false);
    prefs.end();
    return configured;
}

String generateConfigPage() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Web Config</title>";
    html += "<style>";
    html += ".avatar { text-align: center; margin: 10px 0; }";
    html += ".avatar img { width: 80px; height: auto; border-radius: 6px; opacity: 0.85; }";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5;color:#333;}";
    html += "h1{text-align:center;color:#555;}";
    html += "form{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1);}";
    html += "label{display:block;margin:12px 0 4px;font-weight:bold;font-size:14px;}";
    html += "input[type='text'],input[type='password'],input[type='number'],textarea,select{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;font-size:14px;}";
    html += "input[type='submit']{width:100%;padding:12px;background:#4CAF50;color:#fff;border:none;border-radius:4px;font-size:16px;cursor:pointer;margin-top:16px;}";
    html += "input[type='submit']:hover{background:#45a049;}";
    html += ".note{font-size:12px;color:#666;margin-top:8px;text-align:center;}";
    html += "</style></head><body>";
    html += "<div class='avatar'><img src='data:image/gif;base64,R0lGODlhQAHwAIcAAAAAAAEBAQICAgMDAwQEBAUFBQYGBgcHBwgICAkJCQoKCgsLCwwMDA0NDQ4ODg8PDxAQEBERERISEhMTExQUFBUVFRYWFhcXFxgYGBkZGRoaGhsbGxwcHB0dHR4eHh8fHyAgICEhISIiIiMjIyQkJCUlJSYmJicnJygoKCkpKSoqKisrKywsLC0tLS4uLi8vLzAwMDExMTIyMjMzMzQ0NDU1NTY2Njc3Nzg4ODk5OTo6Ojs7Ozw8PD09PT4+Pj8/P0BAQEFBQUJCQkNDQ0REREVFRUZGRkdHR0hISElJSUpKSktLS0xMTE1NTU5OTk9PT1BQUFFRUVJSUlNTU1RUVFVVVVZWVldXV1hYWFlZWVpaWltbW1xcXF1dXV5eXl9fX2BgYGFhYWJiYmNjY2RkZGVlZWZmZmdnZ2hoaGlpaWpqamtra2xsbG1tbW5ubm9vb3BwcHFxcXJycnNzc3R0dHV1dXZ2dnd3d3h4eHl5eXp6ent7e3x8fH19fX5+fn9/f4CAgIGBgYKCgoODg4SEhIWFhYaGhoeHh4iIiImJiYqKiouLi4yMjI2NjY6Ojo+Pj5CQkJGRkZKSkpOTk5SUlJWVlZaWlpeXl5iYmJmZmZqampubm5ycnJ2dnZ6enp+fn6CgoKGhoaKioqOjo6SkpKWlpaampqenp6ioqKmpqaqqqqurq6ysrK2tra6urq+vr7CwsLGxsbKysrOzs7S0tLW1tba2tre3t7i4uLm5ubq6uru7u7y8vL29vb6+vr+/v8DAwMHBwcLCwsPDw8TExMXFxcbGxsfHx8jIyMnJycrKysvLy8zMzM3Nzc7Ozs/Pz9DQ0NHR0dLS0tPT09TU1NXV1dbW1tfX19jY2NnZ2dra2tvb29zc3N3d3d7e3t/f3+Dg4OHh4eLi4uPj4+Tk5OXl5ebm5ufn5+jo6Onp6erq6uvr6+zs7O3t7e7u7u/v7/Dw8PHx8fLy8vPz8/T09PX19fb29vf39/j4+Pn5+fr6+vv7+/z8/P39/f7+/v///yH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAQAHwAAAI/wABCBxIsKDBgwgTKlzIsKHDhxAjSpxIsaLFixgzatzIsaPHjyBDihxJsqTJkyhTqlzJsqXLlzBjypxJs6bNmzhz6tzJs6fPn0CDCh1KtKjRo0iTKl3KtKnTp1CjSp1KtarVq1izat3KtavXr2DDih1LtqzZs2jTql3Ltq3bt3Djyp1Lt67du3jz6t3Lt6/fv4ADCx5MuLDhw4gTK17MuLHjx5AjS55MubLly5gza97MubPnz6BDix5NurTp06hTq17NurXr17Bjy55Nu7bt27hz697Nu7fv38C9/ns8vPW/442PF1etHLni5stPQ3d+eHp00tapE85+fTT36tlRf/83PN50+e3hpacvfB77evTWxb8/2N5t/YL3k88XyH163Pv9QSfZeQHupxaBBfoH2XcJ9vcWgw0amFiEFGq3VoUULohhgw9umKCGHgbYYYgOEkdiiW2diKJ+KsZnX4suMgZjfmXNKCF7Nip4YY4CLsZjjGj9qCNiQvaYVpHNPYdkkkEuqdyETlpIVpRSDkZld2NdSeSVWILFZZd/fXmWmORxOaaZZWppFpk4qlkjmm1Seaab8Mm5Jpx1RjmnnXHqeSedVuKZpaCCsTkloYEZOiighSLqpaNhQiqcpH0pKpaliVLKFaaAcfqVp36B2pWofJG6lal6oZqVqnixepWrdsH/WpWsdNE6la1y4RqVrnDx+pSvLzIaFrApanqqsakiu6qyrTL7qrOxQjurtLVSe6u1uWK7q7a9cvurt8HyeaiwnYLbFLFsobuUugeayxS7R7q7rrztkvspvfHaO6m+leKLFLz5OhnwklD6uSfBWxr8p8AFI7kjwg0X+bDDPlI88Y8sClmsxBnnOCKPINr4H8iReZztjJShXG2LlrEc7YmYwdxsiJtt2G+Fn0WY6Yfe3Riqz8EFLfTQRBdt9NFIJ6300kw37fTTUEct9dRUV2311VhnrfXWXHft9ddghy322GSXbfbZaKet9tpst+3223DHLffcdNdt9914562311+qXbj3QH3L/HfgJP7NH+EeGq744ow37vjjkEcu+eSUV2755ZhnrvnmnHfu+eeghy766KSXbvrpqKeu+uqst+7667DHLvvstNdu++2456777rz37vvvwAcv/PDEFz9VQAAh+QQACgAAACw6ADoAwwCBAAAI/wABCBxIsKDBgwgTKlzIsKHDhxAjSpyY8B/FixgzatyI0CJHjP9CfhxJsuTHkB5NOkQpUqXLlzAJskwZ0+DMljVz6rx4k6bOnjh3Ch1aEShRoD6JKj1qdCjSpVCVPnXaNKrV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' alt='ESPer-Chan'></div>";
    html += "<h1>ESPer-Chan Settings</h1>";
    html += "<form action='/save' method='POST'>";
    html += "<label>WiFi SSID</label><input type='text' name='ssid' value='" + htmlEscape(String(appSettings.wifiSsid)) + "'>";
    html += "<label>WiFi Password</label><input type='password' name='pass' value='" + htmlEscape(String(appSettings.wifiPassword)) + "'>";
    html += "<label>LM Studio Host</label><input type='text' name='lmhost' value='" + htmlEscape(String(appSettings.lmHost)) + "'>";
    html += "<label>LM Studio Port</label><input type='number' name='lmport' value='" + String(appSettings.lmPort) + "'>";
    html += "<label>Whisper.cpp Host</label><input type='text' name='whost' value='" + htmlEscape(String(appSettings.whisperHost)) + "'>";
    html += "<label>Whisper.cpp Port</label><input type='number' name='wport' value='" + String(appSettings.whisperPort) + "'>";
    html += "<label>TTS (VOICEVOX) Host</label><input type='text' name='ttshost' value='" + htmlEscape(String(appSettings.ttsHost)) + "'>";
    html += "<label>TTS Port</label><input type='number' name='ttsport' value='" + String(appSettings.ttsPort) + "'>";
    html += "<label>Model Name</label><input type='text' name='model' value='" + htmlEscape(String(appSettings.modelName)) + "'>";
    html += "<label>TTS Speaker ID</label><input type='number' name='speaker' value='" + String(appSettings.ttsSpeakerId) + "'>";
    html += "<label>System Prompt</label><textarea name='prompt' rows='3'>" + htmlEscape(String(appSettings.systemPrompt)) + "</textarea>";
    html += "<label>天気地域 (例: 東京, 北見)</label><input type='text' name='wcity' value='" + htmlEscape(String(appSettings.weatherCity)) + "'>";
    html += "<label>顔タイプ</label><select name='facetype'>";
    html += String("<option value='0'") + (appSettings.faceType == 0 ? " selected" : "") + ">StackChan</option>";
    html += String("<option value='1'") + (appSettings.faceType == 1 ? " selected" : "") + ">ESPer-Chan</option>";
    html += "</select>";
    html += "<label>ディスプレイタイプ</label><select name='disptype'>";
    html += String("<option value='1'") + (appSettings.displayType == 1 ? " selected" : "") + ">SSD1306 128x64 OLED (I2C)</option>";
    html += String("<option value='2'") + (appSettings.displayType == 2 ? " selected" : "") + ">ST7789 240x320 TFT (SPI)</option>";
    html += String("<option value='3'") + (appSettings.displayType == 3 ? " selected" : "") + ">ST7789 240x240 TFT (SPI)</option>";
    html += String("<option value='4'") + (appSettings.displayType == 4 ? " selected" : "") + ">ST7735S 128x128 TFT (SPI)</option>";
    html += "</select>";
    html += "<label>顔認識率しきい値 (%)</label><input type='number' name='facethr' min='0' max='100' value='" + String(appSettings.faceThreshold) + "'>";
    html += "<input type='submit' value='保存して再起動'>";
    html += "<p class='note'>保存後、ESP32が再起動しWiFiクライアントモードに戻ります</p>";
    html += "</form></body></html>";
    return html;
}

void startAPMode() {
    apModeActive = true;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    IPAddress apIP(192, 168, 20, 1);
    IPAddress gateway(192, 168, 20, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
    WiFi.softAP("ESPer-Chan-Setup", "setup1234");
    IPAddress ip = WiFi.softAPIP();
    // LOG_I("AP Mode started");
    LOG_I("[AP] AP IP: %s", ip.toString().c_str());

    webServer.on("/", HTTP_GET, []() {
        webServer.send(200, "text/html; charset=utf-8", generateConfigPage());
    });

    webServer.on("/save", HTTP_POST, []() {
        strlcpy(appSettings.wifiSsid,     webServer.arg("ssid").c_str(),     sizeof(appSettings.wifiSsid));
        strlcpy(appSettings.wifiPassword, webServer.arg("pass").c_str(),     sizeof(appSettings.wifiPassword));
        strlcpy(appSettings.lmHost,       webServer.arg("lmhost").c_str(),   sizeof(appSettings.lmHost));
        appSettings.lmPort       = webServer.arg("lmport").toInt();
        strlcpy(appSettings.whisperHost,  webServer.arg("whost").c_str(),    sizeof(appSettings.whisperHost));
        appSettings.whisperPort  = webServer.arg("wport").toInt();
        strlcpy(appSettings.ttsHost,      webServer.arg("ttshost").c_str(),  sizeof(appSettings.ttsHost));
        appSettings.ttsPort      = webServer.arg("ttsport").toInt();
        strlcpy(appSettings.modelName,    webServer.arg("model").c_str(),    sizeof(appSettings.modelName));
        appSettings.ttsSpeakerId = webServer.arg("speaker").toInt();
        strlcpy(appSettings.systemPrompt, webServer.arg("prompt").c_str(),   sizeof(appSettings.systemPrompt));
        strlcpy(appSettings.weatherCity,  webServer.arg("wcity").c_str(),    sizeof(appSettings.weatherCity));
        if (webServer.hasArg("facetype"))  appSettings.faceType    = webServer.arg("facetype").toInt();
        if (webServer.hasArg("disptype"))  appSettings.displayType = webServer.arg("disptype").toInt();
        if (webServer.hasArg("facethr"))   appSettings.faceThreshold = webServer.arg("facethr").toInt();

        saveSettings();

        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta http-equiv='refresh' content='5;url=/'>";
        html += "</head><body><h1>保存しました</h1><p>5秒後に再起動します...</p></body></html>";
        webServer.send(200, "text/html; charset=utf-8", html);
        webServer.client().stop();
        delay(500);
        ESP.restart();
    });

    webServer.begin();
}
