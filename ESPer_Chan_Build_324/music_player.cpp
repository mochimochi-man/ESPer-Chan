#include "music_player.h"
#include "config.h"
#if USE_SD
#include "settings.h"
#include <SPI.h>
#include <SD.h>
#include <Audio.h>
#include <ESP_I2S.h>
#include <vector>
#include <algorithm>

// ============================================
// AvatarMode globals (extern)
// ============================================
extern I2SClass I2S_Dac;
extern bool setupSpeaker();
extern void speakPip(int count = 1, int gapMs = 0);
#if USE_AVATAR
#include <Avatar.h>
extern m5avatar::Avatar avatar;
extern TaskHandle_t wakeWordTaskHandle;
extern TaskHandle_t lipSyncTaskHandle;
extern volatile bool g_isLipSyncing;
extern volatile float g_lipSyncLevel;
#if USE_CUSTOM_FACE
#include "CustomFace.h"
extern CustomFace customFace;
extern volatile bool g_musicModeSuspending;
#endif
#endif
#if USE_WEB_AVATAR
extern void setGifFaceIndex(int index);
#endif
extern SystemMode systemMode;

// ============================================
// MusicMode type definitions
// ============================================
struct IndexEntry {
    String filepath;
    String album;
    String artist;
    String title;
};

enum PlayMode {
    MODE_INDEX,
    MODE_RANDOM,
    MODE_ARTIST
};

// ============================================
// MusicMode global variables
// ============================================
Audio audio;
SPIClass sdSPI;

String currentPath = "/";
String currentFile = "";
std::vector<IndexEntry> indexList;
std::vector<int> playOrder;
int currentTrackIdx = -1;
bool isPlaying = false;
bool isPaused = false;
bool repeatMode = false;
int currentVolume = DEFAULT_VOLUME;
PlayMode currentMode = MODE_INDEX;
String cmdBuffer = "";

// Multi-layer EOF detection state
bool eofTriggered = false;
uint32_t lastCur = 0;
uint32_t lastDur = 0;
unsigned long trackStartTime = 0;
unsigned long lastCurChangeTime = 0;

// Command debounce
unsigned long lastCmdTime = 0;
const unsigned long CMD_DEBOUNCE_MS = 300;

// ============================================
// Forward declarations (ALL functions)
// ============================================
// Mode control
void setupMusicPlayer();
void loopMusicPlayer();
void startMusicMode();
void stopMusicMode(bool showPrompt);
bool isMusicModeActive();

// Audio callbacks
void onAudioEvent(Audio::msg_t m);

// PJB functions
void printHelp();
bool initSD();
bool initAudio();
void loadIndex();
void generateIndex();
void buildPlayOrder(PlayMode mode);
void playTrack(int orderIdx);
void playNext();
void playPrev();
void stopPlayback();
void setVolume(int vol);
void processCommand(String& cmd);
bool isAudioFile(const String& filename);
void printSDInfo();
void printStatus();
void printPlaylist();
void listDirectory(const char* path);
void changeDirectory(const char* dir);
String getFullPath(const String& filename);
String getFilenameFromPath(const String& path);
String getModeString();
void checkEOF();
String readID3Tag(const String& filepath, const char* frameID);
String parseID3Text(const uint8_t* data, size_t len);

// M4A/MP4 tag reader
struct M4ATags {
    String title;
    String artist;
    String album;
};
M4ATags readM4ATags(const String& filepath);

// ============================================
// Audio callbacks
// ============================================
// 現行のESP32-audioI2Sは個別のweakコールバック(audio_eof_mp3等)ではなく
// Audio::audio_info_callback (std::function) に一本化されている。
// audio.loop()がaudio.get_info()経由でこれを呼ぶため、playNext()等の呼び出しも安全。
void onAudioEvent(Audio::msg_t m) {
    if (m.e == Audio::evt_eof) {
        if (!eofTriggered) {
            eofTriggered = true;
            if (repeatMode && currentTrackIdx >= 0) {
                playTrack(currentTrackIdx);
            } else {
                playNext();
            }
        }
    } else if (m.e == Audio::evt_info) {
        if (m.msg) LOG_I("[INFO] %s", m.msg);  // コーデック内部情報は診断用（DEBUG_LEVEL>=2で表示）
    }
}

// ============================================
// Schroeder リバーブ (DTS 臨終感エフェクト)
// ============================================
// コムフィルタ4本(並列) + オールパス2本(直列) の Freeverb 方式。
// outBuff は Audio ライブラリ内部で「int16_t << 16」形式の int32_t ステレオ交互配列。
// バッファは ps_malloc で PSRAM に確保（合計 ~26KB）。
// 遅延サンプル数は 44100Hz 基準。48kHz でも COMB_SIZE 内に収まるよう余裕を確保。

static bool   s_verb_ready       = false;
static float* s_verb_c[4]        = {};   // コムフィルタバッファ
static int    s_verb_ci[4]       = {};   // コムフィルタ書き込みインデックス
static float* s_verb_a[2]        = {};   // オールパスバッファ
static int    s_verb_ai[2]       = {};   // オールパス書き込みインデックス

static const int   VERB_COMB_DELAY[4] = {1116, 1188, 1277, 1356};  // 遅延量[サンプル]
static const int   VERB_COMB_SIZE[4]  = {1200, 1300, 1400, 1500};  // バッファ上限
static const float VERB_COMB_FB       = 0.84f;   // フィードバック係数（残響時間を決める）
static const int   VERB_AP_DELAY[2]   = {225,  556};
static const int   VERB_AP_SIZE[2]    = {300,  700};
static const float VERB_AP_COEFF      = 0.5f;
static const float VERB_WET           = 0.11f;  // ルーム感の強さ（0.28×0.4）
static const float VERB_DRY           = 0.89f;

