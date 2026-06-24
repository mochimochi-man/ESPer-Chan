// config.h - ESPer-Chan Configuration File
// Web設定 (/apコマンド) で上書き可能な項目あり

#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// プロジェクト情報
// ============================================
#define PROJECT_NAME "ESPer-Chan"
#define PROJECT_VERSION "Build 312"
#define SPEAK_NAME "エスパーちゃん"

// ============================================
// システムプロンプト
// ============================================
#define SYSTEM_PROMPT "あなたは「" SPEAK_NAME "」という名前の小さな可愛いAIアシスタントです。親しみやすくて少しお茶目な性格です。短くて分かりやすい日本語で返答してください。"

// ============================================
// 【重要】シリアル通信方式の設定
// ============================================
#define USE_USB_CDC 0 //タクトスイッチを使うときは1にする必要がある

// ============================================
// モード切り替えボタン (USE_USB_CDC=1 時 GPIO43 が空く)
// ============================================
#if USE_USB_CDC
#define BTN_MODE_PIN 43
#endif

// ============================================
// シリアル通信設定
// ============================================
#define SERIAL_BAUD 115200

// ============================================
// WiFi設定
// ============================================
#define WIFI_SSID "YOUR_SSID"          // ここにWiFiのSSIDを入力
#define WIFI_PASSWORD "YOUR_PASSWORD"  // ここにWiFiのパスワードを入力
#define WIFI_TIMEOUT_MS 30000
#define WIFI_RECONNECT_INTERVAL_MS 5000

// ============================================
// LM Studio サーバー設定 (LLM)
// ============================================
#define LMSTUDIO_HOST "YOUR_LM_IP"  // LM StudioサーバーのIPアドレス
#define LMSTUDIO_PORT 1234
#define LMSTUDIO_PATH "/v1/chat/completions"
#define LMSTUDIO_API_KEY "lm-studio"

// ============================================
// whisper.cpp STTサーバー設定
// ============================================
#define WHISPER_HOST "YOUR_WHISPER_IP"  // whisper.cppサーバーのIPアドレス
#define WHISPER_PORT 8080
#define WHISPER_PATH "/inference"
#define WHISPER_LANGUAGE "ja"

// ============================================
// TTSサーバー設定
// ============================================
// --- VOICEVOX設定 ---
#define TTS_HOST "YOUR_TTS_IP"  // VOICEVOXサーバーのIPアドレス
#define TTS_PORT 50021
#define TTS_SPEAKER_ID 2

// ============================================
// モデル設定
// ============================================
// 使用するモデルはあらかじめLM Studioでダウンロードしておいてください
// 例 : #define MODEL_NAME      "phi-4-mini-instruct"
// 例 : #define MODEL_NAME      "qwen3-1.7b"
// 例 : #define MODEL_NAME      "gemma-4-e2b"
#define MODEL_NAME "lfm2.5-1.2b-instruct"
#define MAX_TOKENS 512
#define TEMPERATURE 0.1
#define TOP_K 50
#define TOP_P 0.1
#define REPEAT_PENALTY 1.05

// ============================================
// 天気予報設定
// ============================================
#define WEATHER_CITY "北見"  // デフォルト地域名（primary_area.xmlのcity名）
#define WEATHER_API_HOST "weather.tsukumijima.net"
#define WEATHER_API_PATH "/api/forecast"

// 地域名→IDテーブル（primary_area.xml準拠、140都市）
struct WeatherCity {
  const char* name;
  const char* id;
};

