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

// 电台列表(全部 http MP3 ≤64kbps, 无 PSRAM 硬约束)
struct Station { const char* name; const char* url; };
static const Station STATIONS[] = {
  { "星岛中文国语", "http://nap.casthost.net:8759/;?icy=http" },
  { "星岛中文粤语", "http://nap.casthost.net:8765/;?icy=http" },
  { "第一财经",     "http://lhttp.qtfm.cn/live/276/64k.mp3" },
  { "广东新闻广播", "http://lhttp.qtfm.cn/live/1254/64k.mp3" },
};
static const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);
static int gStationIdx = 0;       // 当前电台索引(0=星岛, 1=第一财经, N键切换)

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
inline void setVol(int v) {
  gVol = v;
  audio.setVolume(map(v, 0, 255, 0, 21));
}
inline int vol() { return gVol; }
inline void toggleMute() {
  gMuted = !gMuted;
  audio.setVolume(gMuted ? 0 : map(gVol, 0, 255, 0, 21));
}
inline void reconnect() {
  audio.stopSong();
  audio.connecttohost(STATIONS[gStationIdx].url);
}
inline int numStations() { return NUM_STATIONS; }
inline int stationIdx() { return gStationIdx; }
inline const char* stationName() { return STATIONS[gStationIdx].name; }
// 切换电台(循环), 停止当前并连下一个
inline void nextStation() {
  gStationIdx = (gStationIdx + 1) % NUM_STATIONS;
  gStationName[0] = 0; gTitle[0] = 0;
  audio.stopSong();
  audio.connecttohost(STATIONS[gStationIdx].url);
}
// 直接选择电台(数字键 1-N), 停止当前并连选中台
inline void selectStation(int idx) {
  if (idx < 0 || idx >= NUM_STATIONS) return;
  gStationIdx = idx;
  gStationName[0] = 0; gTitle[0] = 0;
  audio.stopSong();
  audio.connecttohost(STATIONS[gStationIdx].url);
}
// 播放/暂停(OK键): 暂停=停止, 恢复=重连当前台
inline void togglePlayPause() {
  if (audio.isRunning()) {
    audio.stopSong();
    gPlaying = false;
  } else {
    gPlaying = true;
    audio.connecttohost(STATIONS[gStationIdx].url);
  }
}
inline bool isPlaying() { return gPlaying; }

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
  // 打印内存分区诊断
  heap_caps_print_heap_info(MALLOC_CAP_8BIT);

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
  Serial.printf("[RADIO] connecting %s... heap=%u\n", STATIONS[gStationIdx].url, ESP.getFreeHeap());
  if (audio.connecttohost(STATIONS[gStationIdx].url)) {
    gPlaying = true;
    Serial.printf("[RADIO] connect OK, heap=%u\n", ESP.getFreeHeap());
  } else {
    gPlaying = false;
    Serial.printf("[RADIO] connect FAIL, heap=%u\n", ESP.getFreeHeap());
  }
  return true;
}

// 退出电台模式: 停止播放
inline void exit() {
  if (!gActive) return;
  audio.stopSong();
  gActive = false;
  gPlaying = false;
}

inline bool isActive() { return gActive; }

// 电台模式主循环: 只驱动音频库(按键由 handleKeyboard 统一处理, 避免键盘竞争)
inline void loop() {
  if (!gActive) return;
  audio.loop();

  uint32_t now = millis();
  // 定期打印 heap + 播放状态(监控不爆内存 + 确认音频流)
  if (now - gHeapLog > 5000) {
    gHeapLog = now;
    Serial.printf("[RADIO] heap=%u largest=%u playing=%d title=%s\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  audio.isRunning() ? 1 : 0, gTitle[0] ? gTitle : "(none)");
  }
}

// 电台 UI(直接用 Display 画; 局部更新防闪烁, 不全屏反复清屏)
// 只在音量条/曲目/状态变化时更新对应区域, 避免 fillRect 高频清屏频闪
inline void draw() {
  auto& d = M5Cardputer.Display;
  const uint16_t BG = d.color565(8, 14, 26);
  static bool inited = false;
  static int  lastFill = -1;
  static int  lastStationIdx = -1;
  static bool lastMuted = false, lastPlaying = false;

  // 只检测需要重绘的变化
  int fill = gMuted ? 0 : (110 * gVol / 255);
  bool volChanged = (fill != lastFill);
  bool statChanged = (gMuted != lastMuted) || (gPlaying != lastPlaying);
  bool idxChanged = (gStationIdx != lastStationIdx);

  if (!inited || volChanged || statChanged || idxChanged) {
    if (!inited) {  // 首次: 全屏画背景 + 底部提示
      d.fillRect(0, 0, 240, 135, BG);
      d.setFont(&fonts::efontCN_14);
      inited = true;
    }
    // 顶部: "电台" + 音量条
    d.setTextColor(0x07E0, BG);
    d.setCursor(4, 2); d.print("电台");
    d.drawRect(40, 5, 110, 5, 0x7BEF);
    d.fillRect(41, 6, 109, 3, BG);       // 先清空整个音量条内部
    if (fill > 0) d.fillRect(41, 6, fill, 3, 0x07E0);   // 再画当前音量
    lastFill = fill;

    // 频道列表(每个频道一行, 播放中的显示"播放中")
    const int ROW_H = 20, LIST_Y = 16;
    for (int i = 0; i < NUM_STATIONS; i++) {
      int y = LIST_Y + i * ROW_H;
      d.fillRect(4, y, 232, ROW_H - 2, BG);   // 清该行
      char buf[40];
      if (i == gStationIdx) {
        if (gPlaying) snprintf(buf, sizeof(buf), ">>> %s 播放中", STATIONS[i].name);
        else          snprintf(buf, sizeof(buf), ">>> %s 已暂停", STATIONS[i].name);
        d.setTextColor(0xFC4B, BG);   // 当前台=亮橙高亮
      } else {
        snprintf(buf, sizeof(buf), "%d %s", i + 1, STATIONS[i].name);
        d.setTextColor(0xD3AB, BG);   // 非播放=灰
      }
      d.setCursor(4, y); d.print(buf);
    }
    // 分隔线(列表末尾下方) + 底部提示
    int sepY = LIST_Y + NUM_STATIONS * ROW_H + 2;
    d.drawFastHLine(0, sepY, 240, 0x39C7);
    d.setTextColor(0x7BCF, BG); d.setCursor(4, 118); d.print("1-4选台 OK播放 []=音 Tab回");
    lastStationIdx = gStationIdx;
    lastMuted = gMuted; lastPlaying = gPlaying;
  }
}

}  // namespace Radio