static void initReverb() {
    for (int i = 0; i < 4; i++) {
        if (!s_verb_c[i]) s_verb_c[i] = (float*)ps_malloc(VERB_COMB_SIZE[i] * sizeof(float));
        if (s_verb_c[i])  memset(s_verb_c[i], 0, VERB_COMB_SIZE[i] * sizeof(float));
        s_verb_ci[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        if (!s_verb_a[i]) s_verb_a[i] = (float*)ps_malloc(VERB_AP_SIZE[i] * sizeof(float));
        if (s_verb_a[i])  memset(s_verb_a[i], 0, VERB_AP_SIZE[i] * sizeof(float));
        s_verb_ai[i] = 0;
    }
    s_verb_ready = s_verb_c[0] && s_verb_c[1] && s_verb_c[2] && s_verb_c[3] &&
                   s_verb_a[0] && s_verb_a[1];
    if (!s_verb_ready) LOG_E("[VERB] PSRAM allocation failed");
}

// 曲切り替え時に呼ぶ軽量リセット（再確保はせずバッファの中身だけクリア）。
// これをしないと前の曲の残響テールが次の曲の冒頭に漏れ込む。
static void resetReverb() {
    for (int i = 0; i < 4; i++) {
        if (s_verb_c[i]) memset(s_verb_c[i], 0, VERB_COMB_SIZE[i] * sizeof(float));
        s_verb_ci[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        if (s_verb_a[i]) memset(s_verb_a[i], 0, VERB_AP_SIZE[i] * sizeof(float));
        s_verb_ai[i] = 0;
    }
}

// Audio ライブラリの弱参照コールバックをオーバーライド。
// s_verb_ready が false のときは何もせず即リターン（ライブラリのデフォルト動作と同じ）。
void audio_process_i2s(int32_t* outBuff, int16_t validSamples, bool* /*continueI2S*/) {
    if (!s_verb_ready) return;

    for (int i = 0; i < validSamples; i++) {
        // 「int16 << 16」形式 → float [-1.0, 1.0] に変換（符号付き算術右シフト）
        float in = (float)(outBuff[i * 2] >> 16) * (1.0f / 32768.0f);

        // 4本のコムフィルタを並列合成
        float comb = 0.0f;
        for (int c = 0; c < 4; c++) {
            float o = s_verb_c[c][s_verb_ci[c]];
            s_verb_c[c][s_verb_ci[c]] = in + o * VERB_COMB_FB;
            if (++s_verb_ci[c] >= VERB_COMB_DELAY[c]) s_verb_ci[c] = 0;
            comb += o;
        }
        comb *= 0.25f;

        // 2本のオールパスフィルタを直列（拡散・自然な残響感）
        for (int a = 0; a < 2; a++) {
            float bo = s_verb_a[a][s_verb_ai[a]];
            float o  = bo - comb;
            s_verb_a[a][s_verb_ai[a]] = comb + bo * VERB_AP_COEFF;
            if (++s_verb_ai[a] >= VERB_AP_DELAY[a]) s_verb_ai[a] = 0;
            comb = o;
        }

        // ドライ/ウェット混合 → クランプ
        float out_f = in * VERB_DRY + comb * VERB_WET;
        if      (out_f >  1.0f) out_f =  1.0f;
        else if (out_f < -1.0f) out_f = -1.0f;

        // float [-1.0, 1.0] → 「int16 << 16」形式に戻す
        int32_t s = (int32_t)(out_f * 32767.0f) << 16;
        outBuff[i * 2]     = s;
        outBuff[i * 2 + 1] = s;  // モノラル: L = R
    }
}

// ============================================
// Mode control functions
// ============================================
void setupMusicPlayer() {
    // Delayed initialization: startMusicMode() performs actual setup
}

void loopMusicPlayer() {
    audio.loop();
    checkEOF();
}

void handleMusicSerialCommand(const String& cmd) {
    String c = cmd;
    processCommand(c);
}

// setup() から呼ばれるスタブ。実際のSPI2リセットは startMusicMode() 内で
// avatar.suspend() 後に行う（DMA完全停止後でないとハードウェアリセットが危険）。
void preinitMusicPlayerSD() {
    LOG_I("[SD] Music player pre-init (SPI2 reset deferred to startMusicMode)");
}

void startMusicMode() {
    // カメラモード中なら先に停止（排他制御）
    #if USE_CAMERA
    if (systemMode == MODE_CAMERA) {
        extern void stopCameraMode();
        stopCameraMode();
    }
    #endif

    USER_SERIAL.println();
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println(F("  ミュージックプレーヤーモード"));
    USER_SERIAL.println(F("========================================"));
    speakPip();  // モード切り替えフィードバック（DAC終了前に鳴らす）

    // Stop AvatarMode I2S DAC to free pins for MusicMode Audio
    I2S_Dac.end();
    delay(300);  // I2Sハードウェア完全停止待ち

    // Suspend AvatarMode tasks
    #if USE_AVATAR
    // 自タスクからの呼び出し（音声コマンド経由）の場合は自己サスペンドを回避する
    if (wakeWordTaskHandle != NULL && xTaskGetCurrentTaskHandle() != wakeWordTaskHandle) {
        vTaskSuspend(wakeWordTaskHandle);
    }
    if (lipSyncTaskHandle != NULL) {
        vTaskSuspend(lipSyncTaskHandle);
    }

    // リップシンク状態のみリセット（顔の表情はそのまま freeze させる）
    g_isLipSyncing = false;
    g_lipSyncLevel = 0.0f;
    #endif
    #if USE_WEB_AVATAR
    setGifFaceIndex(0);
    #endif

    // Avatar 描画タスクを停止して SPI2 バスを排他（TFT と SD の競合防止）
    // SSD1306: I2C接続のためSPI2競合なし。suspend()するとI2C転送が中断されデッドロックが起きる。
    #if USE_AVATAR
    if (appSettings.displayType != DISPLAY_SSD1306) {
        #if USE_CUSTOM_FACE
        // vTaskSuspend() はタスクが startWrite()/endWrite() の途中でも即座に凍結する。
        // その状態で reinitDisplaySPI() が M5.Display.init() を呼ぶと
        // LovyanGFX 内部の _transaction_count がリセットされ、resume() 後に
        // endWrite() がアンダーフロー(0→0xFFFFFFFF)し、以降の startWrite() が
        // CS をアサートしないまま描画をスキップし続ける（永久に顔が表示されない）。
        // g_musicModeSuspending フラグで CustomFace::draw() を即時リターンさせ、
        // drawLoop が vTaskDelay(10) に入ってから suspend する。
        g_musicModeSuspending = true;
        delay(80);  // 最大フレーム時間(~50ms) + TaskDelay(10ms) + マージン
        #endif
        avatar.suspend();
        #if USE_CUSTOM_FACE
        g_musicModeSuspending = false;
        #else
        delay(300);  // カスタム顔なし: DMA完了待ち
        #endif
    }
    #endif

    // SPI2 を SDカードデバイス用の Arduino ダイレクトアクセスモードに切り替える。
    // TFT(SPI2共有)の場合: IDF DMAモードから切り離す必要がある。
    //   - periph_ll_reset(PERIPH_SPI2_MODULE) 時に CS が LOW だと GRAM が汚染されるため
    //     SPI.end() の前に CS を強制 HIGH にしておく。
    // SSD1306(I2C)の場合: SPI2は未使用のため SPI.end() は不要。ただし SD用の
    //   Arduino SPI2 を起動するために SPI.begin() は必ず呼ぶ必要がある。
    if (appSettings.displayType != DISPLAY_SSD1306) {
        pinMode(TFT_CS_PIN, OUTPUT);
        digitalWrite(TFT_CS_PIN, HIGH);
        SPI.end();  // IDF DMAモードのリンクを断ち切る（periph_ll_reset を発行）
    }
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);  // SD用 Arduino SPI2 を起動
    LOG_I("[SD] SPI2 Arduinoダイレクトアクセスモードに切り替え");

    // Init MusicMode SD & Audio
    if (!initSD()) {
        USER_SERIAL.println(F("⚠️ SDカードデバイスの初期化に失敗しました。AvatarModeに戻ります。"));
        // 呼び出し元（音声/テキストコマンド処理側）がこの後で自分の「あなた:」プロンプトを
        // 表示するため、ここでは表示せず二重表示を防ぐ。
        stopMusicMode(false);
        return;
    }
    if (!initAudio()) {
        USER_SERIAL.println(F("⚠️ オーディオの初期化に失敗しました。AvatarModeに戻ります。"));
        stopMusicMode(false);
        return;
    }
    initReverb();  // リバーブバッファ初期化（PSRAMが確保できなければ無効のまま）
    loadIndex();
    if (indexList.empty()) {
        USER_SERIAL.println(F("[INDEX] /index.csv が見つからないか壊れています。自動生成を開始します..."));
        USER_SERIAL.flush();
        generateIndex();
        if (indexList.empty()) {
            USER_SERIAL.println(F("⚠️ インデックス生成に失敗しました。AvatarModeに戻ります。"));
            stopMusicMode(false);
            return;
        }
    }
    printHelp();

    // 起動時自動再生
    if (appSettings.musicAutoPlay != MUSIC_AUTO_PLAY_NONE) {
        PlayMode pm = MODE_INDEX;
        if      (appSettings.musicAutoPlay == MUSIC_AUTO_PLAY_RANDOM)  pm = MODE_RANDOM;
        else if (appSettings.musicAutoPlay == MUSIC_AUTO_PLAY_ARTIST)  pm = MODE_ARTIST;
        buildPlayOrder(pm);
        if (!playOrder.empty()) {
            USER_SERIAL.println(F("[AUTO] 自動再生を開始します"));
            playTrack(0);
        }
    }
}

void stopMusicMode(bool showPrompt) {
    s_verb_ready = false;  // Audio タスクのコールバックを即無効化
    stopPlayback();  // 内部で audio.stopSong() 済み
    delay(100);

    // SD を明示的に終了
    SD.end();
    // startMusicMode() の SPI.begin(SD_SCK_PIN, ...) で Arduino SPIClass が SPI2_HOST を
    // 掴んだままになっている。SD.end() はファイルシステム層を閉じるだけでバスは解放しないため、
    // ここで明示的に SPI.end() しないと reinitDisplaySPI() の _tft_bus.init() が
    // 「ホスト使用中」で失敗し、TFTが再初期化されず白画面のまま固まる。
    SPI.end();

    // SPI2 IDF DMAバスを再構築してから画面をクリアする。
    // startMusicMode() の SPI.begin() が periph_ll_reset(PERIPH_SPI2_MODULE) を呼んだため、
    // SPI2-GDMA チャンネルリンクが切れている。このまま fillScreen() を呼ぶと
    // spi_device_polling_transmit() が GDMA 待ちで永久ハングする。
    // reinitDisplaySPI() が release()+init() で GDMA-SPI2 を再結合してから fillScreen() する。
    extern void reinitDisplaySPI();
    reinitDisplaySPI();

    // Restart AvatarMode I2S DAC
    if (!setupSpeaker()) {
        USER_SERIAL.println("[WARN] Speaker re-init failed");
    }
    speakPip();  // 通常モード復帰フィードバック（DAC復活直後に鳴らす）

    // Avatar 描画タスクを再開（TFTの場合のみ停止していた）
    #if USE_AVATAR
    if (appSettings.displayType != DISPLAY_SSD1306) {
        avatar.resume();
    }
    #endif

    // Resume AvatarMode tasks
    #if USE_AVATAR
    if (wakeWordTaskHandle != NULL) {
        vTaskResume(wakeWordTaskHandle);
    }
    if (lipSyncTaskHandle != NULL) {
        vTaskResume(lipSyncTaskHandle);
    }

    g_isLipSyncing = false;
    g_lipSyncLevel = 0.0f;
    #if USE_CUSTOM_FACE
    customFace.setMouthOpenRatio(0.0f);
    customFace.setCustomExpression(EXPR_NEUTRAL);
    #endif
    #endif
    #if USE_WEB_AVATAR
    setGifFaceIndex(0);
    #endif

    systemMode = MODE_AGENT;
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println(F("  通常モードに戻りました"));
    USER_SERIAL.println(F("========================================"));
    if (showPrompt) USER_SERIAL.println(F("👤 あなた: "));
}

bool isMusicModeActive() {
    return systemMode == MODE_MUSIC;
}

// ============================================
// CSV helper
// ============================================
static String csvEscape(const String& s) {
    if (s.indexOf(',') >= 0 || s.indexOf('"') >= 0 || s.indexOf('\n') >= 0) {
        String out = "\"";
        for (size_t i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    }
    return s;
}

// ============================================
// PJB function definitions
// ============================================
// ============================================
// Multi-Layer EOF Detection (silent)
// ============================================
void checkEOF() {
    if (!isPlaying || isPaused || eofTriggered) return;

    uint32_t cur = audio.getAudioCurrentTime();
    uint32_t dur = audio.getAudioFileDuration();
    unsigned long now = millis();

    // Detect song change (new track started, reset all flags)
    if (dur != lastDur && dur > 0) {
        eofTriggered = false;
        lastCur = 0;
        lastDur = dur;
        trackStartTime = now;
        lastCurChangeTime = now;
        return;
    }

    if (dur == 0) return;

    // Update freeze detector
    if (cur != lastCur) {
        lastCurChangeTime = now;
    }

    unsigned long elapsed = now - trackStartTime;
    unsigned long sinceLastChange = now - lastCurChangeTime;

    // Layer 3: Timer-wrap
    if (lastCur > 3 && cur == 0 && !eofTriggered) {
        eofTriggered = true;
        if (repeatMode && currentTrackIdx >= 0) {
            playTrack(currentTrackIdx);
        } else {
            playNext();
        }
        return;
    }

    // Layer 4: Freeze detection
    if (lastCur > 3 && sinceLastChange > 5000 && !eofTriggered) {
        eofTriggered = true;
        if (repeatMode && currentTrackIdx >= 0) {
            playTrack(currentTrackIdx);
        } else {
            playNext();
        }
        return;
    }

    // Layer 5: Max-duration timeout
    if (dur > 3 && elapsed > (unsigned long)(dur + 3) * 1000 && !eofTriggered) {
        eofTriggered = true;
        if (repeatMode && currentTrackIdx >= 0) {
            playTrack(currentTrackIdx);
        } else {
            playNext();
        }
        return;
    }

    // Layer 6: connecttoFS失敗/ファイル破損でdur==0のまま再生が始まらないケース
    if (cur == 0 && dur == 0 && elapsed > 8000 && !eofTriggered) {
        LOG_E("[EOF] 再生がスタックしています。次の曲へスキップします");
        eofTriggered = true;
        playNext();
        return;
    }

    lastCur = cur;
}

bool initSD() {
    LOG_I("[SD] pin: CS=%d, SCK=%d, MISO=%d, MOSI=%d", SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);

    // 前回の SD 状態をリセット（2回目以降の Music Mode 入場に対応）
    SD.end();

    // CS を明示的にHIGHにしてカードを非選択状態にする
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    delay(10);

    // startMusicMode() 内の SPI.begin() で Arduino SPI2 は起動済み。
    USER_SERIAL.println(F("[SD] SDカードデバイスを検出中..."));
    const uint32_t freqs[] = {4000000, 2000000, 1000000};
    for (int i = 0; i < 3; i++) {
        unsigned long t0 = millis();
        LOG_I("[SD] 試行 %d/3 (SPI %d Hz)...", i + 1, freqs[i]);
        if (SD.begin(SD_CS_PIN, SPI, freqs[i])) {
            LOG_I("[SD] SDカードデバイスを初期化しました (%dms)", (int)(millis() - t0));
            return true;
        }
        LOG_I("[SD] 試行 %d/3 失敗 (%dms)", i + 1, (int)(millis() - t0));
        delay(200);
    }
    LOG_E("[SD] SDカードデバイスの初期化に失敗しました");
    USER_SERIAL.println(F("[HINT] 以下を確認してください:"));
    USER_SERIAL.println(F("  1. microSDカードが正しく挿入されているか"));
    USER_SERIAL.println(F("  2. フォーマットがFAT32であるか"));
    USER_SERIAL.println(F("  3. カード容量が32GB以下であるか"));
    return false;
}

bool initAudio() {
    LOG_I("[I2S] pin: BCLK=%d, LRC=%d, DOUT=%d", I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);

    if (I2S_BCLK_PIN == SD_SCK_PIN || I2S_BCLK_PIN == SD_MISO_PIN ||
        I2S_BCLK_PIN == SD_MOSI_PIN || I2S_BCLK_PIN == SD_CS_PIN) {
        LOG_E("[I2S] 致命的: BCLKがSDカードのピンと競合しています！");
        return false;
    }
    if (I2S_LRC_PIN == SD_SCK_PIN || I2S_LRC_PIN == SD_MISO_PIN ||
        I2S_LRC_PIN == SD_MOSI_PIN || I2S_LRC_PIN == SD_CS_PIN) {
        LOG_E("[I2S] 致命的: LRCがSDカードのピンと競合しています！");
        return false;
    }
    if (I2S_DOUT_PIN == SD_SCK_PIN || I2S_DOUT_PIN == SD_MISO_PIN ||
        I2S_DOUT_PIN == SD_MOSI_PIN || I2S_DOUT_PIN == SD_CS_PIN) {
        LOG_E("[I2S] 致命的: DOUTがSDカードのピンと競合しています！");
        return false;
    }

    audio.audio_info_callback = onAudioEvent;  // EOF等のイベント通知を受け取る（必須）
    audio.stopSong();  // 安全のため既存の再生を停止
    delay(50);
    audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    audio.setVolume(DEFAULT_VOLUME - 2);  // ミュージックモードは通常より -2
    currentVolume = DEFAULT_VOLUME - 2;
    delay(100);  // DAC立ち上がり安定化待ち
    LOG_I("[I2S] オーディオを初期化しました");
    return true;
}

void printSDInfo() {
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        USER_SERIAL.println("[SD] エラー: SDカードが検出されません");
        return;
    }
    const char* typeStr = "UNKNOWN";
    switch (cardType) {
        case CARD_MMC:  typeStr = "MMC"; break;
        case CARD_SD:   typeStr = "SDSC"; break;
        case CARD_SDHC: typeStr = "SDHC"; break;
        default:        typeStr = "UNKNOWN"; break;
    }
    LOG_I("[SD] カードタイプ: %s", typeStr);
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    LOG_I("[SD] カードサイズ: %lluMB", cardSize);
    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    LOG_I("[SD] 合計: %lluMB, 使用: %lluMB, 空き: %lluMB", totalBytes, usedBytes, totalBytes - usedBytes);
}

String readID3Tag(const String& filepath, const char* frameID) {
    File f = SD.open(filepath);
    if (!f) return "";

    uint8_t header[10];
    if (f.read(header, 10) < 10 || header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        f.close();
        return "";
    }

    uint8_t id3Major = header[3];
    uint8_t flags    = header[5];
    uint32_t tagSize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) |
                       ((header[8] & 0x7F) <<  7) | (header[9] & 0x7F);

    size_t readSize = min((size_t)tagSize + 10, (size_t)32768);
    uint8_t* tagData = (uint8_t*)ps_malloc(readSize);  // PSRAMを使ってヒープ断片化を防ぐ
    if (!tagData) { f.close(); return ""; }

    f.seek(0);
    size_t actualRead = f.read(tagData, readSize);
    f.close();

    if (actualRead < 10) { free(tagData); return ""; }

    size_t pos = 10;
    if ((flags & 0x40) && actualRead >= 14) {
        uint32_t extSize = (id3Major >= 4)
            ? (((uint32_t)(tagData[10] & 0x7F) << 21) | ((uint32_t)(tagData[11] & 0x7F) << 14) |
               ((uint32_t)(tagData[12] & 0x7F) <<  7) | (tagData[13] & 0x7F))
            : (((uint32_t)tagData[10] << 24) | ((uint32_t)tagData[11] << 16) |
               ((uint32_t)tagData[12] <<  8) | tagData[13]);
        pos += 4 + extSize;
    }

    String result = "";
    int frameCount = 0;
    while (pos + 10 <= actualRead && frameCount < 64) {
        char frameName[5] = {0};
        memcpy(frameName, tagData + pos, 4);
        bool validName = true;
        for (int i = 0; i < 4; i++) {
            char c = frameName[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { validName = false; break; }
        }
        if (!validName) break;

        uint32_t frameSize = (id3Major >= 4)
            ? (((uint32_t)(tagData[pos+4] & 0x7F) << 21) | ((uint32_t)(tagData[pos+5] & 0x7F) << 14) |
               ((uint32_t)(tagData[pos+6] & 0x7F) <<  7) | (tagData[pos+7] & 0x7F))
            : (((uint32_t)tagData[pos+4] << 24) | ((uint32_t)tagData[pos+5] << 16) |
               ((uint32_t)tagData[pos+6] <<  8) | tagData[pos+7]);

        if (frameSize == 0 || frameSize > 100000 || pos + 10 + frameSize > actualRead) break;

        if (strcmp(frameName, frameID) == 0) {
            result = parseID3Text(tagData + pos + 10, frameSize);
        }

        pos += 10 + frameSize;
        frameCount++;
    }

    free(tagData);
    return result;
}

// ID3v1 の固定長フィールド（null/空白パディング）を整形する
static String trimID3v1Field(const uint8_t* data, size_t len) {
    size_t end = len;
    while (end > 0 && (data[end - 1] == '\0' || data[end - 1] == ' ')) end--;
    return String((const char*)data, end);
}

// ファイル末尾128byteの ID3v1 タグから、まだ未取得の項目だけを補完する
static void readID3v1Tags(File& f, String& artist, String& album, String& title) {
    uint32_t fsize = f.size();
    if (fsize < 128) return;
    uint8_t tag[128];
    f.seek(fsize - 128);
    if (f.read(tag, 128) != 128) return;
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') return;

    if (title.length()  == 0) title  = trimID3v1Field(tag + 3,  30);
    if (artist.length() == 0) artist = trimID3v1Field(tag + 33, 30);
    if (album.length()  == 0) album  = trimID3v1Field(tag + 63, 30);
}

// MP3 ファイルを1回だけ開いて artist / album / title を一括取得する。
// ID3v2.2(3byte frame ID) / v2.3 / v2.4(4byte frame ID) と ID3v1(末尾128byte) に対応。
static void readAllID3Tags(const String& filepath, String& artist, String& album, String& title) {
    File f = SD.open(filepath);
    if (!f) return;

    uint8_t header[10];
    bool hasV2 = (f.read(header, 10) == 10 && header[0] == 'I' && header[1] == 'D' && header[2] == '3');

    if (hasV2) {
        uint8_t id3Major = header[3];  // 2 = v2.2, 3 = v2.3, 4 = v2.4
        uint8_t flags    = header[5];
        uint32_t tagSize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) |
                           ((header[8] & 0x7F) <<  7) | (header[9] & 0x7F);

        size_t readSize = min((size_t)tagSize + 10, (size_t)32768);
        uint8_t* buf = (uint8_t*)ps_malloc(readSize);
        if (buf) {
            f.seek(0);
            size_t got = f.read(buf, readSize);

            if (got >= 10) {
                size_t pos = 10;
                if (id3Major >= 3 && (flags & 0x40) && got >= 14) {
                    uint32_t extSize = (id3Major >= 4)
                        ? (((uint32_t)(buf[10] & 0x7F) << 21) | ((uint32_t)(buf[11] & 0x7F) << 14) |
                           ((uint32_t)(buf[12] & 0x7F) <<  7) | (buf[13] & 0x7F))
                        : (((uint32_t)buf[10] << 24) | ((uint32_t)buf[11] << 16) |
                           ((uint32_t)buf[12] <<  8) | buf[13]);
                    pos += 4 + extSize;
                }

                if (id3Major == 2) {
                    // ID3v2.2: フレームID3byte + サイズ3byte(non-syncsafe)、flags無し
                    int found = 0, scanned = 0;
                    while (pos + 6 <= got && scanned < 64 && found < 3) {
                        char fname[4] = {0};
                        memcpy(fname, buf + pos, 3);
                        bool valid = true;
                        for (int k = 0; k < 3; k++) {
                            char c = fname[k];
                            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { valid = false; break; }
                        }
                        if (!valid) break;

                        uint32_t fsize = ((uint32_t)buf[pos+3] << 16) | ((uint32_t)buf[pos+4] << 8) | buf[pos+5];
                        if (fsize == 0 || fsize > 100000 || pos + 6 + fsize > got) break;

                        if      (strcmp(fname, "TT2") == 0) { title  = parseID3Text(buf + pos + 6, fsize); found++; }
                        else if (strcmp(fname, "TP1") == 0) { artist = parseID3Text(buf + pos + 6, fsize); found++; }
                        else if (strcmp(fname, "TAL") == 0) { album  = parseID3Text(buf + pos + 6, fsize); found++; }

                        pos += 6 + fsize;
                        scanned++;
                    }
                } else {
                    // ID3v2.3 / v2.4: フレームID4byte + サイズ4byte + flags2byte
                    int found = 0, scanned = 0;
                    while (pos + 10 <= got && scanned < 64 && found < 3) {
                        char fname[5] = {0};
                        memcpy(fname, buf + pos, 4);
                        bool valid = true;
                        for (int k = 0; k < 4; k++) {
                            char c = fname[k];
                            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) { valid = false; break; }
                        }
                        if (!valid) break;

                        uint32_t fsize = (id3Major >= 4)
                            ? (((uint32_t)(buf[pos+4] & 0x7F) << 21) | ((uint32_t)(buf[pos+5] & 0x7F) << 14) |
                               ((uint32_t)(buf[pos+6] & 0x7F) <<  7) | (buf[pos+7] & 0x7F))
                            : (((uint32_t)buf[pos+4] << 24) | ((uint32_t)buf[pos+5] << 16) |
                               ((uint32_t)buf[pos+6] <<  8) | buf[pos+7]);

                        if (fsize == 0 || fsize > 100000 || pos + 10 + fsize > got) break;

                        if      (strcmp(fname, "TPE1") == 0) { artist = parseID3Text(buf + pos + 10, fsize); found++; }
                        else if (strcmp(fname, "TALB") == 0) { album  = parseID3Text(buf + pos + 10, fsize); found++; }
                        else if (strcmp(fname, "TIT2") == 0) { title  = parseID3Text(buf + pos + 10, fsize); found++; }

                        pos += 10 + fsize;
                        scanned++;
                    }
                }
            }
            free(buf);
        }
    }

    // ID3v2 が無い、または一部のフレームが欠けている場合は ID3v1 で補完
    if (title.length() == 0 || artist.length() == 0 || album.length() == 0) {
        readID3v1Tags(f, artist, album, title);
    }

    f.close();
}

