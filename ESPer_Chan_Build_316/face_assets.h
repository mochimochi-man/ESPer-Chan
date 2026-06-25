// face_assets.h - WEB顔用GIFアセットのデコード・共有管理
// Web_Avatar_server.cpp(ブラウザ向け配信)とcamera_face_display.cpp(物理ディスプレイ表示)の
// 両方から使われる。appSettings.faceTypeに応じてStackChanデフォルト顔/ESPer-Chanオリジナル顔の
// どちらか一方だけを実行時にデコードする。

#ifndef FACE_ASSETS_H
#define FACE_ASSETS_H

#include <Arduino.h>

#define FACE_COUNT 11

// FACE_B64と同じ並び
// 0=NEUTRAL 1=HAPPY 2=SAD 3=ANGRY 4=PANIC
// 5=LIPSYNC_NEUTRAL 6=LIPSYNC_SAD 7=LIPSYNC_ANGRY 8=LIPSYNC_PANIC
// 9=SURPRISED 10=LIPSYNC_NEUTRAL(オリジナル)/LIPSYNC_HAPPY(デフォルト)
enum FaceGifIndex {
    FACE_GIF_NEUTRAL = 0,
    FACE_GIF_HAPPY = 1,
    FACE_GIF_SAD = 2,
    FACE_GIF_ANGRY = 3,
    FACE_GIF_PANIC = 4,
    FACE_GIF_LIPSYNC_NEUTRAL = 5,
    FACE_GIF_LIPSYNC_SAD = 6,
    FACE_GIF_LIPSYNC_ANGRY = 7,
    FACE_GIF_LIPSYNC_PANIC = 8,
    FACE_GIF_SURPRISED = 9,
};

// useCustomFace=true: ESPer-Chanオリジナル顔, false: StackChanデフォルト顔
// 既に同じセットがデコード済みなら何もしない(idempotent)。異なるセット要求時は再デコード。
void decodeFaceAssets(bool useCustomFace);

// デコード済みGIFバイナリへのポインタを取得。未デコード/失敗時はnullptr。
const uint8_t* getFaceGifData(int idx, size_t* outSize);

#endif
