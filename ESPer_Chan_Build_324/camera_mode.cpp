#include "camera_mode.h"
#include "config.h"
#include "settings.h"
#include "Web_Avatar_html.h"
#include "camera_face_display.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESP_I2S.h>
#include <ArduinoJson.h>

#if USE_CAMERA

#include <esp_camera.h>
#include <esp_heap_caps.h>

// ============================================
// XIAO ESP32S3 Sense Camera Pins
// ============================================
#define CAM_PWDN_GPIO     -1
#define CAM_RESET_GPIO    -1
#define CAM_XCLK_GPIO     10
#define CAM_SIOD_GPIO     40
#define CAM_SIOC_GPIO     39
#define CAM_Y9_GPIO       48
#define CAM_Y8_GPIO       11
#define CAM_Y7_GPIO       12
#define CAM_Y6_GPIO       14
#define CAM_Y5_GPIO       16
#define CAM_Y4_GPIO       18
#define CAM_Y3_GPIO       17
#define CAM_Y2_GPIO       15
#define CAM_VSYNC_GPIO    38
#define CAM_HREF_GPIO     47
#define CAM_PCLK_GPIO     13

volatile int g_faceDetectedCount = 0;

extern WebServer webServer;
extern SystemMode systemMode;
extern AppSettings appSettings;
extern bool wifiConnected;

extern void setupLED();
extern void setLED(uint8_t b);

static String cameraSerialBuffer = "";

// ============================================
// カメラ初期化（ブートモード専用）
// ============================================
static bool initCamera() {
    LOG_I("[CAM] Initializing camera...");

    // ============================================
    // 【Build310】物理ディスプレイ表示のためM5.begin()(initCameraFaceDisplay内)を
    // 呼ぶようになった。M5.begin()はESP32-S3では内部I2C(In_I2C)としてI2C_NUM_1を
    // 確保するため、このままだとesp_camera_init()のSCCB用i2c_driver_installが
    // I2C_NUM_1で衝突しESP_ERR_INVALID_STATE(0x103)で失敗する。
    // initCameraFaceDisplay()内でM5.begin()直後にM5.In_I2C.release()してから
    // 本関数を呼ぶこと。
    // ============================================

    // ============================================
    // カメラ設定
    // ============================================
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_pwdn = CAM_PWDN_GPIO;
    config.pin_reset = CAM_RESET_GPIO;
    config.pin_xclk = CAM_XCLK_GPIO;

    // 注: pin_sccb_sdaが-1以外の場合、sccb_i2c_portはesp32-cameraライブラリ内部で
    // 無視される(ライブラリが新規I2Cバスを自前で作成するため)。ポート競合の回避は
    // initCameraFaceDisplay()側でM5.begin()が掴んだI2C_NUM_1を解放することで行う。
    config.pin_sccb_sda = CAM_SIOD_GPIO;
    config.pin_sccb_scl = CAM_SIOC_GPIO;
    config.sccb_i2c_port = -1;

    config.pin_d7 = CAM_Y9_GPIO;
    config.pin_d6 = CAM_Y8_GPIO;
    config.pin_d5 = CAM_Y7_GPIO;
    config.pin_d4 = CAM_Y6_GPIO;
    config.pin_d3 = CAM_Y5_GPIO;
    config.pin_d2 = CAM_Y4_GPIO;
    config.pin_d1 = CAM_Y3_GPIO;
    config.pin_d0 = CAM_Y2_GPIO;
    config.pin_vsync = CAM_VSYNC_GPIO;
    config.pin_href = CAM_HREF_GPIO;
    config.pin_pclk = CAM_PCLK_GPIO;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    if (psramFound()) {
        config.jpeg_quality = 10;
        config.fb_count = 2;
        config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        LOG_E("[CAM] Camera init failed: 0x%x", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
    }
    LOG_I("[CAM] Camera initialized");
    return true;
}

static void stopCamera() {
    esp_camera_deinit();
    LOG_I("[CAM] Camera stopped");
}

// ============================================
// WebServer（カメラブートモード）
// ============================================
void initCameraWebServer() {
    webServer.on("/", HTTP_GET, []() {
        webServer.send(200, "text/html", FPSTR(CAMERA_HTML));
    });

    webServer.on("/api/mode", HTTP_GET, []() {
        webServer.send(200, "text/plain", "camera");
    });

    webServer.on("/capture", HTTP_GET, []() {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            webServer.send(500, "text/plain", "Camera capture failed");
            return;
        }
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        bool converted = frame2jpg(fb, 85, &jpg_buf, &jpg_len);
        esp_camera_fb_return(fb);
        if (!converted || !jpg_buf || jpg_len == 0) {
            webServer.send(500, "text/plain", "JPEG conversion failed");
            return;
        }
        webServer.setContentLength(jpg_len);
        webServer.send(200, "image/jpeg", "");
        webServer.client().write(jpg_buf, jpg_len);
        free(jpg_buf);
    });

    // 顔検出結果JSON: countと各顔のscore(信頼度0.0-1.0)を受け取り、
    // 最大scoreが顔認識率しきい値(appSettings.faceThreshold, %)を超えていれば
    // 物理ディスプレイの顔を笑顔に切り替える。
    webServer.on("/face", HTTP_POST, []() {
        String body = webServer.arg("plain");
        if (body.length() > 0) {
            USER_SERIAL.println(body);
            DynamicJsonDocument doc(2048);
            if (deserializeJson(doc, body) == DeserializationError::Ok) {
                int count = doc["count"] | 0;
                float maxScore = 0.0f;
                for (JsonObject f : doc["faces"].as<JsonArray>()) {
                    float s = f["score"] | 0.0f;
                    if (s > maxScore) maxScore = s;
                }
                g_faceDetectedCount = count;
                bool smile = (count > 0) && (maxScore * 100.0f >= (float)appSettings.faceThreshold);
                setCameraFaceExpression(smile);
            }
        } else {
            g_faceDetectedCount = 0;
            setCameraFaceExpression(false);
        }
        webServer.send(200, "application/json", "{\"ok\":true}");
    });

    // フロントエンドが取得する送信間隔（秒）
    webServer.on("/interval", HTTP_GET, []() {
        webServer.send(200, "text/plain", "5");
    });

    webServer.on("/api/camera/exit", HTTP_POST, []() {
        webServer.send(200, "text/plain", "OK");
        stopCameraMode();
    });

    webServer.begin();
}