String parseID3Text(const uint8_t* data, size_t len) {
    if (len < 1) return "";
    uint8_t encoding = data[0];
    const uint8_t* textData = data + 1;
    size_t textLen = len - 1;

    if (encoding == 0) {
        return String((const char*)textData, textLen);
    }
    else if (encoding == 3) {
        return String((const char*)textData, textLen);
    }
    else if (encoding == 1 || encoding == 2) {
        if (textLen < 2) return "";
        bool be = (textData[0] == 0xFE && textData[1] == 0xFF);
        size_t charPos = 2;
        String out = "";
        while (charPos + 1 < textLen) {
            uint16_t ch;
            if (be) {
                ch = (textData[charPos] << 8) | textData[charPos + 1];
            } else {
                ch = textData[charPos] | (textData[charPos + 1] << 8);
            }
            if (ch == 0) break;
            if (ch < 0x80) {
                out += (char)ch;
            } else if (ch < 0x800) {
                out += (char)(0xC0 | (ch >> 6));
                out += (char)(0x80 | (ch & 0x3F));
            } else {
                out += (char)(0xE0 | (ch >> 12));
                out += (char)(0x80 | ((ch >> 6) & 0x3F));
                out += (char)(0x80 | (ch & 0x3F));
            }
            charPos += 2;
        }
        return out;
    }
    return "";
}

