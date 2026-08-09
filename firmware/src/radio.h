#pragma once
#include <M5Cardputer.h>
#include <Audio.h>
#include <WiFi.h>
#include "config.h"

// 克劳德电台模式 — Tab 切换 聊天/电台, 方案A(播放时暂停克劳德场景/动画)
//
// 内存策略(不爆内存):
//   - 进入电台模式: 不渲染克劳德场景/精灵(省CPU), 音频库独立增量
//   - 用静态缓冲, 零大堆分配(除了音频库自身的解码缓冲, 由库管理)
//   - 单电台(稳定后再加多台)
//
// 控制: Tab=返回聊天, [=降音, ]=升音, M=静音, R=重连
namespace Radio {

// 单电台(当前: 星岛中文国语, http MP3 64kbps, 无 PSRAM 硬约束)
static const char* STATION_NAME = "星岛中文电台";
static const char* STATION_URL  = "http://nap.casthost.net:8759/;?icy=http";

static Audio audio;
static bool gActive = false;       // 电台模式是否激活
static bool gPlaying = false;      // 是否正在播放
static bool gMuted = false;
static int  gVol = 128;            // 内部音量 0-255(默认128, map到库0-21)
static uint32_t gDebounce = 0;     // 按键防抖
static uint32_t gHeapLog = 0;
static char gStationName[40] = {0};  // 从库回调获取的站名
static char gTitle[48] = {0};        // 从库回调获取的曲目

// 库音量范围是 0-21, 不是 0-255! 内部用 0-255, 调用时 map
static void setVol(int v) {
  gVol = v;
  audio.setVolume(map(v, 0, 255, 0, 21));
}

// 库回调(在音频库任务执行, 只存数据不渲染)
void audio_showstation(const char* info) {
  if (info) { strncpy(gStationName, info, sizeof(gStationName) - 1); gStationName[sizeof(gStationName) - 1] = 0; }
}
void audio_showstreamtitle(const char* info) {
  if (info) { strncpy(gTitle, info, sizeof(gTitle) - 1); gTitle[sizeof(gTitle) - 1] = 0; }
}
void audio_info(const char* info) { (void)info; }
void audio_id3data(const char* info) { (void)info; }
void audio_eof_mp3(const char* info) { (void)info; }
void audio_process_i2s(void* s, size_t f, uint8_t* b, uint16_t l) { (void)s; (void)f; (void)b; (void)l; }
void audio_process_arlim_dma(void* s, uint8_t* b, uint32_t l) { (void)s; (void)b; (void)l; }

// 进入电台模式: 连接 WiFi, 初始化音频, 释放克劳德场景/精灵内存
inline bool enter() {
  if (gActive) return true;
  gActive = true;

  // 释放克劳德运行时内存给音频解码(关键): 清精灵缓存/场景临时
  Serial.printf("[RADIO] enter: heap_before=%u largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // WiFi: 克劳德已连接(ensureWiFi), 这里等 IP 就绪 + 设公共 DNS(不覆盖IP)
  uint32_t t0 = millis();
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - t0 < 10000) delay(200);
  // 设公共 DNS(绕过路由 DNS 污染): 传当前 IP, 只改 DNS 不覆盖 IP
  IPAddress dns1(223, 5, 5, 5), dns2(119, 29, 29, 29);
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
  delay(200);
  Serial.printf("[RADIO] enter: WiFi=%s IP=%s RSSI=%d\n",
                WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  audio.setPinout(41, 43, 42);   // I2S_BCK, I2S_WS, I2S_DOUT (Cardputer ADV)
  // setBufsize(8000) 过小导致 MP3 解码器分配缓冲失败, 用默认缓冲
  setVol(gVol);
  audio.setBalance(0);

  gStationName[0] = 0; gTitle[0] = 0;
  Serial.printf("[RADIO] connecting %s... heap=%u\n", STATION_URL, ESP.getFreeHeap());
  if (audio.connecttohost(STATION_URL)) {
    gPlaying = true;
    Serial.printf("[RADIO] connect OK, heap=%u\n", ESP.getFreeHeap());
  } else {
    gPlaying = false;
    Serial.printf("[RADIO] connect FAIL, heap=%u\n", ESP.getFreeHeap());
  }
  return true;
}

// 退出电台模式: 停止播放, 释放音频
inline void exit() {
  if (!gActive) return;
  audio.stopSong();
  gActive = false;
  gPlaying = false;
}

inline bool isActive() { return gActive; }

// 电台模式主循环: 驱动音频 + 控制键 + 更新状态
inline void loop() {
  if (!gActive) return;
  audio.loop();

  // 控制键: Tab返回, [降, ]升, M静音, R重连
  M5Cardputer.update();
  auto st = M5Cardputer.Keyboard.keysState();
  uint32_t now = millis();
  if (now > gDebounce) {
    for (auto c : st.word) {
      if (c == '[') { setVol(gVol >= 10 ? gVol - 10 : 0); gDebounce = now + 200; }
      else if (c == ']') { setVol(gVol <= 245 ? gVol + 10 : 255); gDebounce = now + 200; }
      else if (c == 'm' || c == 'M') { gMuted = !gMuted; audio.setVolume(gMuted ? 0 : map(gVol, 0, 255, 0, 21)); gDebounce = now + 200; }
      else if (c == 'r' || c == 'R') { audio.stopSong(); audio.connecttohost(STATION_URL); gDebounce = now + 500; }
    }
  }

  // 定期打印 heap + 播放状态(监控不爆内存 + 确认音频流)
  if (now - gHeapLog > 5000) {
    gHeapLog = now;
    Serial.printf("[RADIO] heap=%u largest=%u playing=%d title=%s\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  audio.isRunning() ? 1 : 0, gTitle[0] ? gTitle : "(none)");
  }
}

// 电台 UI(画到 canvas, 方案A: 不画克劳德场景/精灵)
inline void draw(M5Canvas& canvas) {
  const int W = 240;
  canvas.fillRect(0, 0, W, 135, canvas.color565(8, 14, 26));  // 深色背景

  // 统一用默认 14px 字体(克劳德标准)
  canvas.setFont(&fonts::efontCN_14);

  // 顶部: "电台" + 音量条
  canvas.setTextColor(0x07E0, canvas.color565(8, 14, 26));   // 绿"电台"
  canvas.setCursor(4, 2); canvas.print("电台");
  // 音量条(左, 宽度 120)
  int vw = 110, vh = 5, vx = 40, vy = 5;
  canvas.drawRect(vx, vy, vw, vh, 0x7BEF);
  int fill = gMuted ? 0 : (vw * gVol / 255);
  if (fill > 0) canvas.fillRect(vx + 1, vy + 1, fill - 1, vh - 2, 0x07E0);

  // 站名(14px, 亮橙)
  canvas.setTextColor(0xFC4B, canvas.color565(8, 14, 26));   // 亮橙
  canvas.setCursor(4, 18);
  canvas.print(gStationName[0] ? gStationName : STATION_NAME);

  // 曲目/状态(14px, 灰)
  canvas.setTextColor(0xD3AB, canvas.color565(8, 14, 26));
  canvas.setCursor(4, 36);
  if (gMuted) canvas.print("已静音");
  else if (gTitle[0]) canvas.print(gTitle);
  else if (gPlaying) canvas.print("播放中…");
  else canvas.print("连接失败,按R重连");

  // 分隔线
  canvas.drawFastHLine(0, 54, W, 0x39C7);

  // 底部提示(14px)
  canvas.setTextColor(0x7BCF, canvas.color565(8, 14, 26));
  canvas.setCursor(4, 118);
  canvas.print("Tab=返回 []=音量 M=静音 R=重连");
}

}  // namespace Radio
