// AI Agent ESPer-Chan
// ESP32S3 x LM Studio x whisper.cpp x VOICEVOX

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "settings.h"
#include <WebServer.h>

#if USE_WEB_AVATAR
#include "Web_Avatar_server.h"
#endif
#if USE_SD
#include "music_player.h"
#endif

#if USE_CUSTOM_FACE
#include "CustomFace.h"
CustomFace customFace;
#endif
#if USE_AVATAR
#include <M5Unified.h>
#include <Avatar.h>
#include "display_driver.h"
using namespace m5avatar;
static m5avatar::FixedFace* _fixedFacePtr = nullptr;  // ランタイム顔タイプ選択用 (StackChan選択時)
Avatar avatar;
volatile bool g_isLipSyncing = false;
volatile float g_lipSyncLevel = 0.0f;
TaskHandle_t lipSyncTaskHandle = NULL;
#endif
#if WAKEWORD_ENABLED
TaskHandle_t wakeWordTaskHandle = NULL;
#endif

// シリアル (USER_SERIAL は config.h で定義)
// USE_USB_CDC=1 のときのみ USBSerial インスタンスを宣言する。
// USE_USB_CDC=0 では宣言しないことでフレームワークの CDC 初期化と干渉しない。
#if USE_USB_CDC
USBCDC USBSerial;
#endif
#define DBG_SERIAL Serial

// グローバル
I2SClass I2S_Mic, I2S_Dac;
bool micInitialized = false, dacInitialized = false;
int16_t* voiceAudioBuffer = nullptr;
SystemMode systemMode = MODE_AGENT;
const int VOICE_MAX_SAMPLES = MIC_SAMPLE_RATE * 10;

struct ChatMessage { String role, content; };
ChatMessage chatHistory[MAX_MESSAGE_HISTORY];
int historyIndex = 0, historyCount = 0;
bool wifiConnected = false;
unsigned long lastWifiCheck = 0;
WebServer webServer(80);
String inputBuffer = "";
bool inputReady = false;

enum VoiceState { VOICE_IDLE, VOICE_LISTENING, VOICE_PROCESSING, VOICE_SPEAKING };
VoiceState voiceState = VOICE_IDLE;
unsigned long lastSpeakEndTime = 0;
const unsigned long POST_SPEAK_COOLDOWN_MS = 1200;

int wifiRetryCount = 0;
const int MAX_WIFI_RETRIES = 3;
bool wifiRetryExhausted = false, wifiFirstConnect = true;

// ============================================
// ヘルパー
// ============================================
#define SAFE_ALLOC(type, size) (ESP.getPsramSize() > 0 ? (type*)ps_malloc(size) : (type*)malloc(size))
#define SAFE_FREE(ptr) if(ptr){ free(ptr); ptr=nullptr; }

// JsonDocument用PSRAMアロケータ。
// デフォルトのDynamicJsonDocument/JsonDocumentは内部SRAMヒープ(malloc)を使うため、
// 音声会話を繰り返すたびにLLMリクエストのJSONバッファ(4KB前後)が内部ヒープを
// 確保・解放してフラグメンテーションを進行させ、最終的に確保失敗→リセットに至る。
// PSRAMはサイズが大きくこの種の出入りに強いため、ここへ逃がす。
class PsramAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void* ptr) override { heap_caps_free(ptr); }
    void* reallocate(void* ptr, size_t new_size) override { return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM); }
    static PsramAllocator* instance() {
        static PsramAllocator allocator;
        return &allocator;
    }
};

// 感情検出: 0=neutral 1=happy 2=sad 3=angry 4=panic 5=surprised
int detectEmotionIndex(const String& text) {
    const char** lists[] = {EMOTION_HAPPY_WORDS, EMOTION_SAD_WORDS, EMOTION_ANGRY_WORDS, EMOTION_PANIC_WORDS, EMOTION_SURPRISED_WORDS};
    for (int e = 0; e < 5; e++) {
        for (int i = 0; i < MAX_EMOTION_WORDS; i++) {
            if (lists[e][i] && strlen(lists[e][i]) > 0 && text.indexOf(lists[e][i]) >= 0) return e + 1;
        }
    }
    return 0;
}
#if USE_CUSTOM_FACE
CustomExpression detectEmotion(const String& text) {
    static const CustomExpression exprs[] = {EXPR_NEUTRAL, EXPR_HAPPY, EXPR_SAD, EXPR_ANGRY, EXPR_PANIC, EXPR_SURPRISED};
    return exprs[detectEmotionIndex(text)];
}
#endif

#if USE_LED
void setLED(uint8_t b) { ledcWrite(LED_PIN, b); }
void blinkLED(int n, int d) {
    for (int i = 0; i < n; i++) { setLED(LED_BRIGHTNESS); delay(d); setLED(0); delay(d); }
}
#else
void setLED(uint8_t b) {}
void blinkLED(int n, int d) {}
#endif

void setupLED() {
#if USE_LED
    pinMode(LED_PIN, OUTPUT); ledcAttach(LED_PIN, 5000, 8); setLED(0);
#endif
}

void setupUSBSerial() {
#if USE_USB_CDC
    USB.begin(); USBSerial.begin();  // CDC On Boot=Disabled 時に必要。タクトスイッチ構成でのみ呼ぶ
#endif
    delay(1000);
}

// ============================================
// WiFi
// ============================================
bool connectWiFi() {
    if (wifiFirstConnect) { USER_SERIAL.print(F("[INF] WiFi: ")); USER_SERIAL.println(appSettings.wifiSsid); LOG_I("WiFi connecting"); }
    USER_SERIAL.flush();
    WiFi.mode(WIFI_STA); WiFi.disconnect(true); delay(100);
    WiFi.begin(appSettings.wifiSsid, appSettings.wifiPassword);
    unsigned long start = millis(); int dots = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT_MS) { USER_SERIAL.println(); return false; }
        delay(500); USER_SERIAL.print("."); dots++;
        if (dots >= 30) { USER_SERIAL.println(); dots = 0; }
        setLED((millis() / 250) % 2 ? LED_BRIGHTNESS : 0); yield();
    }
    USER_SERIAL.println();
    LOG_I("WiFi OK IP:%s", WiFi.localIP().toString().c_str());
    setLED(LED_BRIGHTNESS); wifiConnected = true;
    configTime(9 * 3600, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");  // JST = UTC+9
    return true;
}

void checkWiFiConnection() {
    if (wifiRetryExhausted || millis() - lastWifiCheck < WIFI_RECONNECT_INTERVAL_MS) return;
    lastWifiCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
#if USE_WEB_AVATAR
        initGifServer();
#endif
        return;
    }
    if (wifiRetryCount >= MAX_WIFI_RETRIES) {
        wifiRetryExhausted = true; wifiConnected = false; setLED(0);
        LOG_E("WiFi permanently failed. Offline mode."); return;
    }
    wifiRetryCount++; wifiFirstConnect = false;
    USER_SERIAL.printf("[ERR] WiFi reconnect %d/%d\n", wifiRetryCount, MAX_WIFI_RETRIES);
    wifiConnected = false; setLED(0); connectWiFi();
}

// ============================================
// 履歴
// ============================================
void addToHistory(const char* role, const char* content) {
    chatHistory[historyIndex].role = role; chatHistory[historyIndex].content = content;
    historyIndex = (historyIndex + 1) % MAX_MESSAGE_HISTORY;
    if (historyCount < MAX_MESSAGE_HISTORY) historyCount++;
}
void clearHistory() {
    historyIndex = historyCount = 0;
    for (int i = 0; i < MAX_MESSAGE_HISTORY; i++) { chatHistory[i].role = ""; chatHistory[i].content = ""; }
}

// ============================================
// LM Studio
// ============================================
String buildChatPayload(const char* userMessage) {
    JsonDocument doc(PsramAllocator::instance());
    doc["model"] = appSettings.modelName; doc["max_tokens"] = MAX_TOKENS;
    doc["temperature"] = TEMPERATURE; doc["top_p"] = TOP_P; doc["stream"] = false;
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject sys = messages.createNestedObject(); sys["role"] = "system"; sys["content"] = appSettings.systemPrompt;
    int start = (historyCount < MAX_MESSAGE_HISTORY) ? 0 : historyIndex;
    for (int i = 0; i < historyCount; i++) {
        int idx = (start + i) % MAX_MESSAGE_HISTORY;
        if (chatHistory[idx].role.length() > 0) {
            JsonObject h = messages.createNestedObject(); h["role"] = chatHistory[idx].role; h["content"] = chatHistory[idx].content;
        }
    }
    JsonObject u = messages.createNestedObject(); u["role"] = "user"; u["content"] = userMessage;
    String payload; serializeJson(doc, payload); return payload;
}

String sendChatRequest(const char* userMessage) {
    if (!wifiConnected) return "[ERROR: WiFi未接続]";
    String url = "http://" + String(appSettings.lmHost) + ":" + String(appSettings.lmPort) + String(LMSTUDIO_PATH);
    LOG_I("LM API:%s", url.c_str());
    HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(LMSTUDIO_API_KEY));
    String payload = buildChatPayload(userMessage);
    setLED(LED_BRIGHTNESS / 2);
    int code = http.POST(payload); webServer.handleClient(); setLED(LED_BRIGHTNESS);
    LOG_I("LM HTTP:%d", code);
    if (code != HTTP_CODE_OK) { http.end(); return "[ERROR: LM HTTP " + String(code) + "]"; }
    String resp = http.getString(); http.end(); webServer.handleClient();
    JsonDocument doc(PsramAllocator::instance());
    DeserializationError err = deserializeJson(doc, resp);
    if (err) return "[ERROR: JSON parse]";
    if (doc.containsKey("error")) return "[ERROR: " + String(doc["error"]["message"]|"Unknown") + "]";
    const char* content = doc["choices"][0]["message"]["content"] | "";
    if (strlen(content) == 0) return "[ERROR: Empty response]";
    addToHistory("user", userMessage); addToHistory("assistant", content);
    return String(content);
}