// 簡易CSVフィールドパース（ダブルクォート対応）
static String parseCSVField(const String& line, int& pos) {
    String field = "";
    bool inQuote = false;
    while (pos < line.length()) {
        char c = line.charAt(pos);
        pos++;
        if (inQuote) {
            if (c == '\"') {
                if (pos < line.length() && line.charAt(pos) == '\"') {
                    field += '\"';
                    pos++;
                } else {
                    inQuote = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == ',') break;
            if (c == '\"') inQuote = true;
            else field += c;
        }
    }
    return field;
}

void loadIndex() {
    indexList.clear();
    playOrder.clear();
    currentTrackIdx = -1;

    File idxFile = SD.open("/index.csv");
    if (!idxFile) return;

    USER_SERIAL.println(F("[INDEX] /index.csv を読み込み中...")); USER_SERIAL.flush();
    bool headerSeen = false;
    bool corrupted  = false;
    while (idxFile.available()) {
        String line = idxFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (line.startsWith("filepath")) { headerSeen = true; continue; }

        int pos = 0;
        IndexEntry entry;
        entry.filepath = parseCSVField(line, pos);
        entry.album    = parseCSVField(line, pos);
        entry.artist   = parseCSVField(line, pos);
        entry.title    = parseCSVField(line, pos);

        if (entry.filepath.length() == 0) {
            corrupted = true;  // filepathが空 = 壊れた行
            break;
        }

        if (SD.exists(entry.filepath)) {
            indexList.push_back(entry);
        }
    }
    idxFile.close();

    if (!headerSeen || corrupted) {
        USER_SERIAL.println(F("[INDEX] index.csv が壊れています。再生成します。")); USER_SERIAL.flush();
        indexList.clear();  // 呼び出し元の「空なら自動生成」フォールバックに任せる
        return;
    }

    USER_SERIAL.printf("[INDEX] %u 曲を読み込みました。\n", indexList.size()); USER_SERIAL.flush();
    if (indexList.size() > 0) {
        USER_SERIAL.printf("[INDEX] 1曲目: title=\"%s\" file=\"%s\"\n",
                      indexList[0].title.c_str(),
                      getFilenameFromPath(indexList[0].filepath).c_str());
    }

    if (indexList.size() > 0) {
        buildPlayOrder(MODE_INDEX);
    }
}

void generateIndex() {
    USER_SERIAL.println(F("[INDEX] SDカードから index.csv を生成中...")); USER_SERIAL.flush();
    indexList.clear();
    playOrder.clear();
    currentTrackIdx = -1;

    // 先にCSVファイルを開いてヘッダを書き込む。
    // スキャン中は indexList に溜めず CSV に逐次書き込むことでメモリ使用量を抑える。
    File idxFile = SD.open("/index.csv", FILE_WRITE);
    if (!idxFile) {
        LOG_E("[INDEX] エラー: /index.csv の書き込みに失敗しました。");
        return;
    }
    idxFile.println("filepath,album,artist,title");

    std::vector<String> dirsToScan;
    dirsToScan.push_back("/");
    uint32_t count = 0;

    // ディレクトリ走査とタグ読み込みを1パスで実行（audioFiles ベクター不要）
    while (!dirsToScan.empty()) {
        String dirPath = dirsToScan.back();
        dirsToScan.pop_back();

        File dir = SD.open(dirPath);
        if (!dir || !dir.isDirectory()) { if (dir) dir.close(); continue; }

        USER_SERIAL.printf("[INDEX] フォルダをスキャン中: %s\n", dirPath.c_str());
        USER_SERIAL.flush();

        while (true) {
            File file = dir.openNextFile();
            if (!file) break;
            String name  = file.name();
            bool   isDir = file.isDirectory();
            file.close();  // ハンドルを即解放してリソースを確保

            if (isDir) {
                String subPath = dirPath;
                if (!subPath.endsWith("/")) subPath += "/";
                subPath += name;
                dirsToScan.push_back(subPath);
                continue;
            }
            if (!isAudioFile(name)) continue;

            String fullPath = dirPath;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += name;

            USER_SERIAL.printf("[INDEX] (%u) タグ読込中: %s\n", count + 1, name.c_str());
            USER_SERIAL.flush();

            // フォルダ名から artist / album を推定
            String artistFF = "Unknown";
            String albumFF  = "Unknown";
            int lastSlash = fullPath.lastIndexOf('/');
            String dp = (lastSlash > 0) ? fullPath.substring(0, lastSlash) : "/";
            if (dp != "/") {
                String rel = dp.substring(1);
                int fs = rel.indexOf('/');
                if (fs == -1) {
                    artistFF = rel;
                } else {
                    artistFF = rel.substring(0, fs);
                    String rem = rel.substring(fs + 1);
                    rem.replace("/", "");
                    if (rem.length() > 0) albumFF = rem;
                }
            }

            // タグ読み込み
            String artist = "", album = "", title = "";
            if (fullPath.endsWith(".mp3") || fullPath.endsWith(".MP3")) {
                readAllID3Tags(fullPath, artist, album, title);
            } else if (fullPath.endsWith(".m4a") || fullPath.endsWith(".M4A") ||
                       fullPath.endsWith(".mp4") || fullPath.endsWith(".MP4") ||
                       fullPath.endsWith(".aac") || fullPath.endsWith(".AAC")) {
                M4ATags m4a = readM4ATags(fullPath);
                artist = m4a.artist; album = m4a.album; title = m4a.title;
            }

            if (artist.length() == 0) artist = artistFF;
            if (album.length() == 0)  album  = albumFF;
            if (title.length() == 0) {
                title = getFilenameFromPath(fullPath);
                int dot = title.lastIndexOf('.');
                if (dot > 0) title = title.substring(0, dot);
            }

            // CSV に即書き込み（メモリに溜めない）
            idxFile.printf("%s,%s,%s,%s\n",
                csvEscape(fullPath).c_str(),
                csvEscape(album).c_str(),
                csvEscape(artist).c_str(),
                csvEscape(title).c_str());
            count++;

            if (count % 10 == 0) {
                idxFile.flush();  // 途中クラッシュ時のデータ保護
            }

            delay(1);  // WDT フィード
        }
        dir.close();
        yield();
    }

    idxFile.close();
    USER_SERIAL.printf("[INDEX] /index.csv を生成しました（%u 曲）。\n", count); USER_SERIAL.flush();

    if (count > 0) {
        loadIndex();  // CSV から読み直して indexList を構築
    }
}

void buildPlayOrder(PlayMode mode) {
    String currentFilepath = "";
    if (currentTrackIdx >= 0 && currentTrackIdx < (int)playOrder.size()) {
        int idx = playOrder[currentTrackIdx];
        if (idx >= 0 && idx < (int)indexList.size()) {
            currentFilepath = indexList[idx].filepath;
        }
    }

    playOrder.clear();
    currentMode = mode;

    size_t count = indexList.size();
    for (size_t i = 0; i < count; i++) {
        playOrder.push_back(i);
    }

    if (mode == MODE_RANDOM) {
        for (size_t i = count - 1; i > 0; i--) {
            size_t j = random(i + 1);
            int temp = playOrder[i];
            playOrder[i] = playOrder[j];
            playOrder[j] = temp;
        }
        USER_SERIAL.println("[MODE] ランダムシャッフル");
    }
    else if (mode == MODE_ARTIST) {
        std::sort(playOrder.begin(), playOrder.end(), [](int a, int b) {
            if (indexList[a].artist != indexList[b].artist) {
                return indexList[a].artist < indexList[b].artist;
            }
            if (indexList[a].album != indexList[b].album) {
                return indexList[a].album < indexList[b].album;
            }
            return indexList[a].filepath < indexList[b].filepath;
        });
        USER_SERIAL.println("[MODE] アーティスト順ソート");
    }
    else {
        USER_SERIAL.println("[MODE] INDEX順");
    }

    if (currentFilepath.length() > 0) {
        bool found = false;
        for (size_t i = 0; i < playOrder.size(); i++) {
            if (indexList[playOrder[i]].filepath == currentFilepath) {
                currentTrackIdx = i;
                found = true;
                break;
            }
        }
        if (!found) {
            currentTrackIdx = -1;
        }
    }
}

// playTrack() は stopSong()→約200ms待機→connecttoFS() という時間のかかる処理を行う。
// 音声コマンドは別タスクから発行されるため、この処理が完了する前に次の呼び出しが
// 入るとAudioライブラリの内部状態が壊れ、以降ずっと無音になる。再入を防止する。
static volatile bool s_playTrackBusy = false;

void playTrack(int orderIdx) {
    if (s_playTrackBusy) {
        LOG_E("[PLAY] 前の曲の切り替え処理中のため、この要求は無視しました");
        return;
    }
    s_playTrackBusy = true;

    if (playOrder.size() == 0) {
        LOG_E("[CMD] プレイリストが空です。先に /index を実行してください");
        s_playTrackBusy = false;
        return;
    }
    if (orderIdx < 0 || orderIdx >= (int)playOrder.size()) {
        USER_SERIAL.printf("[PLAY] 無効なトラック番号です: %d\n", orderIdx);
        s_playTrackBusy = false;
        return;
    }

    currentTrackIdx = orderIdx;
    int indexPos = playOrder[orderIdx];
    currentFile = indexList[indexPos].filepath;

    File testFile = SD.open(currentFile.c_str());
    if (!testFile) {
        LOG_E("[PLAY] ファイルを開けません: %s", currentFile.c_str());
        s_playTrackBusy = false;
        return;
    }
    size_t fileSize = testFile.size();
    testFile.close();

    USER_SERIAL.printf("[PLAY] [%d/%u] %s - %s\n", orderIdx + 1, playOrder.size(),
                        indexList[indexPos].title.c_str(), indexList[indexPos].artist.c_str());
    USER_SERIAL.flush();

    audio.stopSong();
    eofTriggered = true;  // stopSong直後のaudio.loop()でevt_eofが発火しても再帰しないようにブロック
    for (int i = 0; i < 20; i++) {
        audio.loop();
        delay(10);
    }

    eofTriggered = false;
    lastCur = 0;
    lastDur = 0;
    trackStartTime = millis();
    lastCurChangeTime = millis();
    resetReverb();  // 前の曲の残響テールが新しい曲の冒頭に漏れないようクリア

    if (!audio.connecttoFS(SD, currentFile.c_str())) {
        LOG_E("[PLAY] connecttoFS failed: %s", currentFile.c_str());
        audio.stopSong();  // 内部ファイルハンドルを確実にクリーンアップ
        isPlaying = false;
        s_playTrackBusy = false;
        return;
    }
    isPlaying = true;
    isPaused = false;
#if USE_WEB_AVATAR
    setGifFaceIndex(5);  // 再生中: lipsync neutral
#endif
    s_playTrackBusy = false;
}

void playNext() {
    if (playOrder.size() == 0) return;
    int nextIdx = currentTrackIdx + 1;
    if (nextIdx >= (int)playOrder.size()) {
        nextIdx = 0;
        LOG_I("[PLAY] 最後の曲まで再生しました。先頭に戻ります");
    }
    playTrack(nextIdx);
}

void playPrev() {
    if (playOrder.size() == 0) return;
    int prevIdx = currentTrackIdx - 1;
    if (prevIdx < 0) {
        prevIdx = playOrder.size() - 1;
        LOG_I("[PLAY] 末尾の曲に戻ります");
    }
    playTrack(prevIdx);
}

void stopPlayback() {
    audio.stopSong();
    isPlaying = false;
    isPaused = false;
    currentFile = "";
    currentTrackIdx = -1;
    eofTriggered = false;
    lastCur = 0;
    lastDur = 0;
    LOG_I("[PLAY] 再生を停止しました");
#if USE_WEB_AVATAR
    setGifFaceIndex(0);  // 停止: neutral
#endif
}

void setVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 21) vol = 21;
    currentVolume = vol;
    audio.setVolume(vol);
    LOG_I("[VOL] 音量を %d / 21 に設定しました。", vol);
}

