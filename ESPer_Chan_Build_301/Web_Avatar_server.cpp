#include "Web_Avatar_server.h"
#include "config.h"
#include "settings.h"
#if USE_CUSTOM_FACE
#include "Web_Avatar_assets.h"
#else
#include "Web_Avatar_assets_default.h"
#endif
#include "Web_Avatar_icons.h"
#include "Web_Avatar_html.h"
#include "mbedtls/base64.h"
#include "music_player.h"
#include "camera_mode.h"
#include "CustomFace.h"
#include <string.h>
#include <esp_heap_caps.h>
#include <WebServer.h>
#include <esp_camera.h>

extern WebServer webServer;
extern SystemMode systemMode;

static uint8_t* FACE_DATA[11] = {nullptr};
static size_t FACE_SIZE[11] = {0};
static volatile int current_face_index = 0;
static bool webAvatarEndpointsRegistered = false;
volatile bool g_webVoiceRequested = false;

#if USE_CUSTOM_FACE
extern CustomFace customFace;
#endif

// ============================================
// GIF Base64 table
// ============================================
#if USE_CUSTOM_FACE
static const char* const FACE_B64[11] = {
    GIF_BLINK_NEUTRAL_B64,     GIF_BLINK_HAPPY_B64,     GIF_BLINK_SAD_B64,
    GIF_BLINK_ANGRY_B64,       GIF_BLINK_PANIC_B64,     GIF_LIPSYNC_NEUTRAL_B64,
    GIF_LIPSYNC_SAD_B64,       GIF_LIPSYNC_ANGRY_B64,   GIF_LIPSYNC_PANIC_B64,
    GIF_SURPRISED_B64,         GIF_LIPSYNC_NEUTRAL_B64
};
#else
static const char* const FACE_B64[11] = {
    GIF_BLINK_NEUTRAL_DEF_B64, GIF_BLINK_HAPPY_DEF_B64, GIF_BLINK_SAD_DEF_B64,
    GIF_BLINK_ANGRY_DEF_B64,   GIF_BLINK_PANIC_DEF_B64, GIF_LIPSYNC_NEUTRAL_DEF_B64,
    GIF_LIPSYNC_SAD_DEF_B64,   GIF_LIPSYNC_ANGRY_DEF_B64, GIF_LIPSYNC_PANIC_DEF_B64,
    GIF_SURPRISED_DEF_B64,     GIF_LIPSYNC_HAPPY_DEF_B64
};
#endif

// ============================================
// HTML builders
// ============================================
static String buildMusicPlayerHtml() {
    String html = FPSTR(MUSIC_PLAYER_HTML);
    html.replace("__ICON_PREV__", String(FPSTR(ICON_PREV_B64)));
    html.replace("__ICON_PLAY__", String(FPSTR(ICON_PLAY_B64)));
    html.replace("__ICON_PAUSE__", String(FPSTR(ICON_PAUSE_B64)));
    html.replace("__ICON_NEXT__", String(FPSTR(ICON_NEXT_B64)));
    html.replace("__ICON_MODE_IDX__", String(FPSTR(ICON_MODE_IDX_B64)));
    html.replace("__ICON_MODE_RND__", String(FPSTR(ICON_MODE_RND_B64)));
    html.replace("__ICON_MODE_ART__", String(FPSTR(ICON_MODE_ART_B64)));
    return html;
}

static String buildIndexHtml() {
    String html = FPSTR(INDEX_HTML);
    html.replace("__ICON_MIC__", String(FPSTR(ICON_MIC_B64)));
    return html;
}

// ============================================
// GIF decode
// ============================================
static void decodeGifAssets() {
    LOG_I("[WEB] Decoding 11 faces from Base64...");
    for (int i = 0; i < 11; i++) {
        const char* b64 = FACE_B64[i];
        if (!b64) continue;
        size_t b64_len = strlen(b64);
        size_t alloc_len = (b64_len * 3) / 4 + 8;

        FACE_DATA[i] = (uint8_t*)heap_caps_malloc(alloc_len, MALLOC_CAP_SPIRAM);
        if (!FACE_DATA[i]) FACE_DATA[i] = (uint8_t*)malloc(alloc_len);
        if (!FACE_DATA[i]) {
            LOG_E("[WEB] Face %d malloc failed (%d bytes)", i+1, (int)alloc_len);
            continue;
        }

        size_t out_len = 0;
        int ret = mbedtls_base64_decode(FACE_DATA[i], alloc_len, &out_len,
                                        (const unsigned char*)b64, b64_len);
        if (ret == 0 && out_len > 0) {
            FACE_SIZE[i] = out_len;
            LOG_I("[WEB] Face %d: OK (%d bytes)", i+1, (int)out_len);
        } else {
            LOG_E("[WEB] Face %d decode failed (ret=%d)", i+1, ret);
            free(FACE_DATA[i]); FACE_DATA[i] = nullptr; FACE_SIZE[i] = 0;
        }
    }
}