// ============================================
// 音声入出力
// ============================================
bool setupMicrophone() {
#if MIC_TYPE == MIC_TYPE_I2S
    LOG_I("Init INMP441 I2S mic (WS=%d SCK=%d SD=%d)...", MIC_WS_PIN, MIC_SCK_PIN, MIC_SD_PIN);
    I2S_Mic.setPins(MIC_SCK_PIN, MIC_WS_PIN, -1, MIC_SD_PIN, -1);
    micInitialized = I2S_Mic.begin(I2S_MODE_STD, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
#else
    LOG_I("Init PDM mic (CLK=%d DATA=%d)...", MIC_CLK_PIN, MIC_DATA_PIN);
    I2S_Mic.setPinsPdmRx(MIC_CLK_PIN, MIC_DATA_PIN);
    micInitialized = I2S_Mic.begin(I2S_MODE_PDM_RX, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
#endif
    LOG_I(micInitialized ? "Mic OK" : "Mic FAIL"); return micInitialized;
}

// INMP441(32bit I2S)とPDM(16bit)を統一的に読み取るヘルパー
// INMP441は32bitフレームで上位16bitが有効音声データ
static inline bool micReadSample(int16_t* out) {
    if (!I2S_Mic.available()) return false;
#if MIC_TYPE == MIC_TYPE_I2S
    *out = (int16_t)((int32_t)I2S_Mic.read() >> 16);
#else
    *out = (int16_t)I2S_Mic.read();
#endif
    return true;
}

bool setupSpeaker() {
    LOG_I("Init I2S DAC...");
    I2S_Dac.end();
    delay(10);
    I2S_Dac.setPins(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    dacInitialized = I2S_Dac.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    LOG_I(dacInitialized ? "DAC OK" : "DAC FAIL"); 
    return dacInitialized;
}

void generateWavHeader(uint8_t* h, uint32_t dataSize, uint32_t sr, uint16_t bps, uint16_t ch) {
    uint32_t br = sr * ch * bps / 8, ba = ch * bps / 8, fs = dataSize + 36;
    memcpy(h, "RIFF", 4); h[4]=fs;h[5]=fs>>8;h[6]=fs>>16;h[7]=fs>>24;
    memcpy(h+8, "WAVE", 4); memcpy(h+12, "fmt ", 4);
    h[16]=16;h[17]=h[18]=h[19]=0; h[20]=1;h[21]=0; h[22]=ch;h[23]=0;
    h[24]=sr;h[25]=sr>>8;h[26]=sr>>16;h[27]=sr>>24;
    h[28]=br;h[29]=br>>8;h[30]=br>>16;h[31]=br>>24;
    h[32]=ba;h[33]=0; h[34]=bps;h[35]=0;
    memcpy(h+36, "data", 4); h[40]=dataSize;h[41]=dataSize>>8;h[42]=dataSize>>16;h[43]=dataSize>>24;
}

// ---- VAD録音 ----
int recordWithVAD(int16_t* buffer, size_t maxSamples) {
    if (!micInitialized) { LOG_E("Mic not init"); return 0; }
    LOG_I("Wait voice..."); voiceState = VOICE_LISTENING;
    int samplesRead = 0; unsigned long lastWeb = millis(), silenceStart = 0, speechStart = 0, waitStart = millis();
    bool speechDetected = false;
    unsigned long vadIgnore = millis() + 200; int echoSamples = 0;

    while (!speechDetected) {
        if (millis() - waitStart > 10000) { LOG_I("No voice"); voiceState = VOICE_IDLE; return 0; }
        int16_t v;
        if (micReadSample(&v)) {
            int32_t a = (int32_t)v * MIC_GAIN; a = constrain(a, -32768, 32767);
            if (samplesRead < (int)maxSamples) {
                buffer[samplesRead++] = (int16_t)a;
                if (millis() < vadIgnore) echoSamples++;
            }
            if (millis() >= vadIgnore && abs(v) > VAD_THRESHOLD) {
                speechDetected = true; speechStart = silenceStart = millis();
                LOG_I("Speech detected:%d", abs(v)); setLED(LED_BRIGHTNESS); break;
            }
        }
        delay(1); yield();
        if (millis() - lastWeb >= 50) { lastWeb = millis(); webServer.handleClient(); }
    }

    while (samplesRead < (int)maxSamples) {
        int16_t v;
        if (micReadSample(&v)) {
            int32_t a = (int32_t)v * MIC_GAIN; a = constrain(a, -32768, 32767);
            buffer[samplesRead++] = (int16_t)a;
            if (abs(v) > VAD_THRESHOLD) silenceStart = millis();
        }
        if (speechDetected && millis() - silenceStart > (unsigned long)VAD_SILENCE_MS && millis() - speechStart > (unsigned long)VAD_MIN_SPEECH_MS) { LOG_I("Silence stop"); break; }
        if (millis() - speechStart > 30000) break;
        yield();
        if (millis() - lastWeb >= 50) { lastWeb = millis(); webServer.handleClient(); }
    }
    if (echoSamples > 0 && samplesRead > echoSamples) {
        memmove(buffer, buffer + echoSamples, (samplesRead - echoSamples) * sizeof(int16_t));
        samplesRead -= echoSamples;
        LOG_I("Cut %d echo samples", echoSamples);
    }
    voiceState = VOICE_IDLE; setLED(0);
    LOG_I("Recorded %d samples (%.1fs)", samplesRead, (float)samplesRead / MIC_SAMPLE_RATE);
    return samplesRead;
}

int recordAudio(int16_t* buffer, size_t maxSamples, int durationMs) {
    if (!micInitialized) { LOG_E("Mic not init"); return 0; }
    LOG_I("Rec %dms...", durationMs); voiceState = VOICE_LISTENING; setLED(LED_BRIGHTNESS);
    int samplesRead = 0; unsigned long start = millis();
    while (millis() - start < (unsigned long)durationMs && samplesRead < (int)maxSamples) {
        int16_t v;
        if (micReadSample(&v)) {
            int32_t a = (int32_t)v * MIC_GAIN; a = constrain(a, -32768, 32767);
            buffer[samplesRead++] = (int16_t)a;
        }
        yield(); webServer.handleClient();
    }
    voiceState = VOICE_IDLE; setLED(0); LOG_I("Rec %d samples", samplesRead); return samplesRead;
}

// ============================================
// Whisper STT (最適化版)
// ============================================

// 無音エッジをトリムしてアクティブ音声区間を取得
static void trimSilenceEdges(const int16_t* buf, int total, int* startOut, int* countOut) {
    const int   BLOCK  = 160;                      // 10ms @ 16kHz
    const float THRESH = WAKEWORD_MIN_ENERGY * 0.5f;
    int first = 0, last = total;
    for (int i = 0; i + BLOCK <= total; i += BLOCK) {
        float e = 0;
        for (int j = 0; j < BLOCK; j++) { float s = buf[i+j] / 32768.0f; e += s * s; }
        if (e / BLOCK >= THRESH) { first = (i > BLOCK) ? i - BLOCK : 0; break; }
    }
    for (int i = total - BLOCK; i >= BLOCK; i -= BLOCK) {
        float e = 0;
        for (int j = 0; j < BLOCK; j++) { float s = buf[i+j] / 32768.0f; e += s * s; }
        if (e / BLOCK >= THRESH) { last = min(total, i + 2 * BLOCK); break; }
    }
    int cnt = last - first;
    if (cnt < BLOCK * 8) { *startOut = 0; *countOut = total; return; } // 80ms未満なら全体使用
    *startOut = first; *countOut = cnt;
}

// 持続TCP接続 (HTTP keep-alive でハンドシェイクを省略)
static WiFiClient _wc;
static HTTPClient _wh;

static const char* WHISPER_BND = "----ESP32WB";

String sendAudioToWhisper(int16_t* audioBuffer, int sampleCount, bool updateFace = true) {
    if (!wifiConnected) return "[ERROR: WiFi未接続]";

    // ---- 無音トリム ----
    int trimStart, trimCount;
    trimSilenceEdges(audioBuffer, sampleCount, &trimStart, &trimCount);
    const int16_t* pcm = audioBuffer + trimStart;

    // ---- audio_ctx: エンコーダを実際の音声長に限定 (最大の高速化ポイント) ----
    // 公式: (秒 / 30秒) * 1500フレーム + 128(余裕) → デフォルト1500から大幅削減
    float    audioSec = (float)trimCount / MIC_SAMPLE_RATE;
    int      audioCtx = (int)((audioSec / 30.0f) * 1500.0f) + 128;
    if (audioCtx < 64)   audioCtx = 64;
    if (audioCtx > 1500) audioCtx = 1500;
    char     ctxStr[6]; itoa(audioCtx, ctxStr, 10);

    uint32_t dataSize = (uint32_t)trimCount * 2;
    uint32_t wavSize  = dataSize + 44;

    // ---- マルチパートボディ (1回のアロケーションで構築) ----
    String head = "--"; head += WHISPER_BND;
    head += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\nContent-Type: audio/wav\r\n\r\n";

    // 追加フィールド: サーバー側の無駄な処理をすべてスキップ
    String tail;
    tail.reserve(380);
    tail  = "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nja";
    tail += "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext";
    tail += "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"no_timestamps\"\r\n\r\ntrue";
    tail += "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"temperature\"\r\n\r\n0";
    tail += "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"best_of\"\r\n\r\n1";
    tail += "\r\n--"; tail += WHISPER_BND; tail += "\r\nContent-Disposition: form-data; name=\"audio_ctx\"\r\n\r\n"; tail += ctxStr;
    tail += "\r\n--"; tail += WHISPER_BND; tail += "--\r\n";

    size_t headLen = head.length(), tailLen = tail.length();
    size_t total   = headLen + (size_t)wavSize + tailLen;

    uint8_t* buf = SAFE_ALLOC(uint8_t, total);
    if (!buf) return "[ERROR: mem]";

    // WAVヘッダ + PCM をそのまま最終位置に書き込み (中間バッファなし)
    memcpy(buf, head.c_str(), headLen);
    generateWavHeader(buf + headLen, dataSize, MIC_SAMPLE_RATE, 16, 1);
    memcpy(buf + headLen + 44, pcm, dataSize);
    memcpy(buf + headLen + wavSize, tail.c_str(), tailLen);

    // ---- HTTP POST (静的WiFiClientで接続再利用 + 切断時リトライ) ----
    String url = "http://" + String(appSettings.whisperHost) + ":" + String(appSettings.whisperPort) + WHISPER_PATH;

    voiceState = VOICE_PROCESSING; setLED(LED_BRIGHTNESS / 3);
#if USE_WEB_AVATAR
    if (updateFace) setGifFaceIndex(1);
#endif

    // whisper.cpp (httplib) はデフォルト5秒無通信でkeep-alive接続を切る。
    // ウェイクワード間隔はそれより長いため、POSTが失敗 (code<=0) したら
    // 接続をリセットして1回だけ再試行する。
    int code = -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) { _wh.end(); _wc.stop(); } // 再接続
        _wh.setReuse(true);
        _wh.setTimeout(15000);
        _wh.begin(_wc, url);
        _wh.addHeader("Content-Type", String("multipart/form-data; boundary=") + WHISPER_BND);
        code = _wh.POST(buf, (int)total);
        if (code > 0) break; // 接続成功
    }

    SAFE_FREE(buf);
    webServer.handleClient();
    setLED(0); voiceState = VOICE_IDLE;

    LOG_I("Whisper HTTP:%d ctx:%d %.1fs", code, audioCtx, audioSec);
    if (code != HTTP_CODE_OK) {
        _wh.end(); _wc.stop();
        return "[ERROR: Whisper HTTP " + String(code) + "]";
    }

    // response_format=text → プレーンテキスト直接取得 (JSON解析不要)
    String resp = _wh.getString();
    // end() 不要 — setReuse(true) が接続を維持する
    resp.trim();
    if (resp.length() == 0) return "[ERROR: Empty STT]";
    // 旧サーバー互換: JSON が返ってきた場合のフォールバック
    if (resp.startsWith("{")) {
        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, resp)) resp = doc["text"].as<String>();
        resp.trim();
    }
    LOG_I("STT:%s", resp.c_str());
    return resp;
}