String getModeString() {
    switch (currentMode) {
        case MODE_INDEX: return "INDEX";
        case MODE_RANDOM: return "RANDOM";
        case MODE_ARTIST: return "ARTIST";
    }
    return "INDEX";
}

void printHelp() {
    USER_SERIAL.println(F("========================================"));
        LOG_I("[HELP] ミュージックプレーヤーモード コマンド一覧");
        USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println("/play               : INDEX順で再生開始");
    USER_SERIAL.println("/random             : ランダムシャッフルで再生");
    USER_SERIAL.println("/artist             : アーティスト順でソートして再生");
    USER_SERIAL.println("/next               : 次の曲へ");
    USER_SERIAL.println("/prev               : 前の曲へ");
    USER_SERIAL.println("/index              : INDEXを生成・再生成");
    USER_SERIAL.println("/stop               : 再生停止");
    USER_SERIAL.println("/pause              : 一時停止");
    USER_SERIAL.println("/resume             : 再生再開");
    USER_SERIAL.println("/vol <0-21>         : 音量設定（0=ミュート、21=最大）");
    USER_SERIAL.println("/repeat             : リピート ON/OFF 切替");
    USER_SERIAL.println("/status             : プレイヤー状態を表示");
    USER_SERIAL.println("/playlist           : INDEX一覧を表示");
    USER_SERIAL.println("/ls [path]          : ファイル/フォルダ一覧");
    USER_SERIAL.println("/cd <folder>        : フォルダ移動");
    USER_SERIAL.println("/pwd                : 現在のフォルダを表示");
    USER_SERIAL.println("/info               : SDカード情報を表示");
    USER_SERIAL.println("/help               : このヘルプを表示");
    USER_SERIAL.println(F("========================================"));
}

