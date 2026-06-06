#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <string>
#include "config.h"
#include "dashscope.h"  // DS::volRef

// 按住 Opt → 边录边把 PCM 流式发给 DashScope Paraformer 实时识别(WSS)。
// 无 PSRAM:不缓存整段音频,边说边传,支持 ~1 分钟。WebSocket 用 Links2004(ESP32 上 WSS 稳)。
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

inline void genTaskId(char* out) {
  const char* hx = "0123456789abcdef";
  for (int i = 0; i < 32; i++) out[i] = hx[esp_random() & 0xF];
  out[32] = 0;
}

// 状态回调:把当前阶段交给主程序去画(主程序刷新动画 + 角落录音提示,不再全屏黑)
typedef void (*StatusCb)(const char*);
static const char* g_lastErr = "";

// 回调用全局状态
static bool g_connected, g_disconnected, g_started, g_finished, g_failed;
static std::string g_text;
static char g_taskId[33];

inline void onEvent(WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) { g_connected = true; return; }
  if (type == WStype_DISCONNECTED || type == WStype_ERROR) { g_disconnected = true; return; }
  if (type != WStype_TEXT || !payload) return;
  JsonDocument filter;
  filter["header"]["event"] = true;
  filter["payload"]["output"]["sentence"]["text"] = true;
  filter["payload"]["output"]["sentence"]["sentence_end"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, len, DeserializationOption::Filter(filter))) return;
  const char* ev = doc["header"]["event"] | "";
  if (!strcmp(ev, "task-started")) g_started = true;
  else if (!strcmp(ev, "result-generated")) {
    JsonObjectConst s = doc["payload"]["output"]["sentence"];
    if (s["sentence_end"] | false) { const char* t = s["text"] | ""; g_text += t; }  // 只拼最终句
  } else if (!strcmp(ev, "task-finished")) g_finished = true;
  else if (!strcmp(ev, "task-failed")) { g_failed = true; g_finished = true; }
}

// 返回识别文本(空=失败/没说话)。tick 每帧回调,让主程序刷新画面(动画+角落提示)
inline std::string streamListen(StatusCb tick = nullptr) {
  auto T = [&](const char* s) { if (tick) tick(s); };
  g_lastErr = "";
  if (strlen(DASHSCOPE_API_KEY) == 0 || WiFi.status() != WL_CONNECTED) { g_lastErr = "无网络"; return ""; }
  g_connected = g_disconnected = g_started = g_finished = g_failed = false; g_text = "";
  genTaskId(g_taskId);

  T("连接中…");
  WebSocketsClient ws;
  ws.beginSSL("dashscope.aliyuncs.com", 443, "/api-ws/v1/inference");  // ESP32 默认 setInsecure
  String hdr = String("Authorization: bearer ") + DASHSCOPE_API_KEY + "\r\nX-DashScope-DataInspection: enable";
  ws.setExtraHeaders(hdr.c_str());
  ws.onEvent(onEvent);
  ws.setReconnectInterval(60000);  // 别自动重连干扰

  uint32_t t0 = millis();
  while (!g_connected && millis() - t0 < 10000) { ws.loop(); delay(5); if (g_disconnected) break; }
  if (!g_connected) { g_lastErr = "连接失败"; ws.disconnect(); return ""; }

  T("发起…");
  {  // run-task
    JsonDocument d;
    d["header"]["action"] = "run-task"; d["header"]["task_id"] = g_taskId; d["header"]["streaming"] = "duplex";
    JsonObject p = d["payload"].to<JsonObject>();
    p["task_group"] = "audio"; p["task"] = "asr"; p["function"] = "recognition"; p["model"] = "paraformer-realtime-v2";
    JsonObject pr = p["parameters"].to<JsonObject>();
    pr["format"] = "pcm"; pr["sample_rate"] = 16000; pr["disfluency_removal_enabled"] = false;
    pr["language_hints"].to<JsonArray>().add("zh");
    p["input"].to<JsonObject>();
    String s; serializeJson(d, s); ws.sendTXT(s.c_str());
  }

  t0 = millis();
  while (!g_started && millis() - t0 < 8000) { ws.loop(); delay(5); if (g_failed || g_disconnected) break; }
  if (!g_started) { g_lastErr = g_failed ? "任务被拒" : "超时"; ws.disconnect(); return ""; }

  switchToMic();
  // 双缓冲乒乓:录一块的同时把上一块发出去,DMA 连续录音不留间隙(否则长句后半段丢音→识别不到)
  static int16_t buf[2][1600];  // 各 100ms @16k = 3200 字节
  M5.Mic.record(buf[0], 1600, SR);
  M5.Mic.record(buf[1], 1600, SR);  // 两个槽都喂上,DMA 连续录
  int idx = 0;
  uint32_t start = millis();
  while (millis() - start < 55000) {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.keysState().opt && millis() - start > 400) break;  // 松开
    if (g_disconnected || g_failed) break;
    while (M5.Mic.isRecording() == 2) { ws.loop(); delay(1); }  // 等最老一块录满
    ws.sendBIN((uint8_t*)buf[idx], 1600 * 2);   // 发已录满的块
    M5.Mic.record(buf[idx], 1600, SR);          // 立刻补回该槽,保持连续录音
    idx ^= 1;
    ws.loop();
    T("正在听");  // 回调:主程序刷新一帧(动画 + 录音角标)
  }
  switchToSpeaker();

  T("识别中…");
  {  // finish-task
    char b[160];
    snprintf(b, sizeof(b),
             "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{}}}",
             g_taskId);
    ws.sendTXT(b);
  }
  t0 = millis();
  while (!g_finished && millis() - t0 < 8000) { ws.loop(); delay(5); if (g_disconnected) break; }
  ws.disconnect();
  if (g_text.empty() && g_lastErr[0] == 0) g_lastErr = "没听清";
  return g_text;
}
}  // namespace STT