// ============================================
// TTS
// ============================================
String urlEncode(const String& str) {
    String enc = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (c == ' ') enc += "%20";
        else if (c == '!') enc += "%21";
        else if (c == '"') enc += "%22";
        else if (c == '#') enc += "%23";
        else if (c == '$') enc += "%24";
        else if (c == '%') enc += "%25";
        else if (c == '&') enc += "%26";
        else if (c == '\'') enc += "%27";
        else if (c == '(') enc += "%28";
        else if (c == ')') enc += "%29";
        else if (c == '*') enc += "%2A";
        else if (c == '+') enc += "%2B";
        else if (c == ',') enc += "%2C";
        else if (c == '-') enc += "%2D";
        else if (c == '.') enc += "%2E";
        else if (c == '/') enc += "%2F";
        else if (c == ':') enc += "%3A";
        else if (c == ';') enc += "%3B";
        else if (c == '<') enc += "%3C";
        else if (c == '=') enc += "%3D";
        else if (c == '>') enc += "%3E";
        else if (c == '?') enc += "%3F";
        else if (c == '@') enc += "%40";
        else if (c == '[') enc += "%5B";
        else if (c == '\\') enc += "%5C";
        else if (c == ']') enc += "%5D";
        else if (c == '^') enc += "%5E";
        else if (c == '_') enc += "%5F";
        else if (c == '`') enc += "%60";
        else if (c == '{') enc += "%7B";
        else if (c == '|') enc += "%7C";
        else if (c == '}') enc += "%7D";
        else if (c == '~') enc += "%7E";
        else if ((uint8_t)c >= 0x80 || (uint8_t)c < 0x20 || (uint8_t)c == 0x7F) {
            char hex[4]; snprintf(hex, sizeof(hex), "%02X", (uint8_t)c); enc += "%"; enc += hex;
        } else enc += c;
    }
    return enc;
}

String preprocessForTTS(const String& raw) {
    String p = raw; p.replace("\n", " "); p.replace("\r", " ");
    String r = "";
    for (size_t i = 0; i < p.length(); ) {
        uint8_t c = p.charAt(i);
        if (c < 0x80) { r += (char)c; i++; }
        else if ((c & 0xE0) == 0xC0) { r += p.substring(i, i + 2); i += 2; }
        else if ((c & 0xF0) == 0xE0) {
            if (c == 0xE2 && i + 2 < p.length()) {
                uint8_t c2 = p.charAt(i + 1);
                if (c2 == 0x98 || c2 == 0x9C || c2 == 0x9D || c2 == 0x9E || c2 == 0xA0 || c2 == 0xA1 || c2 == 0xA2 || c2 == 0xA3 ||
                    c2 == 0xAD || c2 == 0xB0 || c2 == 0xB1 || c2 == 0xB2 || c2 == 0xB3) { i += 3; continue; }
            }
            r += p.substring(i, i + 3); i += 3;
        }
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i++;
    }
    const int MAX_TTS = 500;
    if (r.length() > MAX_TTS) {
        int cut = MAX_TTS;
        while (cut > 0 && ((uint8_t)r.charAt(cut) & 0xC0) == 0x80) cut--;
        r = r.substring(0, cut) + "...";
    }
    r.trim(); return r;
}

void speakBeep();

void speakWithVoicevox(const String& text) {
    if (!wifiConnected || !dacInitialized) { LOG_E("WiFi/DAC not ready"); return; }
    String tts = preprocessForTTS(text);
    if (tts.length() == 0) { LOG_E("TTS empty"); speakBeep(); return; }
    voiceState = VOICE_SPEAKING; setLED(LED_BRIGHTNESS / 2);
#if USE_WEB_AVATAR
    setGifFaceIndex(5);
#endif

    String enc = urlEncode(tts);
    String qUrl = "http://" + String(appSettings.ttsHost) + ":" + String(appSettings.ttsPort) + "/audio_query?text=" + enc + "&speaker=" + String(appSettings.ttsSpeakerId);
    HTTPClient q; q.setTimeout(5000); q.begin(qUrl);
    int qCode = q.POST("");
    if (qCode != HTTP_CODE_OK) { q.end(); voiceState = VOICE_IDLE; setLED(0); LOG_E("TTS query:%d", qCode); return; }
    String qJson = q.getString(); q.end();

    String sUrl = "http://" + String(appSettings.ttsHost) + ":" + String(appSettings.ttsPort) + "/synthesis?speaker=" + String(appSettings.ttsSpeakerId);
    HTTPClient s; s.setTimeout(30000); s.begin(sUrl);
    s.addHeader("Content-Type", "application/json"); s.addHeader("Accept", "audio/wav");
    int sCode = s.POST(qJson);
    if (sCode != HTTP_CODE_OK) { s.end(); voiceState = VOICE_IDLE; setLED(0); LOG_E("TTS synth:%d", sCode); return; }

    WiFiClient* stream = s.getStreamPtr();
    const size_t CHUNK = 512;
    uint8_t* buf = SAFE_ALLOC(uint8_t, CHUNK);
    if (!buf) { s.end(); voiceState = VOICE_IDLE; setLED(0); return; }

    uint32_t skipped = 0; unsigned long ss = millis();
    while (skipped < 44) {
        if (stream->available() > 0) {
            int toRead = min((int)(44 - skipped), (int)CHUNK);
            int rd = stream->read(buf, toRead);
            if (rd > 0) skipped += rd;
        }
        if (millis() - ss > 5000) break; yield();
    }
    uint32_t skipA = 0; const uint32_t SKIP_A = 4800; ss = millis();
    while (skipA < SKIP_A) {
        if (stream->available() > 0) {
            int toRead = min((int)(SKIP_A - skipA), (int)CHUNK);
            int rd = stream->read(buf, toRead);
            if (rd > 0) skipA += rd;
        }
        if (millis() - ss > 5000) break; yield();
    }

    int16_t zeroBuf[256] = {0};
    for (int i = 0; i < 4; i++) I2S_Dac.write((uint8_t*)zeroBuf, sizeof(zeroBuf));

#if USE_CUSTOM_FACE
    CustomExpression ttsEmo = detectEmotion(tts);
    customFace.setCustomExpression(ttsEmo);
#if USE_WEB_AVATAR
    switch(ttsEmo) {
        case EXPR_HAPPY: setGifFaceIndex(5); break;
        case EXPR_SAD: setGifFaceIndex(6); break;
        case EXPR_ANGRY: setGifFaceIndex(7); break;
        case EXPR_PANIC: setGifFaceIndex(8); break;
        case EXPR_SURPRISED: setGifFaceIndex(9); break;
        default: setGifFaceIndex(5); break;
    }
#endif
#elif USE_AVATAR
    {
        // デフォルト顔: 感情ワード検出 → Expression + リップシンクGIF切り替え
        static const Expression EMO_EXPR[] = {
            Expression::Neutral,  // 0: neutral
            Expression::Happy,    // 1: happy
            Expression::Sad,      // 2: sad
            Expression::Angry,    // 3: angry
            Expression::Neutral,  // 4: panic (Neutralで代用)
            Expression::Neutral   // 5: surprised (発話中は不使用)
        };
        static const int EMO_GIF[] = {5, 10, 6, 7, 8, 5};
        int emoIdx = detectEmotionIndex(tts);
        avatar.setExpression(EMO_EXPR[emoIdx]);
        LOG_I("Emo:%d", emoIdx);
#if USE_WEB_AVATAR
        setGifFaceIndex(EMO_GIF[emoIdx]);
#endif
    }
#endif

    unsigned long start = millis(); size_t total = 0; int fadeIn = 0; const int FADE_IN_DUR = 9600;
    while (stream->available() > 0 || stream->connected()) {
        int rd = stream->read(buf, CHUNK);
        if (rd > 0) {
            int16_t* samples = (int16_t*)buf; int sc = rd / 2;
            for (int i = 0; i < sc; i++) {
                float g = TTS_VOLUME_GAIN;
                if (fadeIn < FADE_IN_DUR) { g *= (float)fadeIn / FADE_IN_DUR; fadeIn++; }
                int32_t v = (int32_t)(samples[i] * g);
                samples[i] = (int16_t)constrain(v, -32768, 32767);
            }
#if USE_AVATAR
            int32_t sum = 0; for (int i = 0; i < sc; i++) sum += abs(samples[i]);
            float lvl = sc > 0 ? (float)(sum / sc) / 32767.0f : 0;
            g_lipSyncLevel = constrain(lvl, 0.0f, 1.0f);
#endif
            size_t w = I2S_Dac.write(buf, rd);
            if (w != (size_t)rd) LOG_E("I2S write incomplete");
            total += w;
        }
        if (millis() - start > 30000) { LOG_E("TTS timeout"); break; }
        yield(); webServer.handleClient();
    }
    I2S_Dac.write((uint8_t*)zeroBuf, sizeof(zeroBuf));
#if USE_AVATAR
    g_lipSyncLevel = 0.0f;
#if USE_CUSTOM_FACE
    customFace.setMouthOpenRatio(0.0f);
#else
    avatar.setMouthOpenRatio(0.0f);
#endif
#endif
    SAFE_FREE(buf); s.end();
    voiceState = VOICE_IDLE; setLED(0);
#if USE_CUSTOM_FACE
    customFace.setCustomExpression(EXPR_NEUTRAL); customFace.setMouthOpenRatio(0.0f);
#elif USE_AVATAR
    avatar.setExpression(Expression::Neutral);
#endif
#if USE_WEB_AVATAR
    setGifFaceIndex(0);
#endif
    lastSpeakEndTime = millis();
}

void speakResponse(const String& text) {
    if (text.length() == 0) return;
#if USE_AVATAR
    startLipSync(text);
#endif
    speakWithVoicevox(text);
#if USE_AVATAR
    stopLipSync();
#endif
}

