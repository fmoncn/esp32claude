#pragma once
#include <M5Cardputer.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>

// 语音识别 STT — 设备直连 Azure Speech (westus3)
// 方案参考原版克劳德 mic_stt.h(在 Cardputer 上验证过能正常录音):
//   1. switchToMic: Speaker.end + gpio_reset_pin(43/46) 复位 ES8311 codec
//      (用 GPIO 复位而非 I2C Wire — Wire 复位会导致死机)
//   2. Mic.config magnification=48 (增益)
//   3. 双缓冲乒乓录音: 双槽 record + 等 isRecording()==2 填满, 连续无间隙
//   4. switchToSpeaker: Mic.end + gpio_reset + Speaker.begin + 静音
// 触发: 按住 Opt 说话, 松开或5秒后自动上传 Azure STT
namespace STT {

constexpr uint32_t SR = 16000;
constexpr uint32_t RECORD_MS = 5000;

enum State { STT_IDLE, STT_LISTEN, STT_PROCESS };
static State g_state = STT_IDLE;

inline void finish();  // 前置声明

inline State& state() { return g_state; }

static int16_t buf[2][1600];  // 双缓冲(各100ms @16k)
static int idx = 0;
static uint32_t recStart = 0;
static bool initialized = false;
static File g_rec;

// 切到麦克风 (原版克劳德方式: GPIO 复位 ES8311)
static void switchToMic() {
  M5Cardputer.Speaker.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);  // 复位 ES8311 codec
  auto mc = M5Cardputer.Mic.config();
  mc.sample_rate = SR;
  mc.magnification = 48;   // 原版增益值
  M5Cardputer.Mic.config(mc);
  M5Cardputer.Mic.begin(); delay(60);
}

// 切回扬声器
static void switchToSpeaker() {
  M5Cardputer.Mic.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(0);  // 静音(消除 NS4150 电流声)
  delay(20);
}

// 开始录音 (按住 Opt 触发)
inline void start() {
  if (g_state != STT_IDLE) return;
  switchToMic();
  if (!LittleFS.begin()) return;
  LittleFS.remove("/rec.pcm");
  g_rec = LittleFS.open("/rec.pcm", "w");
  if (!g_rec) { M5Cardputer.Mic.end(); return; }
  // 双槽喂上, DMA 连续录
  idx = 0;
  initialized = false;
  recStart = millis();
  g_state = STT_LISTEN;
}

// 驱动录音状态机(主循环每帧调用) — 双缓冲乒乓 + 松开/5秒停止
inline void update() {
  if (g_state != STT_LISTEN) return;
  M5Cardputer.update();
  // 首次初始化双缓冲
  if (!initialized) {
    memset(buf[0], 0, sizeof(buf[0]));
    memset(buf[1], 0, sizeof(buf[1]));
    M5Cardputer.Mic.record(buf[0], 1600, SR);
    M5Cardputer.Mic.record(buf[1], 1600, SR);
    initialized = true;
    return;
  }
  // 松开发送 (或 5 秒到)
  if ((!M5Cardputer.Keyboard.keysState().opt && millis() - recStart > 400) ||
      millis() - recStart >= RECORD_MS) {
    finish();
    return;
  }
  // 等最老一块录满, 存块 + 补槽
  if (M5Cardputer.Mic.isRecording() != 2) {
    if (g_rec) g_rec.write((const uint8_t*)buf[idx], 1600 * 2);
    M5Cardputer.Mic.record(buf[idx], 1600, SR);
    idx ^= 1;
  }
}

// 结束录音并识别
inline void finish() {
  if (g_state != STT_LISTEN) return;
  if (g_rec) { g_rec.close(); }
  switchToSpeaker();
  g_state = STT_PROCESS;
}

// 上传 PCM 到 Azure STT, 返回识别文本
inline std::string recognize() {
  // 快速失败: 无 WiFi 直接返回(避免 TLS 白等 15s)
  if (WiFi.status() != WL_CONNECTED) return "";
  File rf = LittleFS.open("/rec.pcm", "r");
  if (!rf || rf.size() < 8000) { if (rf) rf.close(); LittleFS.remove("/rec.pcm"); return ""; }
  uint32_t pcmBytes = rf.size();
  rf.close();
  WiFiClientSecure c;
  c.setInsecure();
  // 连接超时缩到 8s(减轻主循环 UI 冻结;正常局域网+Azure 1-3s 足够)
  if (!c.connect("westus3.stt.speech.microsoft.com", 443, 8000)) { LittleFS.remove("/rec.pcm"); return ""; }
  uint8_t hdr[44] = {0};
  memcpy(hdr, "RIFF", 4); uint32_t riff = 36 + pcmBytes; memcpy(hdr+4, &riff, 4);
  memcpy(hdr+8, "WAVE", 4); memcpy(hdr+12, "fmt ", 4);
  uint32_t fmt=16; memcpy(hdr+16, &fmt, 4); uint16_t pcm=1,mono=1,bits=16;
  memcpy(hdr+20, &pcm, 2); memcpy(hdr+22, &mono, 2);
  memcpy(hdr+24, &SR, 4); uint32_t br=SR*2; memcpy(hdr+28, &br, 4);
  uint16_t align=2; memcpy(hdr+32, &align, 2); memcpy(hdr+34, &bits, 2);
  memcpy(hdr+36, "data", 4); memcpy(hdr+40, &pcmBytes, 4);
  size_t total = 44 + pcmBytes;
  c.printf("POST /speech/recognition/conversation/cognitiveservices/v1?language=zh-CN HTTP/1.1\r\n");
  c.printf("Host: westus3.stt.speech.microsoft.com\r\n");
  c.printf("Ocp-Apim-Subscription-Key: %s\r\n", AZURE_STT_KEY);
  c.println("Content-Type: audio/wav");
  c.println("Accept: application/json");
  c.println("Connection: close");
  c.printf("Content-Length: %d\r\n\r\n", (int)total);
  c.write(hdr, 44);
  File uf = LittleFS.open("/rec.pcm", "r");
  static uint8_t fbuf[2048];
  while (uf.available()) { int r = uf.read(fbuf, sizeof(fbuf)); if (r <= 0) break; c.write(fbuf, r); }
  uf.close();
  String resp;
  uint32_t t0 = millis();
  while (c.connected() && millis() - t0 < 8000) {  // 读响应缩到8s
    while (c.available()) { char ch = c.read(); resp += ch; }
    if (resp.indexOf("DisplayText") >= 0) break;
  }
  c.stop();
  LittleFS.remove("/rec.pcm");
  int di = resp.indexOf("DisplayText");
  if (di >= 0) {
    int s = resp.indexOf("\"", di+12);
    int e = resp.indexOf("\"", s+1);
    if (s >= 0 && e > s) return std::string(resp.substring(s+1, e).c_str());
  }
  return "";
}

inline void reset() { g_state = STT_IDLE; }

}  // namespace STT