// ============================================
// カメラ専用ブートモード
// ============================================
void setupCameraBootMode() {
    USER_SERIAL.println();
    USER_SERIAL.println(F("========================================"));
    USER_SERIAL.println(F("  Camera Boot Mode"));
    USER_SERIAL.println(F("========================================"));

    setupLED();
#ifdef BTN_MODE_PIN
    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
#endif

    // WiFi接続（NVSに保存された設定を使用）
    wifiConnected = false;
    if (strlen(appSettings.wifiSsid) > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(appSettings.wifiSsid, appSettings.wifiPassword);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
            delay(500);
            USER_SERIAL.print(".");
            setLED((millis() / 250) % 2 ? LED_BRIGHTNESS : 0);
        }
        if (WiFi.status() == WL_CONNECTED) {
            USER_SERIAL.println();
            USER_SERIAL.print(F("[BOOT] WiFi OK IP:"));
            USER_SERIAL.println(WiFi.localIP());
            wifiConnected = true;
            setLED(LED_BRIGHTNESS);
        } else {
            USER_SERIAL.println();
            USER_SERIAL.println(F("[BOOT] WiFi failed. Camera mode requires WiFi."));
            setLED(0);
        }
    }

    // 物理ディスプレイに顔GIFを表示開始（M5.begin()はここで実行される）。
    // 内部でI2C_NUM_1を解放してからカメラ初期化に進むため、必ずinitCamera()より先に呼ぶ。
    initCameraFaceDisplay();

    // カメラ初期化
    if (!initCamera()) {
        USER_SERIAL.println(F("[BOOT] Camera init failed. Rebooting to normal mode..."));
        appSettings.bootMode = BOOT_MODE_NORMAL;
        saveSettings();
        delay(1000);
        ESP.restart();
        return;
    }

    // WebServer
    initCameraWebServer();

    USER_SERIAL.println(F("[BOOT] Camera mode ready."));
    USER_SERIAL.println(F("  /exit = Return to normal mode"));
    USER_SERIAL.println(F("========================================"));
}

void loopCameraBootMode() {
    webServer.handleClient();

    // シリアル: /exit のみ受け付ける。出力は一切しない（JSONフィード専用）
    while (USER_SERIAL.available()) {
        char c = USER_SERIAL.read();
        if (c == '\n' || c == '\r') {
            if (cameraSerialBuffer.length() > 0) {
                String cmd = cameraSerialBuffer;
                cameraSerialBuffer = "";
                cmd.trim();
                cmd.toLowerCase();
                if (cmd == "/exit" || cmd == "exit") {
                    stopCameraMode();  // 再起動して通常モードへ（この行以降は実行されない）
                }
                // その他のコマンドは無視（出力なし）
            }
        } else if (cameraSerialBuffer.length() < 128) {
            cameraSerialBuffer += c;
        }
    }
    delay(10);

#ifdef BTN_MODE_PIN
    {
        static bool last = false;
        static bool initialized = false;
        static unsigned long lastMs = 0;
        bool cur = digitalRead(BTN_MODE_PIN);
        if (!initialized) { last = cur; lastMs = millis(); initialized = true; }
        else if (cur && !last && millis() - lastMs > 250) {
            lastMs = millis();
            stopCameraMode();  // NVS保存→reboot→通常モード
        }
        last = cur;
    }
#endif
}

// ============================================
// モード制御（NVSフラグ＋再起動方式）
// ============================================
void setupCameraMode() {
    // 遅延初期化：startCameraMode() で再起動
}

void loopCameraMode() {
    // ブートモードで分岐するため、ここは未使用
}

void startCameraMode() {
    USER_SERIAL.println(F("[MODE] Switching to camera mode (reboot)..."));
    appSettings.bootMode = BOOT_MODE_CAMERA;
    saveSettings();
    delay(500);
    ESP.restart();
}

void stopCameraMode() {
    USER_SERIAL.println(F("[MODE] Switching to normal mode (reboot)..."));
    appSettings.bootMode = BOOT_MODE_NORMAL;
    saveSettings();
    delay(500);
    ESP.restart();
}

bool isCameraModeActive() {
    return systemMode == MODE_CAMERA;
}

#endif // USE_CAMERA