void processCommand(String& cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;

    if (!cmd.startsWith("/")) {
        cmd = "/" + cmd;
    }

    unsigned long now = millis();
    if (now - lastCmdTime < CMD_DEBOUNCE_MS) {
        USER_SERIAL.println(F("⚠️ 連続入力を防止しました。少し待ってください"));
        return;
    }
    lastCmdTime = now;

    int spaceIdx = cmd.indexOf(' ');
    String command = (spaceIdx == -1) ? cmd : cmd.substring(0, spaceIdx);
    String args = (spaceIdx == -1) ? "" : cmd.substring(spaceIdx + 1);
    args.trim();
    command.toLowerCase();

    if (command == "/play") {
        if (indexList.size() == 0) {
            LOG_E("[CMD] プレイリストが空です。先に /index を実行してください");
            return;
        }
        buildPlayOrder(MODE_INDEX);
        playTrack(0);
    }
    else if (command == "/random") {
        if (indexList.size() == 0) {
            LOG_E("[CMD] プレイリストが空です。先に /index を実行してください");
            return;
        }
        buildPlayOrder(MODE_RANDOM);
        playTrack(0);
    }
    else if (command == "/artist") {
        if (indexList.size() == 0) {
            LOG_E("[CMD] プレイリストが空です。先に /index を実行してください");
            return;
        }
        buildPlayOrder(MODE_ARTIST);
        playTrack(0);
    }
    else if (command == "/next") {
        playNext();
    }
    else if (command == "/prev") {
        playPrev();
    }
    else if (command == "/index") {
        // 再生中の場合は先に停止してから生成（index.csv破損防止）
        if (isPlaying) {
            USER_SERIAL.println("[INDEX] 再生を停止してからINDEXを生成します。");
            stopPlayback();
            delay(100);
        }
        generateIndex();
    }
    else if (command == "/stop") {
        stopPlayback();
    }
    else if (command == "/pause") {
        if (isPlaying && !isPaused) {
            audio.pauseResume();
            isPaused = true;
            LOG_I("[PLAY] 一時停止しました");
#if USE_WEB_AVATAR
            setGifFaceIndex(0);  // 一時停止: neutral
#endif
        }
    }
    else if (command == "/resume") {
        if (isPlaying && isPaused) {
            audio.pauseResume();
            isPaused = false;
            LOG_I("[PLAY] 再生を再開しました");
#if USE_WEB_AVATAR
            setGifFaceIndex(5);  // 再開: lipsync neutral
#endif
        }
    }
    else if (command == "/vol") {
        if (args.length() == 0) {
            USER_SERIAL.println("使い方: /vol <0-21>");
            return;
        }
        int vol = args.toInt();
        setVolume(vol);
    }
    else if (command == "/repeat") {
        repeatMode = !repeatMode;
        LOG_I("[PLAY] リピート: %s", repeatMode ? "ON" : "OFF");
    }
    else if (command == "/status") {
        printStatus();
    }
    else if (command == "/playlist") {
        printPlaylist();
    }
    else if (command == "/info") {
        printSDInfo();
    }
    else if (command == "/ls") {
        String path = args.length() > 0 ? args : currentPath;
        listDirectory(path.c_str());
    }
    else if (command == "/cd") {
        if (args.length() == 0) {
            USER_SERIAL.println("使い方: /cd <フォルダ名>");
            return;
        }
        changeDirectory(args.c_str());
    }
    else if (command == "/pwd") {
        USER_SERIAL.println(currentPath);
    }
    else if (command == "/exit") {
        stopMusicMode();
    }
    else if (command == "/setmode") {
        if (args.length() == 0) {
            USER_SERIAL.println("使い方: /setmode <index|random|artist>");
            return;
        }
        if (args == "index") {
            buildPlayOrder(MODE_INDEX);
            USER_SERIAL.println("[MODE] INDEX順に設定");
        } else if (args == "random") {
            buildPlayOrder(MODE_RANDOM);
            USER_SERIAL.println("[MODE] ランダムに設定");
        } else if (args == "artist") {
            buildPlayOrder(MODE_ARTIST);
            USER_SERIAL.println("[MODE] アーティスト順に設定");
        } else {
            LOG_E("[CMD] 不明なモード: %s", args.c_str());
            return;
        }
        if (isPlaying && currentTrackIdx >= 0) {
            playTrack(currentTrackIdx);
        }
    }
    else if (command == "/help") {
        printHelp();
    }
    else {
        LOG_E("[CMD] 不明なコマンド: %s", command.c_str());
        USER_SERIAL.println("/help と入力するとコマンド一覧を表示します。");
    }
}