// ============================================
// 前方宣言
// ============================================
#if USE_AVATAR
void setAgentExpression(Expression expr);
#if USE_CUSTOM_FACE
void setAgentExpression(Expression expr, CustomExpression customExpr);
#endif
#endif
int detectWeatherKeyword(const String& text);
void speakWeather(int dayIndex);
bool detectTimeCommand(const String& text);
void speakTime();
#if USE_SD
int detectMusicCommand(const String& text);
#endif
int detectCameraCommand(const String& text);

// ============================================
// Avatar / リップシンク
// ============================================
#if USE_AVATAR
void startLipSync(const String& text) {
    g_isLipSyncing = true; setAgentExpression(Expression::Neutral);
}
void stopLipSync() {
    g_isLipSyncing = false; g_lipSyncLevel = 0.0f;
    avatar.setMouthOpenRatio(0.0f); setAgentExpression(Expression::Neutral);
}
void lipSyncTask(void *pvParameters) {
    while (true) {
        if (voiceState == VOICE_PROCESSING) { vTaskDelay(50 / portTICK_PERIOD_MS); continue; }
        if (g_isLipSyncing) {
            float lvl = (g_lipSyncLevel > 0.0f) ? g_lipSyncLevel : (sin(millis() / 150.0f) + 1.0f) / 2.0f * 0.7f + 0.3f;
            if (random(100) < 15) lvl = 1.0f;
            g_lipSyncLevel = lvl;
#if USE_CUSTOM_FACE
            customFace.setMouthOpenRatio(lvl);
#else
            avatar.setMouthOpenRatio(lvl);
#endif
            vTaskDelay(LIPSYNC_SPEED_MS / portTICK_PERIOD_MS);
        } else {
#if USE_CUSTOM_FACE
            customFace.setMouthOpenRatio(0.0f);
#else
            avatar.setMouthOpenRatio(0.0f);
#endif
            if (g_lipSyncLevel > 0.0f) g_lipSyncLevel = 0.0f;
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
}
#endif

// ============================================
// ピッ音（統合版）
// ============================================
void speakPip(int count = 1, int gapMs = 0) {
    if (!dacInitialized) return;
    const int freq = 1200, dur = 50;
    const int sc = I2S_SAMPLE_RATE * dur / 1000;
    const int gapSc = I2S_SAMPLE_RATE * gapMs / 1000;
    const int totalSc = sc * count + gapSc * (count - 1);

    int16_t* buf = SAFE_ALLOC(int16_t, totalSc * sizeof(int16_t));
    if (!buf) return;
    memset(buf, 0, totalSc * sizeof(int16_t));
    for (int p = 0; p < count; p++) {
        int off = p * (sc + gapSc);
        for (int i = 0; i < sc; i++) {
            float t = (float)i / I2S_SAMPLE_RATE;
            float fi = (i < 15) ? (float)i / 15.0f : 1.0f;
            float fo = (i > sc - 15) ? (float)(sc - i) / 15.0f : 1.0f;
            buf[off + i] = (int16_t)(sin(2 * PI * freq * t) * 6000 * min(fi, fo));
        }
    }
    const size_t CHUNK = 512;
    size_t totalBytes = totalSc * sizeof(int16_t), written = 0;
    while (written < totalBytes) {
        size_t toW = min((size_t)CHUNK, totalBytes - written);
        size_t w = I2S_Dac.write((uint8_t*)buf + written, toW);
        written += w; if (w == 0) delay(1);
    }
    delay(50);
    SAFE_FREE(buf);
    int16_t z[64] = {0}; I2S_Dac.write((uint8_t*)z, sizeof(z));
}

void speakPipPip() { speakPip(2, 50); }

void speakBeep() {
    if (!dacInitialized) { LOG_E("DAC not init"); return; }
    LOG_I("Beep..."); voiceState = VOICE_SPEAKING; setLED(LED_BRIGHTNESS / 2);
#if USE_WEB_AVATAR
    setGifFaceIndex(5);
#endif
#if USE_AVATAR
    startLipSync("beep");
#endif
    const int freq = 1000, dur = 200;
    const int sc = I2S_SAMPLE_RATE * dur / 1000;
    int16_t* buf = SAFE_ALLOC(int16_t, sc * sizeof(int16_t));
    if (buf) {
        for (int i = 0; i < sc; i++) {
            float t = (float)i / I2S_SAMPLE_RATE;
            float fi = (i < 100) ? (float)i / 100.0f : 1.0f;
            float fo = (i > sc - 100) ? (float)(sc - i) / 100.0f : 1.0f;
            buf[i] = (int16_t)(sin(2 * PI * freq * t) * 8000 * min(fi, fo));
        }
        I2S_Dac.write((uint8_t*)buf, sc * sizeof(int16_t)); SAFE_FREE(buf);
    }
    int16_t z[128] = {0}; I2S_Dac.write((uint8_t*)z, sizeof(z));
#if USE_AVATAR
    stopLipSync();
#endif
    lastSpeakEndTime = millis(); voiceState = VOICE_IDLE; setLED(0);
#if USE_WEB_AVATAR
    setGifFaceIndex(0);
#endif
    LOG_I("Beep done");
}

// ============================================
// LLM ストリーミング＋文単位TTS
// ============================================

// 日本語文末（。！？）またはASCII改行で文境界を判定
static bool isSentenceBoundary(const String& s) {
    int len = s.length();
    if (len < 6) return false;
    uint8_t a=(uint8_t)s[len-3], b=(uint8_t)s[len-2], c=(uint8_t)s[len-1];
    if (a==0xE3 && b==0x80 && c==0x82) return true;  // 。
    if (a==0xEF && b==0xBC && c==0x81) return true;  // ！
    if (a==0xEF && b==0xBC && c==0x9F) return true;  // ？
    if (s[len-1]=='\n' || s[len-1]=='!') return true;
    return false;
}

// LLM SSEストリームを受信しながら文単位でTTSを呼ぶ。
// 第一文が生成された時点で発話開始するため、LLM全体完了を待たずに応答できる。
void sendChatRequestStreaming(const char* userMessage) {
    if (!wifiConnected) { LOG_E("No WiFi(stream)"); return; }

    JsonDocument doc(PsramAllocator::instance());
    doc["model"] = appSettings.modelName; doc["max_tokens"] = MAX_TOKENS;
    doc["temperature"] = TEMPERATURE; doc["top_p"] = TOP_P; doc["stream"] = true;
    JsonArray messages = doc.createNestedArray("messages");
    JsonObject sys = messages.createNestedObject(); sys["role"]="system"; sys["content"]=appSettings.systemPrompt;
    int hStart = (historyCount < MAX_MESSAGE_HISTORY) ? 0 : historyIndex;
    for (int i = 0; i < historyCount; i++) {
        int idx = (hStart + i) % MAX_MESSAGE_HISTORY;
        if (chatHistory[idx].role.length() > 0) {
            JsonObject h = messages.createNestedObject(); h["role"]=chatHistory[idx].role; h["content"]=chatHistory[idx].content;
        }
    }
    JsonObject u = messages.createNestedObject(); u["role"]="user"; u["content"]=userMessage;
    String payload; serializeJson(doc, payload);

    String url = "http://" + String(appSettings.lmHost) + ":" + String(appSettings.lmPort) + String(LMSTUDIO_PATH);
    HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(LMSTUDIO_API_KEY));

    setLED(LED_BRIGHTNESS / 2);
    int code = http.POST(payload);
    if (code != HTTP_CODE_OK) { http.end(); setLED(0); LOG_E("LLM stream:%d", code); return; }

    WiFiClient* stream = http.getStreamPtr();
    stream->setTimeout(8000);  // LLMが次のトークンを出すまで最大8秒待機

#if USE_AVATAR
    startLipSync("x");
#endif

    String accumulated = "";
    String fullResponse = "";

    while (stream->connected() || stream->available()) {
        if (!stream->available()) { delay(2); webServer.handleClient(); continue; }

        String line = stream->readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || !line.startsWith(F("data: "))) continue;

        String data = line.substring(6);
        if (data == F("[DONE]")) break;

        StaticJsonDocument<512> chunk;
        if (deserializeJson(chunk, data)) continue;
        const char* delta = chunk["choices"][0]["delta"]["content"] | "";
        if (strlen(delta) == 0) continue;

        accumulated += delta;
        fullResponse += delta;
        // トークン逐次印字はしない。文単位でまとめて印字する（絵文字混入防止）

        if (isSentenceBoundary(accumulated)) {
            USER_SERIAL.print(accumulated); USER_SERIAL.println();
            speakWithVoicevox(accumulated);
            accumulated = "";
        } else if (accumulated.length() >= 120) {
            // 120文字超えたら読点(、)を探して分割
            int splitAt = -1;
            for (int i = (int)accumulated.length() - 3; i >= 9; i -= 3) {
                uint8_t a=(uint8_t)accumulated[i], b=(uint8_t)accumulated[i+1], c=(uint8_t)accumulated[i+2];
                if (a==0xE3 && b==0x80 && c==0x81) { splitAt = i + 3; break; }  // 、
            }
            if (splitAt > 0) {
                String sentence = accumulated.substring(0, splitAt);
                USER_SERIAL.print(sentence); USER_SERIAL.println();
                speakWithVoicevox(sentence);
                accumulated = accumulated.substring(splitAt);
            } else if (accumulated.length() >= 180) {
                USER_SERIAL.print(accumulated); USER_SERIAL.println();
                speakWithVoicevox(accumulated); accumulated = "";
            }
        }
    }

    // ストリーム終了後、バッファに残ったテキストを発話
    // preprocessForTTSで空になる（絵文字・記号のみ等）場合は印字もスキップ
    accumulated.trim();
    if (accumulated.length() > 0) {
        String ttsCheck = preprocessForTTS(accumulated); ttsCheck.trim();
        if (ttsCheck.length() > 0) {
            USER_SERIAL.print(accumulated); USER_SERIAL.println();
            speakWithVoicevox(accumulated);
        }
    }

#if USE_AVATAR
    stopLipSync();
#endif

    http.end();

    // 会話履歴に追加
    fullResponse.trim();
    if (fullResponse.length() > 0) {
        addToHistory("user", userMessage);
        addToHistory("assistant", fullResponse.c_str());
    }
}

// ============================================
// 音声入力フィードバック
// ============================================
void startVoiceConversationFeedback() {
    voiceState = VOICE_PROCESSING;
    blinkLED(2, 100); speakPipPip();
#if USE_AVATAR
#if USE_CUSTOM_FACE
    setAgentExpression(Expression::Neutral, EXPR_SURPRISED);
#else
    avatar.setExpression(Expression::Neutral);
    avatar.setMouthOpenRatio(1.0f);
#endif
#endif
#if USE_WEB_AVATAR
    setGifFaceIndex(9);
#endif
    // PipPip音は約200msで終わる。口開け(驚き)顔を確実に視認できるよう600ms維持する
    for (int i = 0; i < 12; i++) { delay(50); webServer.handleClient(); }
}

