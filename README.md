# AI Agent ESPer-Chan

Copyright git:mochimochi-man / H.N. Uh / X:@calorie0

## User Guide

Your own personal AI assistant, living inside a tiny computer.

---

## 1. Project Overview

ESPer-Chan is a "local AI assistant" built on the Xiao ESP32S3 Sense※.

With the ESP32S3 acting as the front end, it connects to LM Studio (a large language model), whisper.cpp (speech-to-text), and VOICEVOX (text-to-speech) running on your home PC, letting you enjoy voice conversations with an AI that doesn't depend on any cloud service — just talk, and it talks back.

It features a custom face powered by M5Stack Avatar, complete with lip-sync animation while it speaks. Just say "hey, hey" (ねぇねぇ) to start a conversation.

> ※Currently this is a Japanese-local-only build. (Global version support is planned.)
> ※Some features have been confirmed to work on generic ESP32-S3 boards as well. See [9. Running on an ESP32-S3 Devkit](#9-running-on-an-esp32-s3-devkit) for details.

---

## 2. What It Can Do

| Feature | Description |
|---|---|
| Voice conversation | Say "hey, hey" and it recognizes your voice and the AI responds |
| Text input | Chat with the AI by typing over USB serial |
| Voice response | The AI's replies are converted into natural Japanese speech by VOICEVOX and read aloud |
| Avatar display | A face is shown on a small 128x64 OLED, reacting with expressions and lip-sync |
| Web avatar display | The avatar can be viewed from a browser on any PC on the same network. You can also trigger voice input remotely via a button (the microphone on the device itself is still used) |
| Built-in web settings page | Switch the device into AP mode to edit some settings from a PC browser |
| Weather lookup | Uses a weather forecast API (livedoor-weather-compatible) to fetch today's, tomorrow's, and the day-after-tomorrow's forecast |
| Current time lookup | Uses NICT's API to fetch the current time |
| Local AI integration | Run any LLM (large language model) of your choice via LM Studio |
| Music player (Xiao ESP32S3 Sense only) | Switch to Music Player mode to play music files (MP3/MP4/WAV) from a microSD card |
| Face-detection web camera (Xiao ESP32S3 Sense only) | Switch to Camera mode to use it as a web camera with face-detection. Detection info is output as JSON at a specified interval |
| Various commands | Use commands such as speaker test, mic test, TTS test, etc. for checking functionality |

---

## 3. What You'll Need

### ■ Hardware

| Part | Details |
|---|---|
| Main board | Xiao ESP32S3 Sense (uses Wi-Fi + built-in PDM mic) |
| Display | SSD1306 (128x64/I2C) or ST7735S (128x128/SPI) or ST7789 (240x240/SPI) or ST7789 (320x240/SPI) |
| Speaker amp | MAX98357A module (I2S DAC amp for driving the speaker) |
| Speaker | 8Ω 2W or higher |

> \* Connect the speaker to the MAX98357A's "SPK+" and "SPK-" pins.
> \* Preparing a tactile pushbutton lets you use it as a mode-switch button. See the dedicated section below for details.
> \* Some displays on the market don't have a CS pin. Those cannot be used given this wiring.

### ■ Wiring Diagram (Pinout)

**[MAX98357A]**

| Pin | Connects to |
|---|---|
| GPIO1 | BCLK |
| GPIO2 | LRC |
| GPIO3 | DIN |
| GND | GND |
| 3.3V/5V | VIN |

**[SSD1306 (128x64/I2C)]**

| Pin | Connects to |
|---|---|
| GPIO5 | SDA |
| GPIO6 | SCK |
| GND | GND |
| 3.3V | VCC |

**[ST7735S (128x128/SPI) or ST7789 (320x240/SPI)]**

| Pin | Connects to |
|---|---|
| GPIO4 | DC |
| GPIO5 | CS |
| GPIO6 | RST |
| GPIO7 | SCK |
| GPIO9 | SDA |
| GND | GND |
| 3.3V | VCC |

**[ST7789 (240x240/SPI)]**

| Pin | Connects to |
|---|---|
| GPIO4 | DC |
| GPIO5 | CS |
| GPIO6 | RST |
| GPIO7 | SCK |
| GPIO9 | SDA |
| GPIO4 | DC |
| GND | GND |
| 3.3V | VCC |
| 3.3V | BK |

### ■ Software (PC side)

| Software | Purpose |
|---|---|
| LM Studio | Local LLM server |
| whisper.cpp | Speech recognition (STT) server |
| VOICEVOX | Speech synthesis (TTS) server |
| Arduino IDE | For flashing the ESP32 |

---

## 4. Setup Steps

### ■ Step 1: Prepare the Arduino IDE

1. Install the Arduino IDE (2.x recommended).
   https://www.arduino.cc/en/software

2. Open the Arduino IDE and click "File" → "Preferences".

3. Paste the following URL into "Additional Boards Manager URLs":
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

4. Click "OK" to save.

5. Open "Tools" → "Board" → "Boards Manager", search for "ESP32 by Espressif Systems", and install it.

6. Select "Tools" → "Board" → "XIAO_ESP32S3" (or "ESP32S3 Dev Module").

### ■ Step 2: Install the Required Libraries

From the Arduino IDE's "Sketch" → "Include Library" → "Manage Libraries", search for and install the following libraries.

- **M5Unified** (by M5Stack)
- **M5UnitGLASS2** (by M5Stack)
- **M5Stack-Avatar** (by meganetaaan)
- **ArduinoJson** (by Benoit Blanchon)

> \* After installing M5Stack-Avatar, you need to manually patch Face.h. Edit `Arduino/libraries/M5Stack_Avatar/src/Face.h` in two places as follows:
>
> 1. `void draw(DrawContext *ctx);` → `virtual void draw(DrawContext *ctx);`
> 2. `private:` → `protected:`

### ■ Step 3: Start the PC-side Servers (Windows GUI version)

#### --- LM Studio (LLM server) ---

1. Open https://lmstudio.ai/ in your browser and download the Windows version.

2. Run the downloaded installer and follow the on-screen instructions. There's nothing tricky to configure.

3. Launch LM Studio. On first launch, a model download screen appears.

4. Search for and download the model you want to use.
   - Example: `lfm2.5-1.2b-instruct-GGUF`
   - Example: `qwen3-1.7b`
   - Example: `gemma-4-e2b`

5. Open Settings from the gear icon in the bottom-left, go to Developer, and enable "Enable local LLM service". Then quit once.

6. Launch it again, and from the LM Studio icon in the system tray, click "Start Server On Port 1234...".

7. Once the server has started, you can leave LM Studio minimized.

#### --- whisper.cpp (STT server) ---

1. Open https://github.com/ggml-org/whisper.cpp/releases in your browser.

2. From the "Assets" section of the latest release at the bottom of the page, download the Windows ZIP file (e.g. "whisper-bin-x64.zip").

3. Extract the downloaded ZIP and place the folder anywhere you like.
   - Example: `C:\whisper.cpp-bin`

4. Put a model file (ggml-small.bin recommended) into a subfolder of the folder from step 3.
   - Example: `C:\whisper.cpp-bin\models`
   - Models can be downloaded from https://huggingface.co/ggerganov/whisper.cpp/tree/main

5. Open PowerShell and navigate to the folder from step 3:
   ```
   cd C:\whisper.cpp-bin
   ```

6. Launch it in server mode:
   ```
   ./whisper-server -m ./models\ggml-small.bin --host 0.0.0.0 --port 8080 -l ja
   ```
   If you see the "HTTP server started" message, it worked. Don't close this window.

#### --- VOICEVOX (TTS server) ---

1. Open https://voicevox.hiroshiba.jp/ in your browser.

2. Click the "Download" button and download the Windows (installer) version.

3. Run the downloaded installer and follow the on-screen instructions. There's nothing tricky to configure.

4. Once installed, launch VOICEVOX.

5. On first launch, "voice library download" begins. Since Shikoku Metan (speaker ID 2) is used by default, it's fine as long as that speaker's data has been downloaded. Quit once.

6. Open PowerShell and navigate to VOICEVOX's install folder:
   ```
   cd C:\Users\(your username)\AppData\Local\programs\VOICEVOX\vv-engine
   ```
   `\AppData` is a hidden folder, so enable "Hidden items" from the View tab in File Explorer.

7. Launch VOICEVOX in server mode. Don't close this window.
   ```
   ./run.exe --host xxx.xxx.xxx.xxx
   ```

> \* The ESP32 and your PC must both be connected to "the same Wi-Fi network" and "the same public network". Also check that Windows Firewall isn't blocking ports 1234, 8081, and 50021.

### ■ Step 4: Web-based config.h Settings

Basic settings are done from the web screen.

1. **Switch to AP mode**

   If there's no Wi-Fi connection configured, or if you type the /ap command in the serial monitor, the device switches to AP mode.

2. **Connect your PC to the AP-mode ESPer-Chan**

   Once in AP mode, the connection details are displayed; connect your PC to that network. A DHCP server is included, so there's no need to configure an IP address manually.

3. **Access the web settings page from a browser**

   Open the displayed IP address in your browser. Once you're done configuring, reconnect your PC to its original network.

※Make sure the model you want to use has already been downloaded and is ready to use in LM Studio.

> \* You can also edit config.h directly, but anything saved via the web settings takes priority.

### ■ Step 5: Compile and Flash

1. Open the .ino file in the Arduino IDE.
2. Select "XIAO_ESP32S3" under "Tools" → "Board".
3. Select the COM port the ESP32S3 is connected to under "Tools" → "Port".
4. In the board settings near the bottom of the "Tools" menu, set PSRAM to "OPI PSRAM".
5. Click the "→" (upload) button in the top-left.
6. Once compiling finishes and "Done uploading" is shown, you're set.
7. Open the Serial Monitor (the magnifying glass icon in the bottom-right) and set the baud rate to "115200".
8. Restart the ESP32S3 once; if "👤 You: " is shown at the end, it's working.

---

## 5. How to Use

### ■ Startup

Once it boots successfully, it enters the following idle state.

```
========================================
  AI Agent ESPer-Chan
  ESP32S3 x LM Studio + whisper.cpp + VOICEVOX
========================================
👤 You:
```

### ■ Text Input Mode

Type text in the serial monitor and press Enter to send it to the AI.
The AI's reply is shown as text and read aloud at the same time.

```
👤 You: Hello
========================================
🤖 ESPer-Chan: Hi there! How are you? Want to chat about something?
========================================
```

### ■ Voice Conversation Mode (Wake Word)

Say "hey, hey" (ねぇねぇ), and after a few seconds the wake word is detected and it enters voice input mode. That's signaled by the face turning into a surprised expression, the LED flashing briefly, and a "beep" sound.

Just keep talking, and your speech will be recognized and the AI will respond.

While you're talking, the face is expressionless; while it's processing on the server, the face smiles.

If the response is too long, the audio may cut off partway through.

```
✨ Wake word detected
📝 How are you feeling today?
========================================
🤖 ESPer-Chan: I'm feeling great today! Want to go out somewhere?
========================================
```

You can also ask for weather information by voice. To do so, say one of the following:

> "Today's weather" or "Tomorrow's weather" or "The day after tomorrow's weather"

> \* Weather lookups use an API run as a personal goodwill service. Please refrain from repeatedly fetching weather data beyond reasonable use.

You can also get the current time by voice. To do so, say one of the following:

> "What time is it now" or "Current time"

### ■ Music Player Mode (Xiao ESP32S3 Sense only)

Switching to Music Player mode (MP mode) lets you play music files (MP3/MP4/WAV) stored on a microSD card.

To switch to MP mode, type the /music command in the serial monitor, or say "music mode" or "music player" by voice.

When you switch to MP mode, a song list (INDEX) is generated first. The player uses this list to play tracks. If you add new songs, rebuild the list with the /index command.

To play tracks, use /play (list order), /random (random), or /artist (by artist).

MP mode also has its own set of dedicated commands; see /help for details.

> \* Note that voice input doesn't work in MP mode. To return to normal mode, type the /exit command in the serial monitor, or use the button on the web screen.
> \* Due to missing library dependencies, certain files cannot be played. Such files will be skipped once their titles are shown.

### ■ Face-Detection Web Camera Mode (Xiao ESP32S3 Sense only)

Switching to camera mode lets you use it as a web camera with face-detection support.

To switch to camera mode, type the /cam command in the serial monitor, or say "camera" by voice.

Switching to camera mode automatically reboots the device, and the web screen switches to camera mode. Pressing START starts the camera.

> \* There is no feature for recognizing specific individuals.
> \* While in camera mode, recognition info is output to serial as JSON at the configured interval. Note that if a tactile switch is connected, the TX/RX port is unavailable.
> \* To return to normal mode, type the /exit command in the serial monitor, or use the button on the web screen.
> \* Camera mode persists until you exit it with /exit. Even if you reboot while in camera mode, it stays in camera mode — please be aware of this.

### ■ Command List

In normal mode, the following commands can be entered from the serial monitor.

| Command | Description |
|---|---|
| `/voice` | Voice conversation (mic → LLM → speech) |
| `/say <text>` | Read text aloud |
| `/music` | Start Music Player mode |
| `/camera` `/cam` | Start web camera mode |
| `/today` | Today's weather forecast |
| `/tomorrow` | Tomorrow's weather forecast |
| `/dat` | The day after tomorrow's weather forecast |
| `/time` | Read out the current time (NICT) |
| `/status` | Show Wi-Fi/memory/etc. status |
| `/clear` | Clear conversation history |
| `/reset` | Reboot |
| `/ap` | AP mode (Wi-Fi setup) |
| `/initialize` | Reset settings to default |
| `/beep` | Beep sound test |
| `/mic` | Mic test (3-second recording) |
| `/stt` | STT test (record → transcribe) |
| `/tts` | TTS test (read-aloud check) |

---

## 6. Customization

### ■ Changing the AI Assistant's Name

Edit PROJECT_NAME and SPEAK_NAME in config.h.

```cpp
#define PROJECT_NAME    "My-Chan"
#define SPEAK_NAME      "Mai-chan"
```

SPEAK_NAME is the name the AI uses when speaking aloud.

### ■ Changing the Personality

Edit SYSTEM_PROMPT in config.h.

```cpp
#define SYSTEM_PROMPT   "You are a small, cute AI assistant named \"Mai-chan\". You have a friendly, slightly playful personality. Please reply in short, easy-to-understand Japanese."
```

### ■ Changing the AI Model

In the model-selection section of config.h, uncomment the model you want to use.

```cpp
// #define MODEL_NAME      "phi-4-mini-instruct"
// #define MODEL_NAME      "qwen3-1.7b"
#define MODEL_NAME      "lfm2.5-1.2b-instruct"
```

You'll need to have the corresponding model loaded on the LM Studio side.

---

## 7. Connecting a Tactile Switch

Connecting a tactile pushbutton lets you use it as a button to cycle through Normal mode → Music mode → Camera mode.

When wiring it up, refer to the following:

1. Change `#define USE_USB_CDC 0` in config.h to `1`
2. Connect the tactile switch between GPIO43 and GND
3. When flashing, set "USB CDC On Boot" to Enabled

> \* Once a tactile switch is connected, the TX/RX port becomes unavailable, so this can't be used together with monitoring camera mode's JSON output over serial.

---

## 8. Running on an ESP32-S3 Devkit

Operation has been confirmed in the following environment.

### ■ Hardware (test environment)

| Part | Details |
|---|---|
| Main board | UICPAL ESP32-S3 N16R8 Devkit |
| Display | SSD1306 (128x64/I2C) or ST7735S (128x128/SPI) or ST7789 (320x240/SPI) |
| Microphone | INMP441 (I2S) |
| Speaker amp | MAX98357A module (I2S DAC amp for driving the speaker) |
| Speaker | 8Ω 2W or higher |

> \* Connect the speaker to the MAX98357A's "SPK+" and "SPK-" pins.

### ■ Wiring Diagram (Pinout)

**[MAX98357A]**

| Pin | Connects to |
|---|---|
| GPIO1 | BCLK |
| GPIO2 | LRC |
| GPIO3 | DIN |
| GND | GND |
| 3.3V | VIN |

**[SSD1306 (128x64/I2C)]**

| Pin | Connects to |
|---|---|
| GPIO5 | SDA |
| GPIO6 | SCK |
| GND | GND |
| 3.3V | VCC |

**[ST7735S (128x128/SPI) or ST7789 (320x240/SPI)]**

| Pin | Connects to |
|---|---|
| GPIO13 | DC |
| GPIO10 | CS |
| GPIO14 | RST |
| GPIO12 | SCK |
| GPIO11 | SDA |
| GND | GND |
| 3.3V | VCC |

**[INMP441]**

| Pin | Connects to |
|---|---|
| GPIO15 | SCK |
| GPIO16 | WS |
| GPIO17 | SD |
| GND | L/R |
| GND | GND |
| 5VIN | VDD ※Must be wired to 5V, otherwise speech recognition becomes unstable |

### ■ config.h

```cpp
#define MIC_TYPE MIC_TYPE_PDM
#define USE_SD 0
#define USE_CAMERA 0
```

### ■ Untested Devices

- External SD card module
- External camera module

---

## 9. Troubleshooting

**Wi-Fi connection fails**
Check that the SSID/password in config.h are correct.
Check that your PC and ESPer-Chan are connected to the same Wi-Fi network.

**Speech isn't recognized**
Check that the whisper.cpp server is running.
Check that Windows Firewall isn't blocking port 8080.

**No audio output**
Check that the VOICEVOX engine is running.
Check that the MAX98357A's pins and speaker are wired correctly.

**Face doesn't appear**
Check that the display pins are wired correctly.
Check that the M5Unified, M5UnitGLASS2, and M5Stack-Avatar libraries are installed in the Arduino IDE.

**Slow responses**
Check that the LLM you're using isn't too large.
A lightweight model (around 1B–4B parameters) is recommended.

---

## 10. Acknowledgements & License

| Item | Details |
|---|---|
| ESPer-Chan | Copyright git:mochimochi-man / H.N. Uh / X:@calorie0 — this project (MIT License) |
| M5Stack Avatar | Copyright Shishikawa (X:@meganetaaan) — the cute avatar display, a.k.a. the original Stack-Chan |
| whisper.cpp | Copyright ggerganov — a C++ implementation of OpenAI Whisper |
| VOICEVOX | Shikoku Metan — high-quality Japanese TTS / default character voice |
| LM Studio | — local LLM runtime environment |
| Weather Forecast API (livedoor weather compatible) | — weather forecast API |

This project is a personal hobby project.
For commercial use, please check the license terms of each library/software involved.

---

*Enjoy your conversation with ESPer-Chan*
