#pragma once
// 电台 Spike 验证 — 用 ESP32-audioI2S 播放 http MP3 电台(无 PSRAM)
// 独立于主固件, 由 build flag RADIO_SPIKE 控制
#ifdef RADIO_SPIKE

#include <M5Cardputer.h>
#include <Audio.h>
#include <WiFi.h>
#include "config.h"

// ESP32-audioI2S 的 I2S 输出引脚(Cardputer ADV 一致)
#define I2S_BCK  41
#define I2S_WS   43
#define I2S_DOUT 42

// 中文 http MP3 电台(无 PSRAM 硬约束: http + MP3 + ≤128kbps)
// 星岛中文电台国语广播(64kbps 综合新闻) - Dell 实测真 MP3 流
static const char* TEST_URL = "http://nap.casthost.net:8759/;?icy=http";

Audio audio;

static uint32_t gRadioHeapLog = 0;
static int gRadioVol = 128;   // 内部音量 0-255(默认128, 像 halbeshuhn). 库调用时 map 到 0-21

// 内部音量 0-255 → 库 0-21(ESP32-audioI2S 的 setVolume 范围是 0-21, 不是 0-255!)
static void setRadioVol(int v) {
  gRadioVol = v;
  audio.setVolume(map(v, 0, 255, 0, 21));
  Serial.printf("[RADIO] vol=%d/%d\n", v, map(v, 0, 255, 0, 21));
}

// 回调: 站名/曲目(音频库任务里执行, 只存不画)
void audio_showstation(const char* info) { Serial.printf("[RADIO] station: %s\n", info); }
void audio_showstreamtitle(const char* info) { Serial.printf("[RADIO] title: %s\n", info); }
void audio_info(const char* info) { if (strncmp(info, "CONNECT", 7) == 0) Serial.printf("[RADIO] %s\n", info); }
void audio_id3data(const char* info) { (void)info; }
void audio_eof_mp3(const char* info) { (void)info; }
void audio_process_i2s(void* self, size_t frames, uint8_t* buf, uint16_t len) { (void)self; (void)frames; (void)buf; (void)len; }
void audio_process_arlim_dma(void* self, uint8_t* buf, uint32_t len) { (void)self; (void)buf; (void)len; }

// Spike 入口: setup 里调用
inline void radioSpikeSetup() {
  M5Cardputer.begin();
  Serial.printf("[RADIO] begin, heap=%u\n", ESP.getFreeHeap());

  // 确保 WiFi 连接(用 config.h 凭据)
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[RADIO] connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
  }
  Serial.printf("[RADIO] WiFi %s, IP=%s\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                WiFi.localIP().toString().c_str());

  // 关键: 设置固定公共 DNS(绕过路由 DNS 污染, 否则国外域名解析失败)
  IPAddress dns1(223, 5, 5, 5);    // 阿里 DNS
  IPAddress dns2(119, 29, 29, 29); // 腾讯 DNS
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);
  Serial.println("[RADIO] DNS set to 223.5.5.5 / 119.29.29.29");

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);
  setRadioVol(gRadioVol);   // 默认音量(内部 0-255, 映射到库 0-21)

  Serial.printf("[RADIO] connecting %s...\n", TEST_URL);
  if (audio.connecttohost(TEST_URL)) {
    Serial.println("[RADIO] connect OK");
  } else {
    Serial.println("[RADIO] connect FAIL");
  }
}

// Spike 主循环: 驱动音频库 + 音量控制([ ]键) + heap 打印
inline void radioSpikeLoop() {
  audio.loop();

// [ ] 键音量控制(手动边沿 + 时间防抖: 每按一次±10%, 防重复触发)
// 用 keysState 检测 + 200ms 防抖, 避免按键被识别为多帧按下
static uint32_t gVolDebounce = 0;
M5Cardputer.update();
uint32_t vNow = millis();
if (vNow > gVolDebounce) {
  auto st = M5Cardputer.Keyboard.keysState();
  int dir = 0;   // -1=降, +1=升
  for (auto c : st.word) {
    if (c == '[') { dir = -1; break; }
    if (c == ']') { dir = 1;  break; }
  }
  if (dir != 0) {
    // 步进 10(内部 0-255 的 ~4%), 像 halbeshuhn 的 VOLUME_STEP
    int v = (dir < 0) ? ((gRadioVol >= 10) ? gRadioVol - 10 : 0)
                      : ((gRadioVol <= 245) ? gRadioVol + 10 : 255);
    setRadioVol(v);
    gVolDebounce = millis() + 200;   // 200ms 防抖, 一次按键只响应一次
  }
}

  uint32_t now = millis();
  if (now - gRadioHeapLog > 3000) {
    gRadioHeapLog = now;
    Serial.printf("[RADIO] heap=%u largest=%u vol=%d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), gRadioVol);
  }
  delay(5);
}

#endif  // RADIO_SPIKE