// ============================================
// 音声会話ループ
// ============================================
void voiceConversationLoop() {
    if (!wifiConnected) { LOG_E("No WiFi"); speakBeep(); return; }
    startVoiceConversationFeedback();
#if USE_AVATAR
    setAgentExpression(Expression::Neutral);
#if !USE_CUSTOM_FACE
    avatar.setMouthOpenRatio(0.0f);
#endif
#endif
#if USE_WEB_AVATAR
    setGifFaceIndex(0);
#endif
    if (!voiceAudioBuffer) { LOG_E("No audio buf"); USER_SERIAL.println(F("⚠️ mem")); return; }
    USER_SERIAL.println(); LOG_I("Speak...");
    unsigned long tRec = millis();
    int samples = recordWithVAD(voiceAudioBuffer, VOICE_MAX_SAMPLES);
    LOG_I("[TIME] record: %lums", millis() - tRec);
    if (samples > 0) {
        LOG_I("STT...");
#if USE_AVATAR
#if USE_CUSTOM_FACE
        setAgentExpression(Expression::Happy, EXPR_HAPPY);
#else
        setAgentExpression(Expression::Happy);
#endif
#endif
#if USE_WEB_AVATAR
        setGifFaceIndex(1);
#endif
        unsigned long tStt = millis();
        String txt = sendAudioToWhisper(voiceAudioBuffer, samples);
        LOG_I("[TIME] STT: %lums", millis() - tStt);
        for (int i = 0; i < 3; i++) { webServer.handleClient(); delay(10); }
        bool handled = false;
        if (txt.length() > 0 && !txt.startsWith("[ERROR")) {
            txt.trim();
            USER_SERIAL.print(F("📝 ")); USER_SERIAL.println(txt);
            bool noise = (txt == "(ポッ)" || txt == "(シャッ)" || txt == "(ハッ)" || txt == "(フッ)" || txt == "(ッ)" ||
                          txt == "(シュー)" || txt == "(シー)" || txt == "(ブツ)" || txt == "(シャッシュ)" ||
                          (txt.startsWith("(") && txt.endsWith(")")));
            if (noise) {
                LOG_E("Noise"); speakBeep();
#if USE_AVATAR
                setAgentExpression(Expression::Neutral);
#endif
#if USE_WEB_AVATAR
                setGifFaceIndex(0);
#endif
                USER_SERIAL.println(F("👤 あなた: ")); return;
            }
            int w = detectWeatherKeyword(txt);
            if (w >= 0) { LOG_I("Weather!"); speakWeather(w); handled = true; }
            if (!handled && detectTimeCommand(txt)) { LOG_I("Time!"); speakTime(); handled = true; }
#if USE_SD
            int m = detectMusicCommand(txt);
            if (m >= 0 && !handled) { LOG_I("Music!"); speakResponse("ミュージックプレーヤーを起動します"); systemMode = MODE_MUSIC; startMusicMode(); handled = true; }
#endif
#if USE_CAMERA
            int c = detectCameraCommand(txt);
            if (c >= 0 && !handled) {
                LOG_I("Camera!"); speakResponse("ウェブカメラモードを起動します");
                startCameraMode();
                handled = true;
            }
#endif
            if (!handled) {
#if USE_VOICE_OUTPUT
                // ストリーミング: LLM第一文生成時点から即発話
#if USE_WEB_AVATAR
                setGifFaceIndex(1);
#endif
                USER_SERIAL.println(F("========================================"));
                USER_SERIAL.print(F("🤖 ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.print(F(": "));
                unsigned long tLlm = millis();
                sendChatRequestStreaming(txt.c_str());
                LOG_I("[TIME] LLM+TTS stream: %lums", millis() - tLlm);
                setLED(0);
                USER_SERIAL.println(F("========================================"));
#if USE_AVATAR
                setAgentExpression(Expression::Neutral);
#endif
#else
                // TTS無効時は従来の非ストリーミング（テキスト表示のみ）
                LOG_I("LLM...");
                unsigned long tLlm = millis();
                String resp = sendChatRequest(txt.c_str());
                LOG_I("[TIME] LLM: %lums", millis() - tLlm);
                setLED(0);
                USER_SERIAL.println(F("========================================"));
                USER_SERIAL.print(F("🤖 ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.print(F(": ")); USER_SERIAL.println(resp);
                USER_SERIAL.println(F("========================================"));
#if USE_AVATAR
                setAgentExpression(Expression::Neutral);
#endif
#endif
            }
        } else {
            USER_SERIAL.println(F("⚠️ STT failed")); USER_SERIAL.println(txt);
#if USE_AVATAR
            setAgentExpression(Expression::Neutral);
#endif
#if USE_WEB_AVATAR
            setGifFaceIndex(0);
#endif
        }
    } else { LOG_E("No voice"); }
    USER_SERIAL.println(); USER_SERIAL.println(F("👤 あなた: "));
}

// ============================================
// ウェイクワード
// ============================================
#if WAKEWORD_ENABLED
float calculatePeak(int16_t* samples, uint32_t count) {
    if (count == 0) return 0;
    float maxP = 0;
    for (uint32_t i = 0; i < count; i++) { float a = abs((float)samples[i] / 32768.0f); if (a > maxP) maxP = a; }
    return maxP;
}
bool detectWakeWordNeeNee(int16_t* buffer, uint32_t sampleCount) {
    // 25msフレーム: 50msより細かく区切り、素早い発音の音節間ディップを捕捉する
    const uint32_t MS_PER_FRAME = 25;
    uint32_t frameSize = MIC_SAMPLE_RATE * MS_PER_FRAME / 1000;
    uint32_t numFrames = sampleCount / frameSize;
    if (numFrames < 8) return false;
    float peaks[60] = {0}; uint32_t fi = 0;
    for (uint32_t f = 0; f < numFrames && fi < 60; f++) peaks[fi++] = calculatePeak(&buffer[f * frameSize], frameSize);
    float minP = 1.0f;
    for (uint32_t i = 0; i < fi; i++) if (peaks[i] < minP) minP = peaks[i];
    float thr = max(minP + 0.016f, WAKEWORD_MIN_ENERGY);
    bool isV[60] = {false};
    for (uint32_t i = 0; i < fi; i++) isV[i] = (peaks[i] > thr) && (peaks[i] < WAKEWORD_MAX_ENERGY);
    struct VB { uint32_t s, e; float avg; } blocks[5] = {0};
    uint32_t bc = 0; bool inV = false; uint32_t vStart = 0; float vSum = 0; uint32_t vFrames = 0;
    for (uint32_t i = 0; i < fi && bc < 5; i++) {
        if (isV[i] && !inV) { inV = true; vStart = i; vSum = peaks[i]; vFrames = 1; }
        else if (isV[i] && inV) { vSum += peaks[i]; vFrames++; }
        else if (!isV[i] && inV) {
            inV = false; blocks[bc].s = vStart; blocks[bc].e = i - 1; blocks[bc].avg = vSum / vFrames; bc++;
        }
    }
    if (inV && bc < 5) { blocks[bc].s = vStart; blocks[bc].e = fi - 1; blocks[bc].avg = vSum / vFrames; bc++; }

    // ── パターン1: 2ブロック分離検出（「ねぇ　ねぇ」ゆっくり発音） ──
    if (bc >= 2) {
        uint32_t d1  = (blocks[0].e - blocks[0].s + 1) * MS_PER_FRAME;
        uint32_t d2  = (blocks[1].e - blocks[1].s + 1) * MS_PER_FRAME;
        uint32_t gap = (blocks[1].s - blocks[0].e - 1) * MS_PER_FRAME;
        if (d1 >= 100 && d1 <= 550 && d2 >= 100 && d2 <= 550 && gap >= 50 && gap <= 400 &&
            blocks[0].avg > 0.012f && blocks[1].avg > 0.012f &&
            abs(blocks[0].avg - blocks[1].avg) < 0.06f) return true;
    }

    // ── パターン2: 単一ブロック内2ピーク検出（「ねぇねぇ」素早く発音） ──
    // 鼻音 /n/ でエネルギーが谷になるが閾値以下に落ちない場合に対応する
    if (bc == 1) {
        uint32_t bLen = blocks[0].e - blocks[0].s + 1;
        if (bLen >= 4 && bLen <= 22) {  // 100〜550ms
            uint32_t mid = blocks[0].s + bLen / 2;
            float p1 = 0, p2 = 0, dip = 1.0f;
            for (uint32_t i = blocks[0].s; i < mid; i++) if (peaks[i] > p1) p1 = peaks[i];
            for (uint32_t i = mid; i <= blocks[0].e; i++) if (peaks[i] > p2) p2 = peaks[i];
            // 中間付近（±1フレーム）の最小値を谷として取得
            uint32_t ds = (mid > blocks[0].s) ? mid - 1 : blocks[0].s;
            uint32_t de = (mid + 1 <= blocks[0].e) ? mid + 1 : blocks[0].e;
            for (uint32_t i = ds; i <= de; i++) if (peaks[i] < dip) dip = peaks[i];
            float minPeak = (p1 < p2) ? p1 : p2;
            if (p1 > 0.012f && p2 > 0.012f &&
                abs(p1 - p2) < 0.06f &&
                dip < minPeak * 0.70f) return true;
        }
    }
    return false;
}
void wakeWordTask(void *pvParameters) {
    const unsigned long BOOT_CD = 5000;
    const unsigned long taskStart = millis();
    uint32_t wakeSc = MIC_SAMPLE_RATE * WAKEWORD_RECORD_MS / 1000;
    uint32_t preSc = MIC_SAMPLE_RATE * 200 / 1000;
    int16_t* buf = SAFE_ALLOC(int16_t, (wakeSc + preSc) * sizeof(int16_t));
    if (!buf) { vTaskDelete(NULL); return; }
    uint32_t idx = 0;
    // DC-ブロックフィルタ状態: y[n] = x[n] - x[n-1] + R*y[n-1], R=0.9995
    // PDMマイクのDCドリフトと低周波振動ノイズを除去する（fc ≈ 1.3Hz）
    float dc_x = 0.0f, dc_y = 0.0f;
    while (true) {
        if (millis() - taskStart < BOOT_CD || millis() - lastSpeakEndTime < POST_SPEAK_COOLDOWN_MS ||
            voiceState != VOICE_IDLE || systemMode != MODE_AGENT || (wifiRetryExhausted && !wifiConnected)) {
            vTaskDelay(100 / portTICK_PERIOD_MS); idx = 0; dc_x = 0.0f; dc_y = 0.0f; continue;
        }
        uint32_t toRead = MIC_SAMPLE_RATE / 10, read = 0;
        for (uint32_t i = 0; i < toRead; i++) {
            int16_t v;
            if (micReadSample(&v)) {
                float xf = (float)v;
                float yf = xf - dc_x + 0.9995f * dc_y;
                dc_x = xf; dc_y = yf;
                int32_t a = (int32_t)(yf * MIC_GAIN); a = constrain(a, -32768, 32767);
                if (idx < wakeSc + preSc) buf[idx++] = (int16_t)a;
                read++;
            }
        }
        if (idx >= wakeSc + preSc) {
            memmove(buf, &buf[toRead], (wakeSc + preSc - toRead) * sizeof(int16_t));
            idx = wakeSc + preSc - toRead;
        }
        if (idx >= wakeSc && voiceState == VOICE_IDLE && detectWakeWordNeeNee(&buf[idx - wakeSc], wakeSc)) {
            // Stage2: Whisper STT で発話内容を確認し誤検知を排除する（顔変化なし）
            String stt = sendAudioToWhisper(&buf[idx - wakeSc], wakeSc, false);
            String sttNorm = stt; sttNorm.replace("、", ""); sttNorm.replace("。", ""); sttNorm.replace("!", ""); sttNorm.replace("！", "");
            bool confirmed = (sttNorm.indexOf("ねね") >= 0 ||
                              sttNorm.indexOf("ねぇねぇ") >= 0 ||
                              sttNorm.indexOf("ねえねえ") >= 0);
            if (confirmed) {
                USER_SERIAL.println(); USER_SERIAL.println(F("✨ ウェイクワードを検出しました"));
                voiceConversationLoop();
            } else {
                USER_SERIAL.print(F("📝 ")); USER_SERIAL.println(stt);
                USER_SERIAL.println(F("（ウェイクワードと違うみたい...）"));
#if USE_WEB_AVATAR
                setGifFaceIndex(0);
#endif
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }
            idx = 0; dc_x = 0.0f; dc_y = 0.0f;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    SAFE_FREE(buf);
}
void listenForWakeWord() {}
#endif

// ============================================
// 天気
// ============================================
const char* getWeatherCityId(const char* name) {
    for (size_t i = 0; i < WEATHER_CITY_COUNT; i++) if (strcmp(WEATHER_CITIES[i].name, name) == 0) return WEATHER_CITIES[i].id;
    return nullptr;
}
String normalizeWeatherKeyword(const String& raw) {
    String s = raw; s.replace(" ", ""); s.replace("　", ""); s.replace("。", ""); s.replace("、", "");
    s.replace("？", ""); s.replace("?", ""); s.replace("！", ""); s.replace("!", ""); s.trim(); return s;
}
int detectWeatherKeyword(const String& text) {
    String n = normalizeWeatherKeyword(text);
    for (int day = 0; day < 3; day++) for (int pat = 0; pat < MAX_WEATHER_PATTERNS; pat++)
        if (WEATHER_KEYWORDS[day][pat] && n == WEATHER_KEYWORDS[day][pat]) return day;
    return -1;
}
String fetchWeather(int dayIndex) {
    if (!wifiConnected) return "[ERROR: WiFi未接続]";
    const char* cityName = appSettings.weatherCity;
    const char* cityId = getWeatherCityId(cityName);
    if (!cityId) return "[ERROR: 未対応地域:" + String(cityName) + "]";
    String url = "https://" + String(WEATHER_API_HOST) + String(WEATHER_API_PATH) + "?city=" + String(cityId);
    LOG_I("Weather:%s", url.c_str());
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(15000); http.begin(client, url);
    http.addHeader("User-Agent", "ESP32S3/ESPer-Chan");
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return "[ERROR: Weather HTTP " + String(code) + "]"; }
    String resp = http.getString(); http.end(); webServer.handleClient();
    DynamicJsonDocument* doc = (DynamicJsonDocument*)SAFE_ALLOC(uint8_t, sizeof(DynamicJsonDocument));
    if (!doc) return "[ERROR: mem]";
    new(doc) DynamicJsonDocument(8192);
    if (deserializeJson(*doc, resp)) { delete doc; return "[ERROR: Weather JSON]"; }
    JsonArray forecasts = (*doc)["forecasts"];
    if (!forecasts || forecasts.size() <= (size_t)dayIndex) { delete doc; return "[ERROR: No data]"; }
    JsonObject fc = forecasts[dayIndex];
    const char* date = fc["date"] | "", *dateLabel = fc["dateLabel"] | "", *telop = fc["telop"] | "";
    String dateStr = String(date);
    int y = 0, m = 0, d = 0; sscanf(date, "%d-%d-%d", &y, &m, &d);
    if (y > 0 && m > 0 && d > 0) dateStr = String(y) + "年" + String(m) + "月" + String(d) + "日";
    bool hasMax = false, hasMin = false;
    String maxC, minC;
    JsonObject temp = fc["temperature"];
    if (temp) {
        if (temp.containsKey("max") && !temp["max"].isNull() && temp["max"].containsKey("celsius")) {
            const char* v = temp["max"]["celsius"]; if (v && strlen(v) > 0) { hasMax = true; maxC = v; }
        }
        if (temp.containsKey("min") && !temp["min"].isNull() && temp["min"].containsKey("celsius")) {
            const char* v = temp["min"]["celsius"]; if (v && strlen(v) > 0) { hasMin = true; minC = v; }
        }
    }
    String dw = "", di = "", da = "";
    JsonObject detail = fc["detail"];
    if (detail) { dw = detail["weather"] | ""; di = detail["wind"] | ""; da = detail["wave"] | ""; }
    String text = dateStr + " " + String(dateLabel) + "の" + cityName + "の天気は" + telop + "です。";
    if (hasMax && hasMin) text += " 最高気温は" + maxC + "度、最低気温は" + minC + "度です。";
    if (dw.length() > 0) text += " " + dw;
    if (di.length() > 0) text += " " + di;
    if (da.length() > 0) text += " " + da;
    delete doc; return text;
}
void speakWeather(int dayIndex) {
    LOG_I("Weather:%s", appSettings.weatherCity);
    String w = fetchWeather(dayIndex);
    USER_SERIAL.println(w); speakResponse(w);
}

// ============================================
// 現在時刻 (ESP32 NTP / configTime)
// ============================================
String fetchCurrentTime() {
    struct tm timeinfo;
    // NTP 同期済みか確認（最大2秒待機）
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 4) { delay(500); retry++; }
    if (!getLocalTime(&timeinfo)) return "[ERROR: NTP未同期]";
    char buf[64];
    snprintf(buf, sizeof(buf), "現在の時刻は%d時%d分%d秒です。",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
}

bool detectTimeCommand(const String& text) {
    String t = text; t.trim();
    for (int i = 0; i < MAX_TIME_KEYWORDS; i++)
        if (TIME_KEYWORDS[i] && t == TIME_KEYWORDS[i]) return true;
    return false;
}

void speakTime() {
    LOG_I("Time...");
    String t = fetchCurrentTime();
    USER_SERIAL.println(t);
    speakResponse(t);
}

int detectMusicCommand(const String& text) {
    String t = text; t.trim();
    for (int i = 0; i < MAX_MUSIC_KEYWORDS; i++) if (MUSIC_KEYWORDS[i] && t == MUSIC_KEYWORDS[i]) return 0;
    return -1;
}
int detectCameraCommand(const String& text) {
    String t = text; t.trim();
    for (int i = 0; i < MAX_CAMERA_KEYWORDS; i++) if (CAMERA_KEYWORDS[i] && t == CAMERA_KEYWORDS[i]) return 0;
    return -1;
}

// ============================================
// シリアル入力
// ============================================
void processSerialInput() {
    while (USER_SERIAL.available() > 0) {
        char c = USER_SERIAL.read();
        LOG_D("Serial:0x%02X", (uint8_t)c);
        if (c == '\n' || c == '\r') { if (inputBuffer.length() > 0) { inputReady = true; return; } }
        else if (c == '\b' || c == 0x7F) { if (inputBuffer.length() > 0) { inputBuffer.remove(inputBuffer.length() - 1); USER_SERIAL.print("\b \b"); } }
        else if (inputBuffer.length() < MESSAGE_MAX_LENGTH) {
            inputBuffer += c;
            if ((uint8_t)c >= 0x20 && (uint8_t)c != 0x7F) USER_SERIAL.write((uint8_t)c);
        }
    }
}
String getInputBuffer() { String r = inputBuffer; inputBuffer = ""; inputReady = false; return r; }

// ============================================
// コマンド処理（集約版）
// ============================================
bool processCommand(const String& input) {
    String cmd = input; cmd.trim(); cmd.toLowerCase();

#if USE_SD
    if (cmd == "/music") {
        USER_SERIAL.println(F("========================================\n  Music Player start\n  /exit to return\n========================================"));
        systemMode = MODE_MUSIC; startMusicMode(); return true;
    }
#else
    if (cmd == "/music") { LOG_E("Music mode disabled (USE_SD=0)"); return true; }
#endif
    if (cmd == "/cam" || cmd == "/camera") {
#if USE_CAMERA
        USER_SERIAL.println(F("========================================\n  Camera mode start\n========================================")); startCameraMode();
#else
        LOG_E("Camera disabled");
#endif
        return true;
    }
    if (cmd == "/help" || cmd == "help" || cmd == "?") {
        USER_SERIAL.println();
        USER_SERIAL.print(F("=== ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.print(F(" ")); USER_SERIAL.print(PROJECT_VERSION); USER_SERIAL.println(F(" Commands ==="));
        if (wifiConnected) {
            USER_SERIAL.println(F("--- 会話 ---"));
            USER_SERIAL.println(F("/voice              音声会話（マイク→LLM→読み上げ）"));
            USER_SERIAL.println(F("/say <text>         テキストを読み上げ"));
            USER_SERIAL.println(F("--- モード ---"));
            USER_SERIAL.println(F("/music              ミュージックプレーヤーモードを起動"));
            USER_SERIAL.println(F("/camera  /cam       ウェブカメラモードを起動"));
            USER_SERIAL.println(F("--- 天気・時刻 ---"));
            USER_SERIAL.println(F("/today              今日の天気予報"));
            USER_SERIAL.println(F("/tomorrow           明日の天気予報"));
            USER_SERIAL.println(F("/dat                明後日の天気予報"));
            USER_SERIAL.println(F("/time               現在時刻を読み上げ（NICT）"));
            USER_SERIAL.println(F("--- システム ---"));
            USER_SERIAL.println(F("/status             WiFi/メモリ等のステータス表示"));
            USER_SERIAL.println(F("/clear              会話履歴をクリア"));
            USER_SERIAL.println(F("/reset              再起動"));
            USER_SERIAL.println(F("/ap                 APモード（WiFi設定）"));
            USER_SERIAL.println(F("/initialize         設定をデフォルト値に戻す"));
            USER_SERIAL.println(F("--- テスト ---"));
            USER_SERIAL.println(F("/beep               ビープ音テスト"));
            USER_SERIAL.println(F("/mic                マイクテスト（3秒録音）"));
            USER_SERIAL.println(F("/stt                STTテスト（録音→文字変換）"));
            USER_SERIAL.println(F("/tts                TTSテスト（読み上げ確認）"));
        } else {
            USER_SERIAL.println(F("(WiFi offline - 利用可能なコマンド)"));
            USER_SERIAL.println(F("/music              ミュージックプレーヤーモードを起動"));
            USER_SERIAL.println(F("/camera  /cam       ウェブカメラモードを起動"));
            USER_SERIAL.println(F("/status             ステータス表示"));
            USER_SERIAL.println(F("/reset              再起動"));
            USER_SERIAL.println(F("/ap                 APモード（WiFi設定）"));
        }
        USER_SERIAL.println(F("============================")); return true;
    }
    if (cmd == "/initialize") {
        if (!wifiConnected) { LOG_E("No WiFi"); return true; }
        LOG_E("Reset settings..."); Preferences p; p.begin("twentychan", false); p.clear(); p.end();
        strlcpy(appSettings.wifiSsid, WIFI_SSID, sizeof(appSettings.wifiSsid));
        strlcpy(appSettings.wifiPassword, WIFI_PASSWORD, sizeof(appSettings.wifiPassword));
        strlcpy(appSettings.lmHost, LMSTUDIO_HOST, sizeof(appSettings.lmHost));
        appSettings.lmPort = LMSTUDIO_PORT;
        strlcpy(appSettings.whisperHost, WHISPER_HOST, sizeof(appSettings.whisperHost));
        appSettings.whisperPort = WHISPER_PORT;
        strlcpy(appSettings.ttsHost, TTS_HOST, sizeof(appSettings.ttsHost));
        appSettings.ttsPort = TTS_PORT;
        strlcpy(appSettings.modelName, MODEL_NAME, sizeof(appSettings.modelName));
        appSettings.ttsSpeakerId = TTS_SPEAKER_ID;
        strlcpy(appSettings.systemPrompt, SYSTEM_PROMPT, sizeof(appSettings.systemPrompt));
        LOG_I("Defaults restored"); USER_SERIAL.println(F(" /reset to reboot")); return true;
    }
    if (cmd == "/clear") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } clearHistory(); LOG_I("History cleared"); return true; }
    if (cmd == "/status") {
        USER_SERIAL.println();
        USER_SERIAL.print(F("=== ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.println(F(" Status ==="));
        USER_SERIAL.print(F("WiFi:")); USER_SERIAL.println(wifiConnected ? F("OK") : (wifiRetryExhausted ? F("Offline") : F("NG")));
        USER_SERIAL.print(F("IP:")); USER_SERIAL.println(WiFi.localIP());
        USER_SERIAL.print(F("RSSI:")); USER_SERIAL.print(WiFi.RSSI()); USER_SERIAL.println(F(" dBm"));
        USER_SERIAL.print(F("Hist:")); USER_SERIAL.print(historyCount); USER_SERIAL.print(F("/")); USER_SERIAL.println(MAX_MESSAGE_HISTORY);
        USER_SERIAL.print(F("LM:")); USER_SERIAL.print(appSettings.lmHost); USER_SERIAL.print(F(":")); USER_SERIAL.println(appSettings.lmPort);
        USER_SERIAL.print(F("Whisper:")); USER_SERIAL.print(appSettings.whisperHost); USER_SERIAL.print(F(":")); USER_SERIAL.print(appSettings.whisperPort); USER_SERIAL.println(WHISPER_PATH);
        USER_SERIAL.print(F("TTS:")); USER_SERIAL.print(appSettings.ttsHost); USER_SERIAL.print(F(":")); USER_SERIAL.println(appSettings.ttsPort);
        USER_SERIAL.print(F("Model:")); USER_SERIAL.println(appSettings.modelName);
        USER_SERIAL.print(F("Heap:")); USER_SERIAL.print(ESP.getFreeHeap()); USER_SERIAL.println(F(" bytes"));
        USER_SERIAL.print(F("PSRAM:")); if (ESP.getPsramSize() > 0) { USER_SERIAL.print(ESP.getPsramSize()); USER_SERIAL.print(F(" (Free:")); USER_SERIAL.print(ESP.getFreePsram()); USER_SERIAL.println(F(")")); } else USER_SERIAL.println(F("None"));
        USER_SERIAL.print(F("Voice:")); switch(voiceState) { case VOICE_IDLE: USER_SERIAL.println(F("Idle")); break; case VOICE_LISTENING: USER_SERIAL.println(F("Listen")); break; case VOICE_PROCESSING: USER_SERIAL.println(F("Proc")); break; case VOICE_SPEAKING: USER_SERIAL.println(F("Speak")); break; }
        USER_SERIAL.println(F("============================")); return true;
    }
    if (cmd == "/reset") { LOG_I("Reboot..."); delay(500); ESP.restart(); return true; }
    if (cmd == "/today") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } speakWeather(0); return true; }
    if (cmd == "/tomorrow") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } speakWeather(1); return true; }
    if (cmd == "/dat") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } speakWeather(2); return true; }
    if (cmd == "/time") { speakTime(); return true; }
    if (cmd == "/voice") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } LOG_I("Voice mode"); voiceConversationLoop(); return true; }
    if (cmd == "/ap") {
        USER_SERIAL.println(F("========================================\n  AP Mode start\n  SSID:ESPer-Chan-Setup\n  PW:setup1234\n========================================"));
        startAPMode(); return true;
    }
    if (cmd == "/text") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } LOG_I("Text mode"); return true; }
    if (cmd == "/beep") { if (!wifiConnected) { LOG_E("No WiFi"); return true; } LOG_I("Beep test"); speakBeep(); LOG_I("Done"); return true; }