static const WeatherCity WEATHER_CITIES[] = {
  { "札幌", "011000" }, { "北見", "012010" }, { "釧路", "012020" }, { "帯広", "012030" }, { "函館", "013010" }, { "江差", "013020" }, { "室蘭", "013030" }, { "浦河", "014010" }, { "苫小牧", "014020" }, { "日高", "014030" }, { "岩見沢", "015010" }, { "留萌", "015020" }, { "稚内", "016010" }, { "倶知安", "016020" }, { "網走", "017010" }, { "紋別", "017030" }, { "青森", "020010" }, { "むつ", "020020" }, { "八戸", "020030" }, { "盛岡", "030010" }, { "宮古", "030020" }, { "大船渡", "030030" }, { "仙台", "040010" }, { "白石", "040020" }, { "秋田", "050010" }, { "横手", "050020" }, { "山形", "060010" }, { "米沢", "060020" }, { "酒田", "060030" }, { "新庄", "060040" }, { "福島", "070010" }, { "小名浜", "070020" }, { "若松", "070030" }, { "水戸", "080010" }, { "土浦", "080020" }, { "宇都宮", "090010" }, { "大田原", "090020" }, { "前橋", "100010" }, { "みなかみ", "100020" }, { "さいたま", "110010" }, { "熊谷", "110020" }, { "秩父", "110030" }, { "千葉", "120010" }, { "銚子", "120020" }, { "館山", "120030" }, { "東京", "130010" }, { "大島", "130020" }, { "八丈島", "130030" }, { "父島", "130040" }, { "横浜", "140010" }, { "小田原", "140020" }, { "新潟", "150010" }, { "長岡", "150020" }, { "高田", "150030" }, { "相川", "150040" }, { "富山", "160010" }, { "伏木", "160020" }, { "金沢", "170010" }, { "輪島", "170020" }, { "福井", "180010" }, { "敦賀", "180020" }, { "甲府", "190010" }, { "河口湖", "190020" }, { "長野", "200010" }, { "松本", "200020" }, { "諏訪", "200030" }, { "岐阜", "210010" }, { "高山", "210020" }, { "静岡", "220010" }, { "網代", "220020" }, { "三島", "220030" }, { "浜松", "220040" }, { "名古屋", "230010" }, { "豊橋", "230020" }, { "津", "240010" }, { "尾鷲", "240020" }, { "大津", "250010" }, { "彦根", "250020" }, { "京都", "260010" }, { "舞鶴", "260020" }, { "大阪", "270000" }, { "神戸", "280010" }, { "豊岡", "280020" }, { "奈良", "290010" }, { "風屋", "290020" }, { "和歌山", "300010" }, { "串本", "300020" }, { "鳥取", "310010" }, { "米子", "310020" }, { "松江", "320010" }, { "浜田", "320020" }, { "西郷", "320030" }, { "岡山", "330010" }, { "津山", "330020" }, { "広島", "340010" }, { "庄原", "340020" }, { "下関", "350010" }, { "山口", "350020" }, { "柳井", "350030" }, { "萩", "350040" }, { "徳島", "360010" }, { "日和佐", "360020" }, { "高松", "370010" }, { "小豆島", "370020" }, { "松山", "380010" }, { "新居浜", "380020" }, { "宇和島", "380030" }, { "高知", "390010" }, { "室戸", "390020" }, { "清水", "390030" }, { "福岡", "400010" }, { "八幡", "400020" }, { "飯塚", "400030" }, { "久留米", "400040" }, { "佐賀", "410010" }, { "伊万里", "410020" }, { "長崎", "420010" }, { "佐世保", "420020" }, { "厳原", "420030" }, { "福江", "420040" }, { "熊本", "430010" }, { "阿蘇", "430020" }, { "人吉", "430030" }, { "高森", "430040" }, { "大分", "440010" }, { "中津", "440020" }, { "日田", "440030" }, { "佐伯", "440040" }, { "宮崎", "450010" }, { "延岡", "450020" }, { "都城", "450030" }, { "高千穂", "450040" }, { "鹿児島", "460010" }, { "鹿屋", "460020" }, { "種子島", "460030" }, { "名瀬", "460040" }, { "沖縄", "471000" }, { "南大東", "472000" }, { "宮古島", "473000" }, { "石垣島", "474000" }
};

