[README_EN.md](https://github.com/user-attachments/files/29050779/README_EN.md)
# AI Agent ESPer-Chan

Copyright git:mochimochi-man / H.N.Ucchi / X:@calorie0

## Usage Guide

A personal AI assistant that lives inside a tiny computer.

---

## 1. Project Overview

ESPer-Chan is a "local AI assistant" built around the Xiao ESP32S3 Sense*.

With the ESP32S3 acting as the front end, it works together with LM Studio (large language model), whisper.cpp (speech-to-text), and VOICEVOX (text-to-speech) running on your home PC. This lets the AI hold a spoken conversation — you talk to it, it talks back — without depending on any cloud service.

It features a custom face built with M5Stack Avatar, with lip-sync support so its mouth moves while it talks. Just say "hey, hey" (ねぇねぇ) and the conversation begins.

> \* Currently this is a Japanese-local-only version (global version planned).
> \* Some features have also been confirmed to work on a generic ESP32-S3. See [9. Running on an ESP32-S3 Devkit](#9-running-on-an-esp32-s3-devkit) for details.

---

## 2. Features

| Feature | Description |
|---|---|
| Voice conversation | Say "hey, hey" (ねぇねぇ) and the AI recognizes your voice and responds |
| Text input | Type text over USB serial to chat with the AI |
| Voice response | The AI's reply is converted to natural Japanese speech via VOICEVOX and read aloud |
| Avatar display | A face is shown on a small 128x64 OLED, reacting with expressions and lip-sync |
| Web avatar display | View the avatar from a browser on a PC on the same network. Voice input can also be operated remotely via the voice-input start button; the microphone on the device itself is used |
| Built-in web settings page | Switch the device into AP mode to edit some settings from a PC browser |
| Weather info | Fetches today's, tomorrow's, and the day-after-tomorrow's weather using a weather forecast API (livedoor weather compatible) |
| Current time | Fetches the current time using the NICT API |
| Local AI integration | Run any LLM (large language model) of your choice via LM Studio |
| Music player (Xiao ESP32S3 Sense only) | Switch to music player mode to play music files (MP3/MP4/WAV) stored on a microSD card |
| Face-detection web camera (Xiao ESP32S3 Sense only) | Switch to camera mode to use it as a web camera with face-detection. Detection info is output as JSON at a specified interval |
| Various commands | Use commands such as speaker test, mic test, TTS test, etc. for checking functionality |

---

## 3. What You'll Need

### ■ Hardware

| Part | Details |
|---|---|
| Main board | Xiao ESP32S3 Sense (uses Wi-Fi + built-in PDM mic) |
| Display | SSD1306 (128x64/I2C) or ST7735S (128x128/SPI) or ST7789 (320x240/SPI) |
| Speaker amp | MAX98357A module (I2S DAC amp for driving the speaker) |
| Speaker | 8Ω 2W or higher |

> \* Connect the speaker to the MAX98357A's "SPK+" and "SPK-" pins.
> \* If you also prepare a tactile switch, it can be used as a mode-switch button. See the relevant section below for details.

### ■ Wiring Diagram (Pin Assignments)

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

### ■ Software (PC side)

| Software | Purpose |
|---|---|
| LM Studio | Local LLM server |
| whisper.cpp | Speech recognition (STT) server |
| VOICEVOX | Speech synthesis (TTS) server |
| Arduino IDE | Flashing the ESP32 |

---

## 4. Setup Instructions

### ■ Step 1: Prepare the Arduino IDE

1. Install the Arduino IDE (2.x recommended).
   https://www.arduino.cc/en/software

2. Open the Arduino IDE and click "File" → "Preferences".

3. Paste the following URL into "Additional Boards Manager URLs":
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

4. Click "OK" to save.

5. Open "Tools" → "Board" → "Boards Manager", search for "ESP32 by Espressif Systems" and install it.

6. Select "Tools" → "Board" → "XIAO_ESP32S3" (or "ESP32S3 Dev Module").

### ■ Step 2: Install Required Libraries

From the Arduino IDE's "Sketch" → "Include Library" → "Manage Libraries", search for and install the following libraries:

- **M5Unified** (official M5Stack)
- **M5UnitGLASS2** (official M5Stack)
- **M5Stack-Avatar** (by meganetaaan)
- **ArduinoJson** (by Benoit Blanchon)

> \* After installing M5Stack-Avatar, you need to manually patch Face.h. Edit `Arduino/libraries/M5Stack_Avatar/src/Face.h` in the following two places:
>
> 1. `void draw(DrawContext *ctx);` → `virtual void draw(DrawContext *ctx);`
> 2. `private:` → `protected:`

### ■ Step 3: Start the PC-side Servers (Windows GUI)

#### --- LM Studio (LLM server) ---

1. Open https://lmstudio.ai/ in your browser and download the Windows version.

2. Run the downloaded installer and follow the on-screen instructions. There's nothing tricky to configure.

3. Launch LM Studio. On first launch, a model download screen appears.

4. Search for and download the model you want to use.
   - e.g. `lfm2.5-1.2b-instruct-GGUF` * default in config.h
   - e.g. `phi-4-mini-instruct`
   - e.g. `qwen3-1.7b`
   - e.g. `gemma-4-e2b`

5. Open Settings from the gear icon in the lower left, and enable "Enable Local LLM Service" from the Developer section. Quit once.

6. Launch it again, and from the LM Studio icon in the system tray, click "Start Server On Port 1234...".

7. Once the server is running, leave LM Studio minimized.

#### --- whisper.cpp (STT server) ---

1. Open https://github.com/ggml-org/whisper.cpp/releases in your browser.

2. At the bottom of the page, from "Assets" under the latest release, download the Windows ZIP file (e.g. `whisper-bin-x64.zip`).

3. Extract the downloaded ZIP to a folder of your choice.
   - e.g. `C:\whisper.cpp-bin`

4. Place a model file (ggml-small.bin recommended) inside a subfolder of the folder from step 3.
   - e.g. `C:\whisper.cpp-bin\models`
   - Models can be downloaded from https://huggingface.co/ggerganov/whisper.cpp/tree/main

5. Open PowerShell and navigate to the folder from step 3:
   ```
   cd C:\whisper.cpp-bin
   ```

6. Start it in server mode:
   ```
   ./whisper-server -m ./models\ggml-small.bin --host 0.0.0.0 --port 8080 -l ja
   ```
   Success is indicated by the message "HTTP server started". Don't close this window.

#### --- VOICEVOX (TTS server) ---

1. Open https://voicevox.hiroshiba.jp/ in your browser.

2. Click the "Download" button and download the Windows (installer) version.

3. Run the downloaded installer and follow the on-screen instructions. There's nothing tricky to configure.

4. Once installation is complete, launch VOICEVOX.

5. On first launch, the "voice library download" begins. Since Shikoku Metan (speaker ID 2) is used by default, it's fine as long as that speaker's data has been downloaded. Quit once.

6. Open PowerShell and navigate to the VOICEVOX install folder:
   ```
   cd C:\Users\(your username)\AppData\Local\programs\VOICEVOX\vv-engine
   ```
   `\AppData` is a hidden folder, so enable hidden files from the View tab in File Explorer.

7. Start VOICEVOX in server mode. Don't close this window.
   ```
   ./run.exe --host xxx.xxx.xxx.xxx
   ```

> \* The ESP32 and the PC both need to be connected to "the same Wi-Fi network" and a "public network". Also make sure Windows Firewall is not blocking ports 1234, 8081, and 50021.

### ■ Step 4: Configure config.h

Open `config.h` inside the project folder in a text editor and edit it to match your home environment.
Some settings, such as WiFi, can also be configured from a browser on a PC connected to Twenty-Chan in AP mode. See [7. Web Configuration via AP Mode](#7-web-configuration-via-ap-mode) for details.

**--- WiFi settings ---**
```cpp
#define WIFI_SSID       "Your_WiFi_SSID"
#define WIFI_PASSWORD   "Your_WiFi_Password"
```

**--- LM Studio ---**
```cpp
#define LMSTUDIO_HOST   "xxx.xxx.xxx.xxx"
#define LMSTUDIO_PORT   1234
```

**--- whisper.cpp ---**
```cpp
#define WHISPER_HOST    "xxx.xxx.xxx.xxx"
#define WHISPER_PORT    8080
```

**--- VOICEVOX ---**
```cpp
#define TTS_HOST        "xxx.xxx.xxx.xxx"
#define TTS_PORT        50021
```

**--- Model selection ---**

Remove the `//` at the start of the line for the model you want to use, and comment out (`//`) the others.

```cpp
// #define MODEL_NAME      "phi-4-mini-instruct"
// #define MODEL_NAME      "qwen3-1.7b"
#define MODEL_NAME      "lfm2.5-1.2b-instruct"
```

> How to find the HOST name (your PC's IP address):
> Open Command Prompt, run `ipconfig`, and check the "IPv4 Address" value.

### ■ Step 5: Compile and Upload

1. Open the .ino file in the Arduino IDE.
2. Select "XIAO_ESP32S3" under "Tools" → "Board".
3. Select the COM port the ESP32S3 is connected to under "Tools" → "Port".
4. In the board settings near the bottom of the "Tools" menu, set PSRAM to "OPI PSRAM".
5. Click the "→" (Upload) button in the upper left.
6. Once compiling finishes and "Done uploading" is displayed, it's a success.
7. Open the Serial Monitor (the magnifying-glass icon in the lower right) and set the baud rate to "115200".
8. After restarting the ESP32S3 once, you should see "👤 You: " displayed — that means it's working.

---

## 5. How to Use

### ■ Startup

On a successful boot, you'll see something like this in the idle state:

```
========================================
  AI Agent ESPer-Chan
  ESP32S3 x LM Studio + whisper.cpp + VOICEVOX
========================================
👤 You:
```

### ■ Text Input Mode

Type text in the Serial Monitor and press Enter to send it to the AI.
The AI's reply is shown as text and read aloud at the same time.

```
👤 You: Hello
========================================
🤖 ESPer-Chan: Hi! How are you? Want to talk about something?
========================================
```

### ■ Voice Conversation Mode (Wake Word)

Say "hey, hey" (ねぇねぇ), and after a few seconds the wake word is detected and it enters voice input mode. That's signaled by the face turning into a surprised expression, the LED flashing briefly, and a "beep" sound.

Just keep talking — your voice will be recognized and the AI will respond.

While you're talking the face stays expressionless; while processing on the server it shows a smiling expression.

If a reply is too long, the audio may get cut off partway through.

```
✨ Wake word detected
📝 How are you feeling today?
========================================
🤖 ESPer-Chan: I'm feeling great today! Want to go out somewhere?
========================================
```

You can also ask about the weather using voice input. To do so, say one of the following:

> "Today's weather", "Tomorrow's weather", or "The day after tomorrow's weather"

> \* Weather info is retrieved using an API operated through an individual's goodwill. Please refrain from repeatedly fetching weather info beyond reasonable use.

You can also get the current time via voice input. To do so, say one of the following:

> "Current time" or "What time is it now"

### ■ Music Player Mode (Xiao ESP32S3 Sense only)

Switching to music player mode (hereafter, MP mode) lets you play music files (MP3/MP4/WAV) stored on a microSD card.

To switch to MP mode, type the `/music` command in the Serial Monitor, or say "music mode" or "music player" via voice input.

When you switch to MP mode, a track-name list (INDEX) is built first. The player plays tracks based on this list. If you add new tracks, rebuild the list with the `/index` command.

To play tracks, use the `/play` (in list order), `/random` (shuffle), and `/artist` commands.

MP mode also has its own dedicated commands. See `/help` for details.

Note that voice input does not work in MP mode. To return to normal mode, type the `/exit` command in the Serial Monitor, or use the button on the web screen.

### ■ Face-Detection Web Camera Mode (Xiao ESP32S3 Sense only)

Switching to camera mode lets you use it as a web camera with face-detection support.

To switch to camera mode, type the `/cam` command in the Serial Monitor, or say "camera" via voice input.

When you switch to camera mode, it automatically reboots. The web screen switches to camera mode. Press "START" to start the camera.

> \* There is no feature for recognizing specific individuals.
> \* While in camera mode, recognition info is output to serial as JSON at a specified interval. However, note that the TX/RX pins cannot be used if a tactile switch is connected.
> \* The display does not work in camera mode. To return to normal mode, type the `/exit` command in the Serial Monitor, or use the button on the web screen.
> \* Camera mode persists unless you return to normal mode with `/exit`. It will remain in camera mode even after a reboot, so be careful.

### ■ Command List

In normal mode, the following commands can be typed in the Serial Monitor:

| Command | Description |
|---|---|
| `/voice` | Voice conversation (mic → LLM → read aloud) |
| `/say <text>` | Read text aloud |
| `/music` | Start music player mode |
| `/camera` `/cam` | Start web camera mode |
| `/today` | Today's weather forecast |
| `/tomorrow` | Tomorrow's weather forecast |
| `/dat` | Day-after-tomorrow's weather forecast |
| `/time` | Read out the current time (NICT) |
| `/status` | Show WiFi/memory status, etc. |
| `/clear` | Clear conversation history |
| `/reset` | Reboot |
| `/ap` | AP mode (WiFi setup) |
| `/initialize` | Reset settings to defaults |
| `/beep` | Beep sound test |
| `/mic` | Mic test (3-second recording) |
| `/stt` | STT test (recording → text conversion) |
| `/tts` | TTS test (read-aloud check) |

---

## 6. Customization

### ■ Changing the AI Assistant's Name

Change `PROJECT_NAME` and `SPEAK_NAME` in config.h.

```cpp
#define PROJECT_NAME    "My-Chan"
#define SPEAK_NAME      "Mai-chan"
```

`SPEAK_NAME` is the name the AI uses when it reads things aloud.

### ■ Changing the Personality

Edit `SYSTEM_PROMPT` in config.h.

```cpp
#define SYSTEM_PROMPT   "You are a small, cute AI assistant named 'Mai-chan'. You have a friendly, slightly mischievous personality. Reply in short, easy-to-understand Japanese."
```

### ■ Changing the AI Model

In the model-selection section of config.h, uncomment the model you want to use.

```cpp
// #define MODEL_NAME      "phi-4-mini-instruct"
// #define MODEL_NAME      "qwen3-1.7b"
#define MODEL_NAME      "lfm2.5-1.2b-instruct"
```

You need to have the corresponding model loaded in LM Studio.

---

## 7. Web Configuration via AP Mode

Some of config.h can be configured via the web as well. To do so, switch ESPer-Chan into AP mode via the Serial Monitor, then access it from a browser on a PC connected to that AP.

1. **Switch to AP mode**

   If you don't have WiFi configured, or can't connect to your usual network, type the `/ap` command in the Serial Monitor to switch into AP mode.

2. **Connect your PC to ESPer-Chan's AP**

   When you switch to AP mode, the connection name is displayed — connect your PC to it. A DHCP server is also provided, so you don't need to configure an IP address manually.

3. **Access the web configuration page from a browser**

   Access the displayed IP address from your browser. Once you've finished configuring, switch your PC's connection back to its original network.

---

## 8. Connecting a Tactile Switch

Connecting a tactile switch lets you use it as a button to cycle through normal mode → music mode → camera mode.

When wiring it up, refer to the following:

1. Change `#define USE_USB_CDC 0` to `1` in config.h
2. Connect the tactile switch between GPIO43 and GND
3. When uploading, set "USB CDC On Boot:" to "Enabled"

> \* Connecting a tactile switch means the TX/RX pins can no longer be used, so this is not compatible with use cases where you want to monitor camera mode's JSON output over serial.
> \* After uploading with "USB CDC On Boot: Enabled", if you need to upload again, hold the device's BOOT button while reconnecting power.

---

## 9. Running on an ESP32-S3 Devkit

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

### ■ Wiring Diagram (Pin Assignments)

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
| 5VIN | VDD * Voice recognition becomes unstable unless connected to 5V |

### ■ config.h

```cpp
#define MIC_TYPE MIC_TYPE_PDM
#define USE_SD 0
#define USE_CAMERA 0
```

### ■ Untested Devices

- External SD card module
- External camera module
- Tactile switch

---

## 10. Troubleshooting

**Wi-Fi connection fails**
Check that the SSID/password in config.h are correct.
Make sure the PC and ESPer-Chan are connected to the same Wi-Fi network.

**Voice recognition doesn't work**
Check that the whisper.cpp server is running.
Check that Windows Firewall isn't blocking port 8080.

**No audio output**
Check that the VOICEVOX engine is running.
Check that the MAX98357A pins and speaker are wired correctly.

**Face doesn't display**
Check that the display pins are wired correctly.
Check that the M5Unified, M5UnitGLASS2, and M5Stack-Avatar libraries are installed in the Arduino IDE.

**Slow responses**
Check that the LLM you're using isn't too large.
A lightweight model (around 1B–4B parameters) is recommended.

---

## 11. Credits & License

| Item | Details |
|---|---|
| ESPer-Chan | Copyright git:mochimochi-man / H.N.Ucchi / X:@calorie0 — this project (MIT License) |
| M5Stack Avatar | Copyright Shishikawa (X:@meganetaaan) — cute avatar display, originally from Stack-Chan |
| whisper.cpp | Copyright ggerganov — C++ implementation of OpenAI Whisper |
| VOICEVOX | Shikoku Metan — high-quality Japanese TTS / default character voice |
| LM Studio | — local LLM runtime environment |
| Weather forecast API (livedoor weather compatible) | — weather forecast API |

This project is a personal hobby project.
Please check the license terms of each library/software before any commercial use.

---

*Enjoy your conversation with ESPer-Chan*
