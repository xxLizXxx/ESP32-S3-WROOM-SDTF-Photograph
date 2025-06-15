/**********
  Filename    : ESP32-S3-WROOM SD/TF-Photograph
  Description : Button-triggered photo to /camera/ on TF card
                (debounced, flush-safe, AWB-tuned).
  Board       : ESP32-S3 EYE / Freenove ESP32-S3-WROOM
  Author      : L1zXXXXXXXXXX (bilibili)
  Updated     : 2025-06-15
***********/

#include "esp_camera.h"
#include <esp_task_wdt.h>               // ←★ 解决 WDT 报错
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"
#include "sd_read_write.h"

// ─── 用户可调 ────────────────────────────────────────────────────
#define BUTTON_PIN   47        // 外置按钮：GPIO47 ↔ GND  注意：一定要保证该PIN空闲
#define DEBOUNCE_MS  40        // 去抖阈值（20–60 ms 均可）
#define LED_PIN      -1        // 板载 LED；无或不用 = -1
// ────────────────────────────────────────────────────────────────

// ── LED 辅助（可选） ────────────────────────────────────────────
void ledSetup() {
  if (LED_PIN >= 0) { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH); }
}
inline void ledOn () { if (LED_PIN >= 0) digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { if (LED_PIN >= 0) digitalWrite(LED_PIN, HIGH); }

// ── 前置声明 ────────────────────────────────────────────────────
bool cameraSetup();
void takePhoto();

void setup() {
  Serial.begin(2000000);                        // 高波特率日志
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  ledSetup();  ledOn();

  // SD 卡
  sdmmcInit();
  createDir(SD_MMC, "/camera");

  // 摄像头
  if (!cameraSetup()) {
    Serial.println("Camera init FAILED");
    while (true) { ledOn(); delay(100); ledOff(); delay(100); }
  }

  ledOff();
  Serial.println("Setup done, ready for button.");
}

// ── 主循环：去抖 + 等松手 + 看门狗保护 ────────────────────────
void loop() {
  static bool     last = HIGH;
  static uint32_t edge = 0;

  bool now = digitalRead(BUTTON_PIN);

  if (last == HIGH && now == LOW) edge = millis();               // 记录下沿

  if (last == LOW && now == LOW &&
      millis() - edge > DEBOUNCE_MS) {                           // 去抖成立

    takePhoto();                                                 // 拍一张

    uint32_t t0 = millis();                                      // 等松手
    while (digitalRead(BUTTON_PIN) == LOW) {
      esp_task_wdt_reset();                                      // 防卡死
      if (millis() - t0 > 2000) break;                           // 超 2 s 放行
      delay(5);
    }
    delay(DEBOUNCE_MS);                                          // 尾抖
  }
  last = now;
}

// ── 拍照 & 保存 ─────────────────────────────────────────────────
void takePhoto() {
  ledOn();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Capture failed"); ledOff(); return; }

  int idx = readFileNum(SD_MMC, "/camera");
  if (idx < 0) idx = millis();                                   // 回退文件名
  String path = "/camera/" + String(idx) + ".jpg";

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) { Serial.println("File open failed"); esp_camera_fb_return(fb); ledOff(); return; }

  f.write(fb->buf, fb->len);
  f.flush();                                                     // ★ 关键
  f.close();
  esp_camera_fb_return(fb);

  Serial.printf("Saved: %s\n", path.c_str());
  ledOff();
}

// ── 摄像头初始化 ────────────────────────────────────────────────
bool cameraSetup() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;

  cfg.pin_pwdn  = PWDN_GPIO_NUM;
  cfg.pin_reset = RESET_GPIO_NUM;
  cfg.pin_xclk  = XCLK_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;

  cfg.pin_d7 = Y9_GPIO_NUM;  cfg.pin_d6 = Y8_GPIO_NUM;
  cfg.pin_d5 = Y7_GPIO_NUM;  cfg.pin_d4 = Y6_GPIO_NUM;
  cfg.pin_d3 = Y5_GPIO_NUM;  cfg.pin_d2 = Y4_GPIO_NUM;
  cfg.pin_d1 = Y3_GPIO_NUM;  cfg.pin_d0 = Y2_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM;
  cfg.pin_href  = HREF_GPIO_NUM;
  cfg.pin_pclk  = PCLK_GPIO_NUM;

  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_SVGA;      // 先用 SVGA 更稳
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 1;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {                     // 有 PSRAM 再提画质
    cfg.jpeg_quality = 10;
    cfg.fb_count     = 2;
    cfg.grab_mode    = CAMERA_GRAB_LATEST;
  }

  if (esp_camera_init(&cfg) != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_brightness(s, 1);
  s->set_saturation(s, 0);

  // 白平衡调优：0 = AUTO
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);                  // ←★ 替换宏，兼容所有版本

  return true;
}