static const size_t WEATHER_CITY_COUNT = sizeof(WEATHER_CITIES) / sizeof(WEATHER_CITIES[0]);

// ============================================
// LED設定 (microSD使用時は無効化: GPIO21がCSと競合)
// ============================================
#define USE_LED 0  // 1: LED有効, 0: LED無効 (microSD使用時は0)
#define LED_PIN 21
#define LED_BRIGHTNESS 128

// ============================================
// ディスプレイタイプ設定
// ============================================
#define DISPLAY_SSD1306 1          // SSD1306 128x64 OLED (I2C)
#define DISPLAY_ST7789_240X320 2   // ST7789 240x320 TFT (SPI)
#define DISPLAY_ST7789_240X240 3   // ST7789 240x240 TFT (SPI)
#define DISPLAY_ST7735S_128X128 4  // ST7735S 128x128 TFT (SPI)

// ★ここで使用するディスプレイを選択
#define DISPLAY_TYPE DISPLAY_ST7735S_128X128

// ピン定義: 全ディスプレイタイプで共通使用（ランタイム選択対応）
// SSD1306 と TFT は GPIO5/GPIO6 を排他共用 (SDA/CS, SCL/RST)
// ハードウェア接続はいずれか一方のみ可能
#define OLED_SDA_PIN 5   // GPIO5 (SSD1306 SDA)
#define OLED_SCL_PIN 6   // GPIO6 (SSD1306 SCL)
#define TFT_SCK_PIN 7    // SPI2 SCK (SD共有)
#define TFT_MOSI_PIN 9   // SPI2 MOSI (SD共有)
#define TFT_MISO_PIN -1  // 未使用
#define TFT_DC_PIN 4     // GPIO4
#define TFT_CS_PIN 5     // GPIO5 (SSD1306 SDAと排他)
#define TFT_RST_PIN 6    // GPIO6 (SSD1306 SCLと排他)
#define TFT_BL_PIN -1    // バックライト (-1=常時ON)

// ディスプレイタイプ別パラメータ（コンパイル時デフォルト値）
#if DISPLAY_TYPE == DISPLAY_SSD1306
#define TFT_WIDTH 128
#define TFT_HEIGHT 64
#define TFT_IS_COLOR false
#define TFT_INVERT false
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_ROTATION 2
#define AVATAR_POS_X 0
#define AVATAR_POS_Y 0
#define AVATAR_X_OFFSET 0
#define AVATAR_Y_OFFSET 0
#define AVATAR_SCALE_DEFAULT 0.40f   // StackChan デフォルト顔
#define AVATAR_SCALE_ORIGINAL 0.30f  // ESPer-Chan オリジナル顔
#elif DISPLAY_TYPE == DISPLAY_ST7789_240X320
#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define TFT_IS_COLOR true
#define TFT_INVERT true
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_ROTATION 3
#define AVATAR_POS_X 0
#define AVATAR_POS_Y 0
#define AVATAR_X_OFFSET 0
#define AVATAR_Y_OFFSET 0
#define AVATAR_SCALE_DEFAULT 1.00f   // StackChan デフォルト顔
#define AVATAR_SCALE_ORIGINAL 1.00f  // ESPer-Chan オリジナル顔
#elif DISPLAY_TYPE == DISPLAY_ST7735S_128X128
#define TFT_WIDTH 128
#define TFT_HEIGHT 128
#define TFT_IS_COLOR true
#define TFT_INVERT false
#define TFT_OFFSET_X 2
#define TFT_OFFSET_Y 1
#define TFT_ROTATION 1
#define AVATAR_POS_X 0
#define AVATAR_POS_Y 0
#define AVATAR_X_OFFSET 0
#define AVATAR_Y_OFFSET 0
#define AVATAR_SCALE_DEFAULT 0.45f   // StackChan デフォルト顔
#define AVATAR_SCALE_ORIGINAL 0.50f  // ESPer-Chan オリジナル顔
#elif DISPLAY_TYPE == DISPLAY_ST7789_240X240
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define TFT_IS_COLOR true
#define TFT_INVERT true
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0   // 表示が上下にズレる基板の場合は80前後に調整
#define TFT_ROTATION 3
#define AVATAR_POS_X 0
#define AVATAR_POS_Y 0
#define AVATAR_X_OFFSET 0
#define AVATAR_Y_OFFSET 0
#define AVATAR_SCALE_DEFAULT 0.90f   // StackChan デフォルト顔
#define AVATAR_SCALE_ORIGINAL 0.90f  // ESPer-Chan オリジナル顔
#endif