#if USE_VOICE_INPUT
    if (cmd == "/mic") {
        if (!wifiConnected) { LOG_E("No WiFi"); return true; }
        LOG_I("Mic test 3s..."); const int ts = MIC_SAMPLE_RATE * 3;
        int16_t* b = SAFE_ALLOC(int16_t, ts * sizeof(int16_t));
        if (b) { int s = recordAudio(b, ts, 3000); USER_SERIAL.print(F("Samples:")); USER_SERIAL.println(s);
            int32_t sum = 0; for (int i = 0; i < s; i++) sum += abs(b[i]); USER_SERIAL.print(F("Avg:")); USER_SERIAL.println(s > 0 ? sum / s : 0); SAFE_FREE(b); }
        else USER_SERIAL.println(F("⚠️ mem")); return true;
    }
    if (cmd == "/stt") {
        if (!wifiConnected) { LOG_E("No WiFi"); return true; }
        LOG_I("STT test..."); const int ts = MIC_SAMPLE_RATE * 3;
        int16_t* b = SAFE_ALLOC(int16_t, ts * sizeof(int16_t));
        if (b) { USER_SERIAL.println(F("Rec 3s...")); int s = recordAudio(b, ts, 3000); USER_SERIAL.println(F("STT..."));
            String r = sendAudioToWhisper(b, s); USER_SERIAL.print(F("Result:")); USER_SERIAL.println(r); SAFE_FREE(b); }
        else USER_SERIAL.println(F("⚠️ mem")); return true;
    }
