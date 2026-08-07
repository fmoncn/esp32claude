#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include "config.h"
#include "net.h"

// 方案Y改造: TTS 走后端代理(微软 Azure TTS), 不再直连 DashScope。
// 流程: 调后端 /tts → 返回 24kHz 裸 PCM → 流式边收边播。
// ⚠️ 无PSRAM关键: 不能一次性 malloc 整个 PCM(回复约90KB, 堆不够), 
//    必须用小的轮转缓冲边从HTTP读边播放。
namespace DS {

inline int& volRef() { static int v = 160; return v; }  // 0..255 音量

// 流式播放: 调后端 /tts, 边读边播。返回错误字符串(空=成功)
inline std::string speak(const std::string& text) {
  Serial.printf("[TTS] start speak, free heap=%u\n", ESP.getFreeHeap());
  if (WiFi.status() != WL_CONNECTED) return "nowifi";
  if (text.empty()) return "empty";

  std::string url = backendBase() + "/tts";
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, url.c_str())) return "beginfail";
  http.addHeader("Content-Type", "application/json");
  if (std::strlen(PET_TOKEN) > 0) http.addHeader("x-pet-token", PET_TOKEN);
  http.setTimeout(30000);

  JsonDocument req;
  req["text"] = text.c_str();
  std::string body;
  serializeJson(req, body);

  int code = http.POST((uint8_t*)body.data(), body.size());
  if (code != 200) { http.end(); return "tts" + std::to_string(code); }

  WiFiClient* s = http.getStreamPtr();
  s->setTimeout(5000);
  int total = http.getSize();
  if (total <= 0) { http.end(); return "nosize"; }

  // 流式播放: 4 个轮转缓冲(每个1KB), 边读边播, 不用大块malloc
  M5.Speaker.setVolume(volRef());
  const int SR = 24000;
  static int16_t pbuf[4][512];   // 每块 512 samples = 1024 bytes
  static uint8_t carryBuf = 0;
  int slot = 0, carry = 0;
  size_t consumed = 0;
  uint32_t lastData = millis();

  while (true) {
    // 读到下一块
    uint8_t* bb = (uint8_t*)pbuf[slot];
    int off = 0;
    if (carry) { bb[0] = carryBuf; off = 1; carry = 0; }
    int want = 1024 - off;
    int got = 0;
    uint32_t r0 = millis();
    while (got < want && millis() - r0 < 3000) {
      int a = s->available();
      if (a > 0) {
        int n = s->readBytes(bb + off + got, want - got);
        if (n > 0) got += n;
      } else if (!s->connected()) break;
      else delay(2);
    }
    if (got == 0) {
      // 没读到数据: 判断是流结束还是超时
      if (!s->connected() || (total > 0 && consumed >= (size_t)total)) break;
      if (millis() - lastData > 5000) break;  // 5s无数据超时
      continue;
    }
    lastData = millis();
    consumed += got;
    int totalBytes = off + got;
    int samples = totalBytes / 2;
    if (totalBytes & 1) { carry = 1; carryBuf = bb[samples * 2]; }

    // 等扬声器有空位再播(4块轮转)
    uint32_t w0 = millis();
    while (M5.Speaker.isPlaying(0) == 2 && millis() - w0 < 2000) delay(1);
    if (samples > 0) {
      M5.Speaker.playRaw(pbuf[slot], samples, SR, false, 1, 0, false);
      slot = (slot + 1) % 4;
    }
    if (total > 0 && consumed >= (size_t)total) break;  // 读满
  }

  http.end();
  // 等播放完
  uint32_t d0 = millis();
  while (M5.Speaker.isPlaying() && millis() - d0 < 10000) delay(5);
  M5.Speaker.stop();
  Serial.printf("[TTS] end speak, free heap=%u\n", ESP.getFreeHeap());
  return "";
}

}  // namespace DS