void printStatus() {
    USER_SERIAL.println(F("========================================"));
    LOG_I("[STATUS] プレイヤー状態");
    USER_SERIAL.println(F("========================================"));
    LOG_I("[STATUS] モード: %s", getModeString().c_str());
    LOG_I("[STATUS] トラック: %d / %u", currentTrackIdx + 1, playOrder.size());
    if (currentTrackIdx >= 0 && currentTrackIdx < (int)playOrder.size()) {
        int idx = playOrder[currentTrackIdx];
        LOG_I("[STATUS] 曲名: %s", indexList[idx].title.c_str());
    LOG_I("[STATUS] ファイル: %s", getFilenameFromPath(indexList[idx].filepath).c_str());
        LOG_I("[STATUS] アーティスト: %s", indexList[idx].artist.c_str());
        LOG_I("[STATUS] アルバム: %s", indexList[idx].album.c_str());
    }
    LOG_I("[STATUS] 再生中: %s", isPlaying ? "はい" : "いいえ");
    LOG_I("[STATUS] 一時停止: %s", isPaused ? "はい" : "いいえ");
    LOG_I("[STATUS] リピート: %s", repeatMode ? "ON" : "OFF");
    LOG_I("[STATUS] 音量: %d / 21", currentVolume);
    if (isPlaying && currentTrackIdx >= 0) {
        uint32_t cur = audio.getAudioCurrentTime();
        uint32_t dur = audio.getAudioFileDuration();
        LOG_I("[STATUS] 再生位置: %02d:%02d / %02d:%02d", cur / 60, cur % 60, dur / 60, dur % 60);
    }
    USER_SERIAL.println(F("========================================"));
}

void printPlaylist() {
    USER_SERIAL.println(F("========================================")); USER_SERIAL.flush();
    USER_SERIAL.println(F("[PLAYLIST] INDEX プレイリスト")); USER_SERIAL.flush();
    USER_SERIAL.println(F("========================================")); USER_SERIAL.flush();
    for (size_t i = 0; i < indexList.size(); i++) {
        bool inOrder = false;
        int orderPos = -1;
        for (size_t j = 0; j < playOrder.size(); j++) {
            if (playOrder[j] == (int)i) {
                inOrder = true;
                orderPos = j;
                break;
            }
        }
        String marker = "   ";
        if (inOrder && orderPos == currentTrackIdx) marker = "[*]";
        else if (inOrder) marker = "[ ]";

        String displayName = indexList[i].title.length() > 0 ? indexList[i].title : getFilenameFromPath(indexList[i].filepath);
        USER_SERIAL.printf("[PLAYLIST] %s %2d: %s | %s | %s\n", marker.c_str(), i, indexList[i].artist.c_str(), indexList[i].album.c_str(), displayName.c_str());
        USER_SERIAL.flush();
    }
    USER_SERIAL.println(F("========================================")); USER_SERIAL.flush();
}

void listDirectory(const char* path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        LOG_E("[FS] フォルダを開けません: %s", path);
        return;
    }
    LOG_I("[FS] フォルダ: %s", path);
    USER_SERIAL.println("-----");
    File file = dir.openNextFile();
    int count = 0;
    while (file) {
        String name = file.name();
        if (file.isDirectory()) {
            USER_SERIAL.printf("[DIR]  %s\n", name.c_str());
        } else {
            bool audioFile = isAudioFile(name);
            USER_SERIAL.printf("%s %s (%d bytes)\n",
                          audioFile ? "[MUS]" : "[FIL]",
                          name.c_str(),
                          file.size());
        }
        file = dir.openNextFile();
        count++;
        if (count > 100) {
            USER_SERIAL.println("... (100件以上は省略)");
            break;
        }
    }
    dir.close();
    USER_SERIAL.println("-----");
}