#else
    if (cmd == "/mic" || cmd == "/stt") { LOG_E("Voice input disabled (USE_VOICE_INPUT=0)"); return true; }
#endif
    if (cmd == "/tts") {
        if (!wifiConnected) { LOG_E("No WiFi"); return true; }
        LOG_I("TTS test..."); USER_SERIAL.print(F("Say:こんにちは、")); USER_SERIAL.print(SPEAK_NAME); USER_SERIAL.println(F("です"));
        speakResponse(String("こんにちは、") + SPEAK_NAME + "です"); USER_SERIAL.println(F("[TTS done]")); return true;
    }
    if (cmd.startsWith("/say ")) {
        if (!wifiConnected) { LOG_E("No WiFi"); return true; }
        String t = input.substring(5); t.trim();
        if (t.length() > 0) { USER_SERIAL.print(F("[Say:")); USER_SERIAL.print(t); USER_SERIAL.println(F("]")); speakResponse(t); LOG_I("Done"); }
        else LOG_E("Usage: /say <text>"); return true;
    }
    return false;
}

// ============================================
// Avatar 表情
// ============================================
#if USE_AVATAR
#if USE_CUSTOM_FACE
CustomExpression mapExpression(Expression expr) {
    switch(expr) {
        case Expression::Neutral: return EXPR_NEUTRAL;
        case Expression::Happy: return EXPR_HAPPY;
        case Expression::Angry: return EXPR_ANGRY;
        case Expression::Sad: return EXPR_SAD;
        case Expression::Doubt: return EXPR_SCHEMING;
        case Expression::Sleepy: return EXPR_NEUTRAL;
        default: return EXPR_NEUTRAL;
    }
}
#endif
void setAgentExpression(Expression expr) {
    avatar.setExpression(expr);
#if USE_CUSTOM_FACE
    customFace.setCustomExpression(mapExpression(expr));
#endif
#if USE_WEB_AVATAR
    switch (expr) {
        case Expression::Happy:  setGifFaceIndex(1); break;
        case Expression::Sad:    setGifFaceIndex(2); break;
        case Expression::Angry:  setGifFaceIndex(3); break;
        default:                 setGifFaceIndex(0); break;
    }
#endif
}
#if USE_CUSTOM_FACE
void setAgentExpression(Expression expr, CustomExpression customExpr) {
    avatar.setExpression(expr); customFace.setCustomExpression(customExpr);
}
#endif
#endif