// ============================================
// Avatar設定
// ============================================
#define USE_AVATAR 1       // 1: M5Stack Avatar有効, 0: 無効
// オリジナル顔(ESPer-Chan)のコードをビルドに含めるかどうかの下位スイッチ。常に1にすること。
// 実際にどちらの顔を表示するかはWeb設定の「顔タイプ」(appSettings.faceType)で実行時に選択する。
// 0にするとappSettings.faceType==1の分岐がcustomFace未定義でビルドエラーになる。
#define USE_CUSTOM_FACE 1

// リップシンク設定
#define LIPSYNC_SPEED_MS 50            // リップシンク更新間隔(ms)
#define LIPSYNC_MIN_DURATION_MS 1000   // 最小リップシンク時間(ms)
#define LIPSYNC_MAX_DURATION_MS 10000  // 最大リップシンク時間(ms)

// ============================================
// Phase 2: 音声設定
// ============================================
#define USE_VOICE_INPUT 1
#define USE_VOICE_OUTPUT 1

// ---- マイクタイプ選択 ----
#define MIC_TYPE_PDM 0  // Xiao ESP32S3 Sense 内蔵PDMマイク
#define MIC_TYPE_I2S 1  // INMP441 I2Sマイク

// ★ここでマイクタイプを選択
#define MIC_TYPE MIC_TYPE_PDM

// PDMマイク ピン設定 (Xiao ESP32S3 Sense)
#if MIC_TYPE == MIC_TYPE_PDM
#define MIC_DATA_PIN 41  // PDM DATA (GPIO41)
#define MIC_CLK_PIN 42   // PDM CLK  (GPIO42)
#endif

// INMP441 I2Sマイク ピン設定
// 接続: WS=GPIO15, SCK=GPIO16, SD=GPIO17, VDD=3.3V, GND=GND, L/R=GND(左ch)
#if MIC_TYPE == MIC_TYPE_I2S
#define MIC_WS_PIN 15   // LRCLK / WS
#define MIC_SCK_PIN 16  // BCK   / SCK
#define MIC_SD_PIN 17   // DATA  / SD
#endif

// 共通マイク設定
#define MIC_SAMPLE_RATE 16000
#define MIC_BITS 16
#define MIC_CHANNELS 1
#define MIC_BUFFER_MS 500
#define MIC_GAIN 4.0f  // 増幅率 (1.0=等倍、4.0=4倍。遠距離認識改善)

// ---- I2S DAC設定 (MAX98357A) ----
#define I2S_BCLK_PIN 1
#define I2S_LRC_PIN 2
#define I2S_DOUT_PIN 3
#define I2S_SAMPLE_RATE 24000
#define I2S_BITS 16
#define I2S_CHANNELS 1

// TTS(しゃべり声)の出力ゲイン。1.0=そのまま、1.3=約30%増量。
// 上げすぎると音割れするので2.0程度を上限目安に。
#define TTS_VOLUME_GAIN 1.8f

// ---- VAD設定 ----
#define VAD_THRESHOLD 300  // raw ADC値。MIC_GAIN前の生値と比較
#define VAD_SILENCE_MS 1000
#define VAD_MIN_SPEECH_MS 500

