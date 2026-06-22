#include "camera_mode.h"
#include "config.h"
#include "settings.h"
#include "Web_Avatar_html.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESP_I2S.h>

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
// カメラ初期化（ブートモード専用：M5未使用）
// ============================================
static bool initCamera() {
    LOG_I("[CAM] Initializing camera...");

    // ============================================
    // 【修正】M5.begin() が呼ばれないため、新 I2C ドライバー (driver_ng)
    // は初期化されていない。旧ドライバーとの衝突は発生しないため、
    // 以下の Wire.end / i2c_driver_delete は不要。
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

    // ライブラリにSCCB I2Cの初期化を任せる
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

    // 顔検出結果JSONをシリアルに出力（システム情報は一切出力しない）
    webServer.on("/face", HTTP_POST, []() {
        String body = webServer.arg("plain");
        if (body.length() > 0) {
            USER_SERIAL.println(body);
        }
        g_faceDetectedCount = 0;  // カウント更新（将来の利用のため）
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