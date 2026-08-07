#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <string>
#include "config.h"
#include "net.h"
#include "dashscope.h"  // DS::volRef

// 方案Y改造: STT 走后端代理(本地 Whisper), 不再流式直连 DashScope。
// 流程: 按住 Opt 录音(存 LittleFS /rec.pcm) → 松开 → 整段 POST 后端 /stt → 返回文本。
// ⚠️ 重要: 绝不能在录音/上传过程中调用 LittleFS.end()! 因为固件的精灵(sprites)
//    就是从 LittleFS 读取的, 卸载文件系统会导致精灵无法渲染(没画面/没声音)。
namespace STT {
static const uint32_t SR = 16000;

inline void switchToMic() {
  M5.Speaker.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);
  auto mc = M5.Mic.config(); mc.sample_rate = SR; mc.magnification = 48; M5.Mic.config(mc);
  M5.Mic.begin(); delay(60);
}
inline void switchToSpeaker() {
  M5.Mic.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);
  M5.Speaker.begin(); M5.Speaker.setVolume(DS::volRef()); delay(20);
}

typedef void (*StatusCb)(const char*);
static const char* g_lastErr = "";

// 录音到 LittleFS 文件 /rec.pcm;返回字节数(0=失败/没说话)。
// 不调用 LittleFS.end() —— 精灵需要保持文件系统挂载!
inline uint32_t recordToFile(StatusCb tick, uint32_t maxMs = 55000) {
  if (!LittleFS.begin()) { g_lastErr = "fsfail"; return 0; }
  File f = LittleFS.open("/rec.pcm", "wb");
  if (!f) { g_lastErr = "openfail"; return 0; }

  switchToMic();
  static int16_t buf[2][1600];  // 各 100ms @16k = 3200 字节
  M5.Mic.record(buf[0], 1600, SR);
  M5.Mic.record(buf[1], 1600, SR);
  int idx = 0;
  uint32_t start = millis();
  bool spoke = false;
  while (millis() - start < maxMs) {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.keysState().opt && millis() - start > 400) break;  // 松开
    while (M5.Mic.isRecording() == 2) { delay(1); }
    int32_t sum = 0;
    for (int i = 0; i < 1600; i++) sum += abs(buf[idx][i]) / 1600;
    if (sum > 200) spoke = true;
    f.write((uint8_t*)buf[idx], 1600 * 2);
    M5.Mic.record(buf[idx], 1600, SR);
    idx ^= 1;
    if (tick) tick("正在听");
  }
  switchToSpeaker();
  uint32_t bytes = f.size();
  f.close();
  // 不调用 LittleFS.end()! 精灵还要用

  if (!spoke || bytes < 3200) { g_lastErr = "没听清"; return 0; }
  return bytes;
}

// 上传 LittleFS 里的 /rec.pcm 到后端 /stt, 返回识别文本
inline std::string uploadToStt(uint32_t pcmBytes) {
  if (WiFi.status() != WL_CONNECTED) return "";

  // 用 WiFiClient 直接流式发送(分块读文件写socket, 避免 malloc 大块→OOM)
  // 构造 HTTP POST 请求: 后端 Express 的 raw body 解析支持流式接收
  std::string host = backendHost();   // 返回 "ip:port" 或 "ip"
  std::string base = backendBase();
  WiFiClient client;
  if (!client.connect(backendIP(), backendPort())) return "";

  File f = LittleFS.open("/rec.pcm", "r");
  if (!f) { client.stop(); return ""; }
  size_t fsize = f.size();

  // 发送 HTTP 头
  client.print("POST "); client.print((backendPath() + "/stt").c_str()); client.println(" HTTP/1.1");
  client.print("Host: "); client.println(host.c_str());
  client.println("Content-Type: audio/wav");
  client.print("Content-Length: "); client.println(fsize);
  if (std::strlen(PET_TOKEN) > 0) { client.print("x-pet-token: "); client.println(PET_TOKEN); }
  client.println("Connection: close");
  client.println();

  // 分块读文件发送(每块1KB, 零大块内存)
  static uint8_t chunk[1024];
  client.setTimeout(60000);
  while (f.available()) {
    int n = f.read(chunk, sizeof(chunk));
    if (n > 0) client.write(chunk, n);
    else break;
  }
  f.close();

  // 读响应
  uint32_t t0 = millis();
  while (client.available() < 2 && millis() - t0 < 10000) { delay(1); }
  // 跳过响应头
  String head;
  while (client.available()) {
    char c = client.read();
    head += c;
    if (head.endsWith("\r\n\r\n")) break;
  }
  // 读 body (JSON)
  String body;
  while (client.available()) { body += (char)client.read(); }
  client.stop();

  // 解析 JSON
  if (body.isEmpty()) return "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  const char* t = doc["text"] | "";
  return std::string(t).empty() ? "" : t;
}

// 返回识别文本(空=失败/没说话)
inline std::string streamListen(StatusCb tick = nullptr) {
  auto T = [&](const char* s) { if (tick) tick(s); };
  g_lastErr = "";
  if (WiFi.status() != WL_CONNECTED) { g_lastErr = "无网络"; return ""; }

  T("录音…");
  uint32_t bytes = recordToFile(tick);
  if (bytes == 0) { if (g_lastErr[0]==0) g_lastErr = "没听清"; return ""; }

  T("识别中…");
  std::string text = uploadToStt(bytes);
  if (text.empty() && g_lastErr[0] == 0) g_lastErr = "识别失败";
  return text;
}

}  // namespace STT
