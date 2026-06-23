#include <Arduino.h>
#include "display_driver.h"
#include "settings.h"

#include <M5Unified.h>

// 全ディスプレイタイプを常にコンパイル（ランタイム選択対応）
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/panel/Panel_ST7735.hpp>
#include <lgfx/v1/panel/Panel_SSD1306.hpp>

using namespace lgfx::v1;

// ============================================
// TFT SPI パネル (ST7789 / ST7735S)
// ============================================
static Bus_SPI       _tft_bus;
static Panel_ST7789  _panel_st7789;
static Panel_ST7789  _panel_st7789_240;
static Panel_ST7735S _panel_st7735s;

// ============================================
// I2C OLED パネル (SSD1306)
// ============================================
static Bus_I2C       _oled_bus;
static Panel_SSD1306 _oled_panel;

// アクティブパネル追跡（reinitDisplaySPI用）
static Panel_Device* _active_panel   = nullptr;
static bool          _is_tft_display = false;

// ============================================
// TFT SPI バス共通設定
// ============================================
static void initTFTBus() {
    auto cfg = _tft_bus.config();
    cfg.spi_host    = SPI2_HOST;
    cfg.spi_mode    = 0;
    cfg.freq_write  = 27000000;
    cfg.freq_read   = 16000000;
    cfg.spi_3wire   = false;
    cfg.use_lock    = false;  // trueにするとvTaskSuspend中にバスロックが残りSD.begin()が永久ブロックする
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk    = TFT_SCK_PIN;
    cfg.pin_mosi    = TFT_MOSI_PIN;
    cfg.pin_miso    = SD_MISO_PIN;  // SPI2共有バス: SDカード用MISOを含めて初期化
    cfg.pin_dc      = TFT_DC_PIN;
    _tft_bus.config(cfg);
}

// ============================================
// ST7789 240x320
// ============================================
static void initST7789() {
    initTFTBus();
    {
        auto cfg = _panel_st7789.config();
        cfg.pin_cs           = TFT_CS_PIN;
        cfg.pin_rst          = TFT_RST_PIN;
        cfg.pin_busy         = -1;
        cfg.panel_width      = 240;   // GRAMは縦基準(240col×320row)
        cfg.panel_height     = 320;
        cfg.memory_width     = 240;
        cfg.memory_height    = 320;
        cfg.rgb_order        = false;
        cfg.offset_x         = 0;
        cfg.offset_y         = 0;
        cfg.offset_rotation  = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable         = false;
        cfg.invert           = true;
        cfg.dlen_16bit       = false;
        cfg.bus_shared       = false;
        _panel_st7789.config(cfg);
    }
    _panel_st7789.setBus(&_tft_bus);
    _active_panel   = &_panel_st7789;
    _is_tft_display = true;
    M5.Display.init(&_panel_st7789);
    M5.Display.setRotation(3);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
}

// ============================================
// ST7789 240x240
// ============================================
static void initST7789_240() {
    initTFTBus();
    {
        auto cfg = _panel_st7789_240.config();
        cfg.pin_cs           = TFT_CS_PIN;
        cfg.pin_rst          = TFT_RST_PIN;
        cfg.pin_busy         = -1;
        cfg.panel_width      = 240;
        cfg.panel_height     = 240;
        cfg.memory_width     = 240;
        cfg.memory_height    = 320;  // ST7789チップのGRAMは240x320だが表示は240x240のみ使用
        cfg.rgb_order        = false;
        cfg.offset_x         = 0;
        cfg.offset_y         = 0;    // 表示が上下にズレる基板の場合は80前後に調整
        cfg.offset_rotation  = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable         = false;
        cfg.invert           = true;
        cfg.dlen_16bit       = false;
        cfg.bus_shared       = false;
        _panel_st7789_240.config(cfg);
    }
    _panel_st7789_240.setBus(&_tft_bus);
    _active_panel   = &_panel_st7789_240;
    _is_tft_display = true;
    M5.Display.init(&_panel_st7789_240);
    M5.Display.setRotation(TFT_ROTATION);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
}