void changeDirectory(const char* dir) {
    String newPath;
    if (strcmp(dir, "..") == 0) {
        if (currentPath == "/") {
            USER_SERIAL.println("[FS] すでにルートフォルダです。");
            return;
        }
        int lastSlash = currentPath.lastIndexOf('/', currentPath.length() - 2);
        if (lastSlash <= 0) {
            newPath = "/";
        } else {
            newPath = currentPath.substring(0, lastSlash + 1);
        }
    } else {
        if (dir[0] == '/') {
            newPath = String(dir);
        } else {
            newPath = currentPath;
            if (!newPath.endsWith("/")) newPath += "/";
            newPath += dir;
        }
        if (!newPath.endsWith("/")) newPath += "/";
    }
    File test = SD.open(newPath);
    if (!test || !test.isDirectory()) {
        LOG_E("[FS] フォルダが見つかりません: %s", newPath.c_str());
        if (test) test.close();
        return;
    }
    test.close();
    currentPath = newPath;
    LOG_I("[FS] 現在のフォルダ: %s", currentPath.c_str());
}

bool isAudioFile(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".mp3") || lower.endsWith(".wav") ||
           lower.endsWith(".m4a") || lower.endsWith(".aac") ||
           lower.endsWith(".flac") || lower.endsWith(".ogg");
}

String getFullPath(const String& filename) {
    if (filename.startsWith("/")) return filename;
    String path = currentPath;
    if (!path.endsWith("/")) path += "/";
    path += filename;
    return path;
}

String getFilenameFromPath(const String& path) {
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash == -1) return path;
    return path.substring(lastSlash + 1);
}

// ============================================
// Web Interface Functions (MusicMode)
// ============================================
String getCurrentTrackName() {
    if (currentTrackIdx < 0 || currentTrackIdx >= (int)playOrder.size()) return "停止中";
    int idx = playOrder[currentTrackIdx];
    if (indexList[idx].title.length() > 0) return indexList[idx].title;
    return getFilenameFromPath(indexList[idx].filepath);
}

String getCurrentArtist() {
    if (currentTrackIdx < 0 || currentTrackIdx >= (int)playOrder.size()) return "-";
    int idx = playOrder[currentTrackIdx];
    return indexList[idx].artist;
}

String getCurrentAlbum() {
    if (currentTrackIdx < 0 || currentTrackIdx >= (int)playOrder.size()) return "-";
    int idx = playOrder[currentTrackIdx];
    return indexList[idx].album;
}

String getMusicStatusJSON() {
    String json = "{";
    json += "\"track\":\"" + getCurrentTrackName() + "\",";
    json += "\"artist\":\"" + getCurrentArtist() + "\",";
    json += "\"album\":\"" + getCurrentAlbum() + "\",";
    json += "\"playing\":" + String(isPlaying ? "true" : "false") + ",";
    json += "\"paused\":" + String(isPaused ? "true" : "false") + ",";
    json += "\"mode\":\"" + getModeString() + "\",";
    json += "\"volume\":" + String(currentVolume);
    json += "}";
    return json;
}

void handleWebMusicCommand(const String& cmd) {
    String mutableCmd = cmd;
    processCommand(mutableCmd);
}

// ============================================
// M4A/MP4 iTunes Metadata Reader (ilst atom)
// ============================================
static uint32_t mp4BoxSize(const uint8_t* buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
}

static void mp4BoxType(const uint8_t* buf, char* type) {
    memcpy(type, buf, 4);
    type[4] = '\0';
}

// [f.position(), rangeEnd) の範囲内（同階層のみ、再帰しない）から targetType の box を探す。
// 見つかった場合 f はその box の中身の先頭（ヘッダ8byte直後）に位置し、true を返す。
static bool findMp4Box(File& f, uint32_t rangeEnd, const char* targetType, uint32_t& outSize) {
    uint8_t buf[8];
    while (f.position() + 8 <= rangeEnd) {
        uint32_t startPos = f.position();
        if (f.read(buf, 8) != 8) return false;
        uint32_t size = mp4BoxSize(buf);
        char type[5];
        mp4BoxType(buf + 4, type);

        if (size < 8) return false;  // size=0(末尾まで)/size=1(64bit拡張)は非対応として打ち切り

        if (strcmp(type, targetType) == 0) {
            outSize = size;
            return true;
        }

        uint32_t next = startPos + size;
        if (next <= startPos || next > rangeEnd) return false;
        if (!f.seek(next)) return false;
    }
    return false;
}

M4ATags readM4ATags(const String& filepath) {
    M4ATags tags;
    File f = SD.open(filepath);
    if (!f) {
        LOG_E("[M4A] Open failed: %s", filepath.c_str());
        return tags;
    }

    uint32_t fileSize = f.size();
    uint32_t boxSize;

    // ilst は moov > udta > meta > ilst と3階層ネストされている。
    // 各階層を順に降りていく（トップレベルだけ見ても絶対に見つからない）。
    if (!findMp4Box(f, fileSize, "moov", boxSize)) {
        LOG_I("[M4A] moov not found in %s", filepath.c_str());
        f.close();
        return tags;
    }
    uint32_t moovEnd = (f.position() - 8) + boxSize;

    if (!findMp4Box(f, moovEnd, "udta", boxSize)) {
        LOG_I("[M4A] udta not found in %s", filepath.c_str());
        f.close();
        return tags;
    }
    uint32_t udtaEnd = (f.position() - 8) + boxSize;

    if (!findMp4Box(f, udtaEnd, "meta", boxSize)) {
        LOG_I("[M4A] meta not found in %s", filepath.c_str());
        f.close();
        return tags;
    }
    uint32_t metaEnd = (f.position() - 8) + boxSize;
    f.seek(f.position() + 4);  // meta は FullBox: version(1)+flags(3) の4byteを読み飛ばす

    if (!findMp4Box(f, metaEnd, "ilst", boxSize)) {
        LOG_I("[M4A] ilst not found in %s", filepath.c_str());
        f.close();
        return tags;
    }
    uint32_t ilstEnd = (f.position() - 8) + boxSize;

    LOG_I("[M4A] ilst found at %d, end %d", (int)f.position(), (int)ilstEnd);

    // ilst 内の各 item box をスキャン
    uint8_t buf[8];
    while (f.position() + 8 <= ilstEnd) {
        uint32_t itemStart = f.position();
        if (f.read(buf, 8) != 8) break;
        uint32_t itemSize = mp4BoxSize(buf);
        char itemType[5];
        mp4BoxType(buf + 4, itemType);

        if (itemSize < 8 || itemStart + itemSize > ilstEnd) break;
        uint32_t itemEnd = itemStart + itemSize;
        String value = "";

        // item box 内をスキャンして data box を探す
        while (f.position() + 8 <= itemEnd) {
            uint32_t childStart = f.position();
            if (f.read(buf, 8) != 8) break;
            uint32_t childSize = mp4BoxSize(buf);
            char childType[5];
            mp4BoxType(buf + 4, childType);

            if (childSize < 8 || childStart + childSize > itemEnd) break;

            if (strcmp(childType, "data") == 0 && childSize >= 16) {
                uint8_t dataHdr[8];
                f.read(dataHdr, 8); // version/flags + reserved
                uint32_t textLen = childSize - 16;
                if (textLen > 0 && textLen < 512) {
                    char text[512];
                    int rd = f.read((uint8_t*)text, textLen);
                    if (rd > 0) {
                        text[rd] = '\0';
                        value = String(text);
                    }
                }
            }
            f.seek(childStart + childSize);
        }

        // itemType の判定 (iTunes metadata atoms)
        if (itemType[0] == (char)0xA9) {
            if (strcmp(itemType + 1, "nam") == 0) tags.title = value;
            else if (strcmp(itemType + 1, "ART") == 0) tags.artist = value;
            else if (strcmp(itemType + 1, "alb") == 0) tags.album = value;
        }

        f.seek(itemEnd);
    }

    f.close();
    return tags;
}

#endif // USE_SD