// ============================================
// システムモード定義 (MusicMode統合)
// ============================================
enum SystemMode {
  MODE_AGENT,
  MODE_MUSIC,
  MODE_CAMERA
};
extern SystemMode systemMode;

// ============================================
// ミュージックプレーヤー Settings (MusicMode Integration)
// ============================================
#define SD_CS_PIN 21
#define SD_SCK_PIN 7
#define SD_MISO_PIN 8
#define SD_MOSI_PIN 9
#define DEFAULT_VOLUME 18  // 0~21 (旧:15)

// Compile-time pin conflict check
#if (I2S_BCLK_PIN == SD_SCK_PIN) || (I2S_BCLK_PIN == SD_MISO_PIN) || (I2S_BCLK_PIN == SD_MOSI_PIN) || (I2S_BCLK_PIN == SD_CS_PIN)
#error "I2S_BCLK_PIN conflicts with SD card pin!"
#endif
#if (I2S_LRC_PIN == SD_SCK_PIN) || (I2S_LRC_PIN == SD_MISO_PIN) || (I2S_LRC_PIN == SD_MOSI_PIN) || (I2S_LRC_PIN == SD_CS_PIN)
#error "I2S_LRC_PIN conflicts with SD card pin!"
#endif
#if (I2S_DOUT_PIN == SD_SCK_PIN) || (I2S_DOUT_PIN == SD_MISO_PIN) || (I2S_DOUT_PIN == SD_MOSI_PIN) || (I2S_DOUT_PIN == SD_CS_PIN)
#error "I2S_DOUT_PIN conflicts with SD card pin!"
#endif

// ============================================
// ウェイクワード設定
// ============================================
#define WAKEWORD_ENABLED 1
#define WAKEWORD_TEXT "ねぇねぇ"
#define WAKEWORD_RECORD_MS 1200     // ウェイクワード録音時間(ms)
#define WAKEWORD_MIN_ENERGY 0.008f  // 音声ブロック最小エネルギー
#define WAKEWORD_MAX_ENERGY 0.4f    // 音声ブロック最大エネルギー（ノイズ除去）

// ============================================
// HTTP通信設定
// ============================================
#define HTTP_TIMEOUT_MS 30000
#define HTTP_BUFFER_SIZE 4096
#define JSON_BUFFER_SIZE 8192

// ============================================
// デバッグ設定
// ============================================
#define DEBUG_LEVEL 1

// ============================================
// Web Avatar Server設定
// ============================================
#define USE_WEB_AVATAR 1  // 1: 動的GIFサーバー有効, 0: 無効

// ============================================
// SDカード設定（ミュージックモード）
// ============================================
#define USE_SD 1  // 1: SDカード＋ミュージックモード有効, 0: 無効

// ============================================
// ウェブカメラ設定（MediaPipe統合）
// ============================================
#define USE_CAMERA 1  // 1: ウェブカメラ機能有効, 0: 無効

// --- 顔認識率しきい値 ---
// ブラウザ側MediaPipeの検出スコア(%)がこの値を超えたら物理ディスプレイの顔を笑顔にする
#define DEFAULT_FACE_THRESHOLD 80

// ============================================
// その他設定
// ============================================
#define MAX_MESSAGE_HISTORY 5
#define MESSAGE_MAX_LENGTH 512
#define WATCHDOG_TIMEOUT_MS 30000

// ============================================
// 音声コマンドキーワードテーブル
// ============================================
// 天気コマンド: 3日分 x 最大4パターン
// dayIndex: 0=今日, 1=明日, 2=明後日
#define MAX_WEATHER_PATTERNS 4
static const char* WEATHER_KEYWORDS[3][MAX_WEATHER_PATTERNS] = {
  // 今日 (dayIndex=0)
  { "今日の天気", "今日の天気を教えて", "本日の天気", "本日の天気を教えて" },
  // 明日 (dayIndex=1)
  { "明日の天気", "明日の天気を教えて", "明日の天気は", "あしたの天気" },
  // 明後日 (dayIndex=2)
  { "明後日の天気", "明後日の天気を教えて", "あさっての天気", "明後日の天気は" }
};