// ============================================
// Direct GIF send (raw HTTP)
// ============================================
static void sendGifDirect(int idx) {
    WiFiClient client = webServer.client();
    if (!client.connected()) return;
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/gif");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Pragma: no-cache");
    client.println("Expires: 0");
    client.println("Connection: close");
    client.print("Content-Length: "); client.println(FACE_SIZE[idx]);
    client.println();

    const size_t CHUNK = 512;
    size_t sent = 0;
    while (sent < FACE_SIZE[idx]) {
        size_t toSend = min((size_t)(FACE_SIZE[idx] - sent), CHUNK);
        client.write(FACE_DATA[idx] + sent, toSend);
        sent += toSend;
        delay(1);
    }
    client.stop();
}

// ============================================
// Server init
// ============================================
void initGifServer() {
    if (webAvatarEndpointsRegistered || WiFi.status() != WL_CONNECTED) return;
    decodeGifAssets();

    // --- Root page ---
    webServer.on("/", HTTP_GET, []() {
        if (systemMode == MODE_MUSIC) {
            webServer.send(200, "text/html", buildMusicPlayerHtml());
            return;
        }
        if (systemMode == MODE_CAMERA) {
            webServer.send(200, "text/html", FPSTR(CAMERA_HTML));
            return;
        }
        if (apModeActive) {
            webServer.send(200, "text/html; charset=utf-8", generateConfigPage());
            return;
        }
        webServer.send(200, "text/html", buildIndexHtml());
    });

    // --- Mode API ---
    webServer.on("/api/mode", HTTP_GET, []() {
        String modeStr = "agent";
        if (systemMode == MODE_MUSIC) modeStr = "music";
        else if (systemMode == MODE_CAMERA) modeStr = "camera";
        webServer.send(200, "text/plain", modeStr);
    });

    // --- Music APIs ---
    webServer.on("/api/music/status", HTTP_GET, []() {
        if (systemMode != MODE_MUSIC) {
            webServer.send(404, "text/plain", "Not in music mode");
            return;
        }
        webServer.send(200, "application/json", getMusicStatusJSON());
    });

    webServer.on("/api/music/command", HTTP_POST, []() {
        if (systemMode != MODE_MUSIC) {
            webServer.send(404, "text/plain", "Not in music mode");
            return;
        }
        String cmd = webServer.arg("cmd");
        if (cmd.length() > 0) {
            handleWebMusicCommand(cmd);
            webServer.send(200, "text/plain", "OK");
        } else {
            webServer.send(400, "text/plain", "Missing cmd");
        }
    });

    // --- Camera APIs ---
    webServer.on("/capture", HTTP_GET, []() {
        if (systemMode != MODE_CAMERA) {
            webServer.send(404, "text/plain", "Not in camera mode");
            return;
        }
        #if USE_CAMERA
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
        #else
        webServer.send(404, "text/plain", "Camera not enabled");
        #endif
    });

    webServer.on("/api/face/detect", HTTP_POST, []() {
        int count = webServer.arg("count").toInt();
        #if USE_CAMERA
        g_faceDetectedCount = count;
        #endif
        setGifFaceIndex(count > 0 ? 9 : 0);
        #if USE_CUSTOM_FACE
        customFace.setCustomExpression(count > 0 ? EXPR_SURPRISED : EXPR_NEUTRAL);
        #endif
        webServer.send(200, "text/plain", "OK");
    });

    webServer.on("/api/camera/exit", HTTP_POST, []() {
        webServer.send(200, "text/plain", "OK");
        #if USE_CAMERA
        if (systemMode == MODE_CAMERA) stopCameraMode();
        #endif
    });

    // --- Common APIs ---
    webServer.on("/api/face", HTTP_GET, []() {
        if (apModeActive) {
            webServer.send(404, "text/plain", "Not available in AP mode");
            return;
        }
        webServer.send(200, "text/plain", String(current_face_index));
    });

    webServer.on("/face.gif", HTTP_GET, []() {
        if (apModeActive) {
            webServer.send(404, "text/plain", "Not available in AP mode");
            return;
        }
        int idx = current_face_index;
        if (idx >= 0 && idx < 11 && FACE_DATA[idx] && FACE_SIZE[idx] > 0) {
            sendGifDirect(idx);
        } else {
            webServer.send(404, "text/plain", "Face GIF not available");
        }
    });

    webServer.on("/api/voice", HTTP_POST, []() {
        if (apModeActive) {
            webServer.send(404, "text/plain", "Not available in AP mode");
            return;
        }
        g_webVoiceRequested = true;
        webServer.send(200, "text/plain", "OK");
    });

    webAvatarEndpointsRegistered = true;
    LOG_I("[WEB] Endpoints registered at http://%s/", WiFi.localIP().toString().c_str());
}

void setGifFaceIndex(int index) {
    if (index >= 0 && index < 11) current_face_index = index;
}

int getGifFaceIndex() {
    return current_face_index;
}