// ============================================
// ブート画面
// ============================================
void printBootScreen() {
    USER_SERIAL.println();
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.print(F("  AI Agent ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.print(F(" ")); USER_SERIAL.println(PROJECT_VERSION);
    USER_SERIAL.println(F("  ESP32S3 x LM Studio + whisper.cpp + VOICEVOX"));
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println(F("Usage: Text=Enter /voice=Voice /help=Commands"));
    USER_SERIAL.println("WakeWord: Say '" WAKEWORD_TEXT "'");
    USER_SERIAL.println();
}

void printResponse(const String& response) {
    USER_SERIAL.println();
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.print(F("🤖 ")); USER_SERIAL.print(PROJECT_NAME); USER_SERIAL.print(F(": ")); USER_SERIAL.println(response);
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println(); USER_SERIAL.println(F("👤 あなた: "));
}

// ============================================
// Setup
// ============================================
void setup() {
    // I2Sピンをフロートさせないようブート直後に LOW 出力に固定。
    // MAX98357A は BCLK/DOUT がフロートしていると不定データを音声と解釈してノイズを出す。
    pinMode(I2S_DOUT_PIN, OUTPUT); digitalWrite(I2S_DOUT_PIN, LOW);
    pinMode(I2S_BCLK_PIN, OUTPUT); digitalWrite(I2S_BCLK_PIN, LOW);
    pinMode(I2S_LRC_PIN,  OUTPUT); digitalWrite(I2S_LRC_PIN,  LOW);

    setupUSBSerial(); USER_SERIAL.begin(SERIAL_BAUD);
    unsigned long ss = millis();
    while (!USER_SERIAL && (millis() - ss < 3000)) delay(10);
#if USE_USB_CDC
    DBG_SERIAL.begin(SERIAL_BAUD);
#endif
    setupLED(); delay(1000); printBootScreen(); USER_SERIAL.flush();

    loadSettings(); LOG_I("Settings loaded");

    // ============================================
    // NVS未設定（初回起動）→ APモードで設定を行う
    // ============================================
    if (!isSettingsConfigured()) {
        USER_SERIAL.println(F("[BOOT] 初回起動: NVSに設定がありません"));
        USER_SERIAL.println(F("========================================"));
        USER_SERIAL.println(F("  APモードに移行します"));
        USER_SERIAL.println(F("  SSID: ESPer-Chan-Setup"));
        USER_SERIAL.println(F("  PW  : setup1234"));
        USER_SERIAL.println(F("  URL : http://192.168.20.1/"));
        USER_SERIAL.println(F("========================================"));
        startAPMode();
        return;
    }

    // ============================================
    // カメラ専用ブートモード判定（M5/Avatar 初期化より前）
    // ============================================
    if (appSettings.bootMode == BOOT_MODE_CAMERA) {
        Serial.println(F("[BOOT] Camera boot flag detected."));
#if USE_CAMERA
        setupCameraBootMode();
        return;  // 以降、Avatar/M5/WiFi 等の通常初期化を完全スキップ
#else
        Serial.println(F("[BOOT] Camera disabled (USE_CAMERA=0). Resetting boot mode."));
        appSettings.bootMode = BOOT_MODE_NORMAL;
        saveSettings();
#endif
    }

#if USE_AVATAR
    LOG_I("Avatar init...");
    initDisplay();
    
    // SPI2はM5.begin()(initDisplay内)で初期化済み。Avatar起動前にSDデバイスを登録。
#if USE_SD
    preinitMusicPlayerSD();
#endif
#if USE_CUSTOM_FACE
    if (appSettings.faceType == 1) {
        // ESPer-Chan カスタム顔
        avatar.setFace(&customFace);
        avatar.setScale(AVATAR_SCALE_ORIGINAL);
        avatar.setPosition(AVATAR_POS_Y, AVATAR_POS_X);
        {
            ColorPalette cp;
            cp.set(COLOR_PRIMARY,    (uint16_t)0xFFFF);
            cp.set(COLOR_BACKGROUND, (uint16_t)0x0000);
            avatar.setColorPalette(cp);
            M5.Display.fillScreen(0x0000);
        }
        avatar.init();
    } else {
#else
    {
#endif
        // StackChan デフォルト顔
        if (appSettings.displayType == DISPLAY_SSD1306) {
            avatar.setScale(AVATAR_SCALE_DEFAULT);
            avatar.setPosition(-84, -96);
            avatar.init(1);
        } else {
            // TFT: FixedFace で中心座標を固定
            _fixedFacePtr = new m5avatar::FixedFace(M5.Display.width()/2, M5.Display.height()/2);
            avatar.setFace(_fixedFacePtr);
            avatar.setScale(AVATAR_SCALE_DEFAULT);
            {
                ColorPalette cp;
                cp.set(COLOR_PRIMARY,    (uint16_t)0xFFFF);
                cp.set(COLOR_BACKGROUND, (uint16_t)0x0000);
                avatar.setColorPalette(cp);
                M5.Display.fillScreen(0x0000);
            }
            avatar.init(16);
        }
    }
    LOG_I("Avatar OK (face:%d display:%d)", appSettings.faceType, appSettings.displayType);
#endif

    // ============================================
    // DAC先行初期化（WiFiリトライ中のノイズ抑制）
    // ============================================
#if USE_VOICE_OUTPUT
    if (setupSpeaker()) {
        int16_t zeroBuf[64] = {0};
        I2S_Dac.write((uint8_t*)zeroBuf, sizeof(zeroBuf));
        LOG_I("DAC pre-init for noise suppression");
    } else {
        LOG_E("DAC pre-init fail");
    }
#endif

    // ============================================
    // WiFi接続（リトライなし。失敗時はAPモードに移行）
    // ============================================
    if (connectWiFi()) {
        wifiRetryCount = 0;
        wifiFirstConnect = false;
    } else {
        USER_SERIAL.println(F("⚠️ WiFi接続失敗。APモードに移行します"));
        USER_SERIAL.println(F("  SSID: ESPer-Chan-Setup  PW: setup1234  URL: http://192.168.20.1/"));
        startAPMode();
        return;
    }

    USER_SERIAL.print(F("PSRAM:"));
    if (ESP.getPsramSize() > 0) { USER_SERIAL.print(ESP.getPsramSize() / 1024); USER_SERIAL.println(F(" KB")); }
    else USER_SERIAL.println(F("None"));

    USER_SERIAL.print(F("🗣️ Whisper:")); USER_SERIAL.print(appSettings.whisperHost); USER_SERIAL.print(F(":")); USER_SERIAL.print(appSettings.whisperPort); USER_SERIAL.println(WHISPER_PATH);
    USER_SERIAL.print(F("🔊 TTS(VOICEVOX):")); USER_SERIAL.print(appSettings.ttsHost); USER_SERIAL.print(F(":")); USER_SERIAL.println(appSettings.ttsPort);

    // ============================================
    // マイク初期化（WiFi接続確定後）
    // ============================================
#if USE_VOICE_INPUT
    if (setupMicrophone()) {
        voiceAudioBuffer = (int16_t*)ps_malloc(VOICE_MAX_SAMPLES * sizeof(int16_t));
        if (!voiceAudioBuffer) voiceAudioBuffer = (int16_t*)malloc(VOICE_MAX_SAMPLES * sizeof(int16_t));
        if (!voiceAudioBuffer) LOG_E("Audio buf fail");
    } else LOG_E("Mic fail");
#endif
#if USE_VOICE_OUTPUT
    if (!setupSpeaker()) LOG_E("DAC fail");
#endif

    // WiFi接続成功フィードバック（DAC初期化後に実行）
    if (wifiConnected) {
        blinkLED(2, 100);
        speakPip();
    }

#if USE_AVATAR
    xTaskCreatePinnedToCore(lipSyncTask, "LipSyncTask", 4096, NULL, 1, &lipSyncTaskHandle, 0);
#endif
    lastSpeakEndTime = millis();
#if WAKEWORD_ENABLED && USE_VOICE_INPUT
    xTaskCreatePinnedToCore(wakeWordTask, "WakeWordTask", 8192, NULL, 3, &wakeWordTaskHandle, 1);
    LOG_I("WakeWord task start");
#endif
    webServer.begin();
    if (wifiConnected) {
#if USE_WEB_AVATAR
        initGifServer();
#endif
        LOG_I("WebServer start");
    }
    USER_SERIAL.println(); USER_SERIAL.println(F("👤 あなた: "));
#if USE_SD
    setupMusicPlayer();
#endif
#ifdef BTN_MODE_PIN
    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
#endif
}

// ============================================
// モード切り替えボタン
// ============================================
#ifdef BTN_MODE_PIN
void checkModeButton() {
    static bool last = true;
    static bool initialized = false;
    static unsigned long lastMs = 0;
    bool cur = digitalRead(BTN_MODE_PIN);
    if (!initialized) { last = cur; lastMs = millis(); initialized = true; return; }
    if (cur && !last && millis() - lastMs > 250) {
        lastMs = millis();
        if (systemMode == MODE_AGENT) {
            systemMode = MODE_MUSIC;
            startMusicMode();
        } else if (systemMode == MODE_MUSIC) {
            startCameraMode();  // NVS保存→reboot→カメラモード
        }
    }
    last = cur;
}
#endif

// ============================================
// Loop
// ============================================
void loop() {

    // カメラ専用ブートモード
#if USE_CAMERA
    if (appSettings.bootMode == BOOT_MODE_CAMERA) {
        loopCameraBootMode();
        return;
    }
#endif
#ifdef BTN_MODE_PIN
    checkModeButton();
#endif

    webServer.handleClient();
#if USE_SD
    if (systemMode == MODE_MUSIC) {
#if USE_AVATAR
        M5.update();
#endif
        loopMusicPlayer();
        processSerialInput();
        if (inputReady) {
            String input = getInputBuffer();
            if (input.length() > 0) handleMusicSerialCommand(input);
        }
        return;
    }
#endif
    if (systemMode == MODE_CAMERA) {
        // カメラモード時はAvatar/SSD1306を完全停止。M5.update()は呼ばない。
#if USE_CAMERA
        loopCameraMode();
#endif
        return;
    }
#if USE_WEB_AVATAR
    if (g_webVoiceRequested) {
        g_webVoiceRequested = false;
        if (apModeActive) LOG_E("AP mode: ignore web voice");
        else { LOG_I("Web voice start"); voiceConversationLoop(); }
    }
#endif
    if (apModeActive) { yield(); delay(50); return; }
#if USE_AVATAR
    M5.update();
#endif
    checkWiFiConnection(); processSerialInput();
    if (inputReady) {
        String input = getInputBuffer();
        if (input.length() > 0) {
            USER_SERIAL.println();
            if (processCommand(input)) { USER_SERIAL.println(F("👤 あなた: ")); return; }
            LOG_I("User:%s", input.c_str()); setLED(LED_BRIGHTNESS / 2);
            String resp = sendChatRequest(input.c_str()); printResponse(resp);
#if USE_VOICE_OUTPUT
            speakResponse(resp);
#endif
            setLED(LED_BRIGHTNESS);
        }
    }
#if WAKEWORD_ENABLED && USE_VOICE_INPUT
    if (voiceState == VOICE_IDLE && !inputReady && inputBuffer.length() == 0 && USER_SERIAL.available() == 0) listenForWakeWord();
#endif
    yield(); delay(50);
}