#define MAX_MUSIC_KEYWORDS 8
static const char* MUSIC_KEYWORDS[MAX_MUSIC_KEYWORDS] = {
  "ミュージックプレイヤーを起動",
  "ミュージックプレイヤー",
  "音楽を再生",
  "音楽をかけて",
  "音楽かけて",
  "ミュージックモード",
  "ミュージックモードを起動",
  "音楽スタート"
};

// ============================================
// ウェブカメラ音声コマンドキーワードテーブル
// ============================================
#define MAX_CAMERA_KEYWORDS 4
static const char* CAMERA_KEYWORDS[MAX_CAMERA_KEYWORDS] = {
  "カメラモードを起動",
  "カメラ",
  "カメラモード",
  "カメラを起動"
};

// ============================================
// 時刻音声コマンドキーワードテーブル
// ============================================
#define MAX_TIME_KEYWORDS 4
static const char* TIME_KEYWORDS[MAX_TIME_KEYWORDS] = {
  "現在の時刻",
  "現在の時間",
  "今何時?",
  "今何時"
};

// ============================================
// 感情反応ワードテーブル（各8個まで）
// ============================================
#define MAX_EMOTION_WORDS 8

// 笑い
static const char* EMOTION_HAPPY_WORDS[MAX_EMOTION_WORDS] = {
  "元気", "楽しい", "嬉しい", "やったー", "わーい", "最高", "幸せ", "うれしい"
};
// 悲しみ
static const char* EMOTION_SAD_WORDS[MAX_EMOTION_WORDS] = {
  "悲", "辛", "寂しい", "苦しい", "泣", "さびしい", "かなしい", "くるしい"
};
// 怒り
static const char* EMOTION_ANGRY_WORDS[MAX_EMOTION_WORDS] = {
  "怒", "イライラ", "むかつく", "許せない", "バカ", "もう限界", "ふざける", "くそ"
};
// 焦り
static const char* EMOTION_PANIC_WORDS[MAX_EMOTION_WORDS] = {
  "焦", "慌", "パニック", "やばい", "まずい", "大変", "困った", "どうしよう"
};
// 驚き
static const char* EMOTION_SURPRISED_WORDS[MAX_EMOTION_WORDS] = {
  "驚", "びっくり", "えっ", "マジ", "嘘", "信じられない", "へえ", "へぇ"
};


// ============================================
// USER_SERIAL (全ファイル共通)
// ============================================
// USE_USB_CDCの値に関わらず、常にネイティブUSB CDC(USBSerial)を使う。
// (USE_USB_CDCはBTN_MODE_PIN/タクトスイッチ機能の有無のみを切り替える)
#include "USB.h"
#include "USBCDC.h"
extern USBCDC USBSerial;
#define USER_SERIAL USBSerial

// ============================================
// デバッグログマクロ（全ファイル共通）
// ============================================
inline void debugPrint(const char* prefix, const char* fmt, ...) {
  if (fmt == NULL || fmt[0] == ' ') return;
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  USER_SERIAL.print(prefix);
  USER_SERIAL.println(buf);
}

#if DEBUG_LEVEL >= 1
#define LOG_E(fmt, ...) debugPrint("[ERR] ", fmt, ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...)
#endif
#if DEBUG_LEVEL >= 2
#define LOG_I(fmt, ...) debugPrint("[INF] ", fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...)
#endif
#if DEBUG_LEVEL >= 3
#define LOG_D(fmt, ...) debugPrint("[DBG] ", fmt, ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...)
#endif

// ============================================
// ブートモード定数（NVS保存用）
// ============================================
#define BOOT_MODE_NORMAL 0
#define BOOT_MODE_CAMERA 1

#endif  // CONFIG_H
