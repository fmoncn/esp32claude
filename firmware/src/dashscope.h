#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include "config.h"

// 设备直连 DashScope(绕开 Mac VPN)。带失败原因返回。
namespace DS {

inline int& volRef() { static int v = 160; return v; }  // 0..255 音量

inline std::string speak(const std::string& text) {
  if (strlen(DASHSCOPE_API_KEY) == 0) return "nokey";
  if (WiFi.status() != WL_CONNECTED) return "nowifi";
  if (text.empty()) return "empty";
  const char* GEN = "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation";

  IPAddress ip;
  if (!WiFi.hostByName("dashscope.aliyuncs.com", ip)) return "dnsfail";

  String url; int code1 = 0;
  {
    WiFiClientSecure tls; tls.setInsecure(); tls.setHandshakeTimeout(10);
    HTTPClient http;
    if (!http.begin(tls, GEN)) return "beginfail";
    http.addHeader("Authorization", String("Bearer ") + DASHSCOPE_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);
    JsonDocument req;
    req["model"] = DASHSCOPE_TTS_MODEL;
    req["input"]["text"] = text.c_str();
    req["input"]["voice"] = DASHSCOPE_VOICE;
    String body; serializeJson(req, body);
    code1 = http.POST(body);
    if (code1 == 200) {
      JsonDocument filter; filter["output"]["audio"]["url"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)))
        url = doc["output"]["audio"]["url"].as<String>();
    }
    http.end();
  }
  if (code1 != 200) return std::string("post") + String(code1).c_str();
  if (url.isEmpty()) return "nourl";

  bool https = url.startsWith("https:");
  WiFiClient plain;
  WiFiClientSecure tls2; tls2.setInsecure(); tls2.setHandshakeTimeout(10);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  if (!http.begin(https ? (WiFiClient&)tls2 : (WiFiClient&)plain, url)) return "wavbegin";
  int code2 = http.GET();
  if (code2 != 200) { http.end(); return std::string("wav") + String(code2).c_str(); }

  WiFiClient* s = http.getStreamPtr();
  s->setTimeout(3000);
  int total = http.getSize();

  uint8_t h[64]; int hn = 0; uint32_t t0 = millis();
  while (hn < 64 && millis() - t0 < 3000) {
    int a = s->available();
    if (a > 0) { int want = 64 - hn; if (a < want) want = a; int r = s->readBytes(h + hn, want); if (r > 0) hn += r; }
    else if (!s->connected()) break;
    else delay(2);
  }
  uint32_t sr = (hn >= 28) ? (h[24] | (h[25] << 8) | (h[26] << 16) | ((uint32_t)h[27] << 24)) : 24000;
  if (sr < 8000 || sr > 48000) sr = 24000;
  int dataStart = 44;
  for (int k = 12; k + 8 <= hn; k++)
    if (h[k] == 'd' && h[k+1] == 'a' && h[k+2] == 't' && h[k+3] == 'a') { dataStart = k + 8; break; }

  // 播放:4 个轮转缓冲(playRaw 不拷数据)+ 按整采样对齐(进位奇数字节,防错位杂音)
  M5.Speaker.setVolume(volRef());
  static int16_t pbuf[4][512];
  static uint8_t lead[64];
  int slot = 0, carry = 0; uint8_t carryByte = 0;

  int pre = hn - dataStart; if (pre < 0) pre = 0;
  if (pre > 0) {
    memcpy(lead, h + dataStart, pre);
    if (pre & 1) { carry = 1; carryByte = lead[pre - 1]; }
    if (pre / 2 > 0) M5.Speaker.playRaw((int16_t*)lead, pre / 2, sr, false, 1, 0, false);
  }

  size_t consumed = hn;
  uint32_t lastData = millis();
  while (true) {
    if (total > 0 && (int)consumed >= total) break;
    int a = s->available();
    if (a <= 0) { if (!s->connected()) break; if (millis() - lastData > 3000) break; delay(2); continue; }
    uint8_t* bb = (uint8_t*)pbuf[slot];
    int off = 0;
    if (carry) { bb[0] = carryByte; off = 1; carry = 0; }
    int want = 1024 - off; if (a < want) want = a;
    int n = s->readBytes(bb + off, want);
    if (n <= 0) { delay(2); continue; }
    consumed += n; lastData = millis();
    int totalBytes = off + n;
    int samples = totalBytes / 2;
    if (totalBytes & 1) { carry = 1; carryByte = bb[samples * 2]; }
    uint32_t w0 = millis();
    while (M5.Speaker.isPlaying(0) == 2 && millis() - w0 < 2000) delay(1);
    if (samples > 0) { M5.Speaker.playRaw(pbuf[slot], samples, sr, false, 1, 0, false); slot = (slot + 1) % 4; }
  }
  http.end();
  uint32_t d0 = millis();
  while (M5.Speaker.isPlaying() && millis() - d0 < 8000) delay(5);
  M5.Speaker.stop();
  return "";
}

}  // namespace DS