// ============================================
// ST7735S 128x128
// ============================================
static void initST7735S() {
    initTFTBus();
    {
        auto cfg = _panel_st7735s.config();
        cfg.pin_cs           = TFT_CS_PIN;
        cfg.pin_rst          = TFT_RST_PIN;
        cfg.pin_busy         = -1;
        cfg.panel_width      = 128;
        cfg.panel_height     = 128;
        cfg.memory_width     = 128;
        cfg.memory_height    = 160;   // チップメモリは128x160
        cfg.rgb_order        = true;  // ST7735S は BGR パネル
        cfg.offset_x         = 2;
        cfg.offset_y         = 1;
        cfg.offset_rotation  = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable         = false;
        cfg.invert           = false;
        cfg.dlen_16bit       = false;
        cfg.bus_shared       = false;
        _panel_st7735s.config(cfg);
    }
    _panel_st7735s.setBus(&_tft_bus);
    _active_panel   = &_panel_st7735s;
    _is_tft_display = true;
    M5.Display.init(&_panel_st7735s);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
}

// ============================================
// SSD1306 I2C OLED 128x64
// ============================================
static void initOLED() {
    {
        auto cfg = _oled_bus.config();
        cfg.i2c_port    = 0;
        cfg.freq_write  = 400000;
        cfg.freq_read   = 400000;
        cfg.pin_sda     = OLED_SDA_PIN;
        cfg.pin_scl     = OLED_SCL_PIN;
        cfg.i2c_addr    = 0x3C;
        cfg.prefix_cmd  = 0x00;
        cfg.prefix_data = 0x40;
        cfg.prefix_len  = 1;
        _oled_bus.config(cfg);
    }
    _oled_bus.init();
    {
        auto cfg = _oled_panel.config();
        cfg.panel_width     = 128;
        cfg.panel_height    = 64;
        cfg.memory_width    = 128;
        cfg.memory_height   = 64;
        cfg.offset_x        = 0;
        cfg.offset_y        = 0;
        cfg.offset_rotation = 0;
        cfg.invert          = false;
        cfg.bus_shared      = false;
        _oled_panel.config(cfg);
    }
    _oled_panel.setBus(&_oled_bus);
    _active_panel   = &_oled_panel;
    _is_tft_display = false;
    M5.Display.init(&_oled_panel);
    M5.Display.setRotation(2);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
}

// ============================================
// SPI2 IDF DMAバス再構築 (MusicMode終了時)
// ============================================
// startMusicMode() 内の SPI.begin() が periph_ll_reset(PERIPH_SPI2_MODULE) を呼ぶため、
// SPI2-GDMA チャンネルリンクが破壊される。
// release() で壊れた IDF バスハンドルを解放し、init() で GDMA-SPI2 リンクを再構築する。
void reinitDisplaySPI() {
    if (!_is_tft_display) {
        // SSD1306: I2C接続のためSPI2再初期化不要。
        // avatar タスクは startMusicMode() で suspend していないため継続描画中。
        // ここで suspend + M5.Display.* を呼ぶと i2c_master_transmit 内部の
        // バスmutexをタスクが保持したままになりデッドロックが起きる。
        // タスクを止めずにそのまま描画継続させるのが唯一の安全策。
        return;
    }
    // TFT: SPI2 GDMA リンクを再構築してから画面クリア
    _tft_bus.release();
    if (!_tft_bus.init()) {
        Serial.println("[WARN] reinitDisplaySPI: _tft_bus.init() failed");
        return;
    }
    // TFT 初期化コマンドを再送して内部ステートマシンをリセット。
    M5.Display.init(_active_panel);
    int rot = 3;
    if (appSettings.displayType == DISPLAY_ST7735S_128X128) rot = 1;
    else if (appSettings.displayType == DISPLAY_ST7789_240X240) rot = TFT_ROTATION;
    M5.Display.setRotation(rot);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
}

// ============================================
// ディスプレイ初期化 (ランタイム表示タイプ選択)
// ============================================
void initDisplay() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5.begin(cfg);

    switch (appSettings.displayType) {
        case DISPLAY_ST7789_240X320:  initST7789();     break;
        case DISPLAY_ST7789_240X240:  initST7789_240(); break;
        case DISPLAY_ST7735S_128X128: initST7735S();    break;
        default:                      initOLED();       break;  // DISPLAY_SSD1306
    }
}
