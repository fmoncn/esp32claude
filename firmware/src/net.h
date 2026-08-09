#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include "config.h"

struct PetReply {
  bool ok = false;
  std::string reply;
  std::string emotion = "neutral";
  std::string name;
  int intimacy = -1;  // 后端 /chat 返回的 stats.intimacy(-1=未知)
  // 推理建议菜单: 最多3条, 每条≤12字(固定缓冲省内存)。sugCount=实际条数
  int sugCount = 0;
  char sug[3][13];    // 3 × 13 字节 = 39B 静态
};

// 从 BACKEND_URL 推出基址(去掉末尾 /chat)
inline std::string backendBase() {
  std::string u = BACKEND_URL; size_t p = u.rfind("/chat");
  return p == std::string::npos ? u : u.substr(0, p);
}

// 解析 BACKEND_URL (http://ip:port/chat) 辅助函数
inline std::string backendHost() {          // "ip:port"
  std::string u = BACKEND_URL;
  size_t s = u.find("//"); if (s == std::string::npos) return u;
  s += 2; size_t e = u.find('/', s);
  return u.substr(s, e - s);
}
inline IPAddress backendIP() {              // 解析 IP
  std::string h = backendHost();
  size_t c = h.find(':'); std::string ip = (c == std::string::npos) ? h : h.substr(0, c);
  IPAddress a; a.fromString(ip.c_str()); return a;
}
inline uint16_t backendPort() {             // 默认 80
  std::string h = backendHost();
  size_t c = h.find(':');
  if (c == std::string::npos) return 80;
  return (uint16_t)atoi(h.substr(c+1).c_str());
}
inline std::string backendPath() {          // 基路径, 如 "/api" 或 "" (根路径)
  std::string u = BACKEND_URL;
  size_t s = u.find("//"); if (s == std::string::npos) return "";
  s += 2; size_t e = u.find('/', s);
  if (e == std::string::npos) return "";
  size_t q = u.find('/', e+1);
  if (q == std::string::npos) return "";   // 无子路径 → 根路径(返回空,避免 //stt)
  return u.substr(e, q-e);
}

// 拉一次宠物状态里的亲密度(开机种子值);失败返回 -1
inline int fetchIntimacy() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  std::string url = backendBase() + "/pet/" + PET_ID;
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, url.c_str())) return -1;
  if (std::strlen(PET_TOKEN) > 0) http.addHeader("x-pet-token", PET_TOKEN);
  http.setTimeout(8000);
  int code = http.GET(), iv = -1;
  if (code == 200) {
    JsonDocument f; f["stats"]["intimacy"] = true;
    JsonDocument d;
    if (!deserializeJson(d, http.getStream(), DeserializationOption::Filter(f))) {
      float v = d["stats"]["intimacy"] | -1.0f; iv = v < 0 ? -1 : (int)(v + 0.5f);
    }
  }
  http.end(); return iv;
}

inline bool wifiConnect(uint32_t timeoutMs = 12000) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);  // 开启底层自动重连
  WiFi.persistent(true);
  // 多 WiFi: 主路由器优先, 失败试备用热点
  const char* ssids[] = { WIFI_SSID, WIFI2_SSID };
  const char* pwds[] = { WIFI_PASSWORD, WIFI2_PASSWORD };
  // 先直接凭据连接主 SSID(最快, 不扫描; ESP32 自动找网络, 通常 1-2 秒)
  // 注意: mesh 多节点时可能连到非最强节点, 但启动快更重要, 漫游后续由 ensureWiFi 优化
  for (int k = 0; k < 2; k++) {
    WiFi.begin(ssids[k], pwds[k]);
    uint32_t t0 = millis();
    uint32_t wait = (k == 0) ? (timeoutMs / 2) : (timeoutMs / 3);  // 主SSID等久一点
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < wait) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI] 已连接 %s 信号=%d dBm BSSID=%s ch=%d\n",
        WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.BSSIDstr().c_str(), WiFi.channel());
      return true;
    }
    WiFi.disconnect();
  }
  return false;
}

// 非阻塞 WiFi 守护: 掉线自动重连(带5秒冷却, 不阻塞主循环), 主/热点轮流试
inline bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  static uint32_t lastTryMs = 0;
  if (millis() - lastTryMs < 5000) return false;  // 5秒冷却防刷
  lastTryMs = millis();
  WiFi.disconnect();
  // 主路由器优先, 失败试备用热点
  const char* ssids[] = { WIFI_SSID, WIFI2_SSID };
  const char* pwds[] = { WIFI_PASSWORD, WIFI2_PASSWORD };
  for (int k = 0; k < 2; k++) {
    // 扫描选信号最强的匹配 BSSID(避免漫游到弱节点)
    int n = WiFi.scanNetworks();
    String bestBssid = ""; int bestRssi = -999;
    for (int i = 0; i < n; i++) {
      if (String(ssids[k]).equals(WiFi.SSID(i))) {
        int r = WiFi.RSSI(i);
        if (r > bestRssi) { bestRssi = r; bestBssid = WiFi.BSSIDstr(i); }
      }
    }
    if (bestBssid.length() > 0) {
      uint8_t bssid[6]; sscanf(bestBssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]);
      WiFi.begin(ssids[k], pwds[k], 0, bssid, true);
    } else {
      WiFi.begin(ssids[k], pwds[k]);
    }
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 2000) delay(50);
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect();
  }
  return false;
}

// 把一句话发给大脑,返回 {reply, emotion, name}
inline PetReply askPet(const std::string& message) {
  // 失败重试: -1(HTTP错误)常因语音识别后内存未释放/连接瞬断,延迟重试一次
  for (int attempt = 0; attempt < 2; attempt++) {
    PetReply out;
    if (!ensureWiFi()) {
      out.reply = "(网络似乎溜走了,正在重连…)"; out.emotion = "sad"; return out;
    }
    WiFiClient client; // 本地后端走纯 HTTP,省内存(无 PSRAM,别碰 TLS)
    HTTPClient http;
    if (!http.begin(client, BACKEND_URL)) {
      out.reply = "(连不上大脑)"; out.emotion = "sad"; return out;
    }
    http.addHeader("Content-Type", "application/json");
    if (std::strlen(PET_TOKEN) > 0) http.addHeader("x-pet-token", PET_TOKEN);
    http.setTimeout(30000);
    JsonDocument req;
    req["petId"] = PET_ID;
    req["message"] = message;
    std::string body; serializeJson(req, body);
    int code = http.POST((uint8_t*)body.data(), body.size());
    if (code == 200) {
      JsonDocument res;
      DeserializationError err = deserializeJson(res, http.getStream());
      if (!err) {
        out.ok = true;
        const char* reply = res["reply"] | "……";
        const char* emotion = res["emotion"] | "neutral";
        const char* name = res["name"] | "";
        out.reply = reply; out.emotion = emotion; out.name = name;
        float iv = res["stats"]["intimacy"] | -1.0f; out.intimacy = iv < 0 ? -1 : (int)(iv + 0.5f);
        // 推理建议: 解析 suggestions 数组(最多3条, 固定缓冲省内存)
        out.sugCount = 0;
        if (res["suggestions"].is<JsonArray>()) {
          for (JsonVariant s : res["suggestions"].as<JsonArray>()) {
            if (out.sugCount >= 3) break;
            const char* t = s | "";
            if (t && *t) { strncpy(out.sug[out.sugCount], t, 12); out.sug[out.sugCount][12] = 0; out.sugCount++; }
          }
        }
      } else {
        out.reply = "(没听懂大脑的话)"; out.emotion = "sleepy";
      }
      http.end();
      return out;
    }
    http.end();
    if (code == -1 && attempt == 0) { delay(500); continue; }  // 语音后内存未释放,延迟重试
    out.reply = std::string("(大脑出错 ") + std::to_string(code) + ")";
    out.emotion = "sad";
    return out;
  }
  PetReply out; out.reply = "(大脑出错 -1)"; out.emotion = "sad"; return out;
}

// 中英互译:调后端 /translate,返回翻译结果(成功时 ok=true,translation 为译文)
inline bool translateText(const std::string& text, std::string& translation) {
  translation.clear();
  if (!ensureWiFi()) return false;
  std::string url = backendBase() + "/translate";
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, url.c_str())) return false;
  http.addHeader("Content-Type", "application/json");
  if (std::strlen(PET_TOKEN) > 0) http.addHeader("x-pet-token", PET_TOKEN);
  http.setTimeout(30000);
  JsonDocument req;
  req["message"] = text;
  std::string body;
  serializeJson(req, body);
  int code = http.POST((uint8_t*)body.data(), body.size());
  if (code == 200) {
    JsonDocument res;
    if (!deserializeJson(res, http.getStream())) {
      const char* t = res["translation"] | "";
      if (*t) { translation = t; http.end(); return true; }
    }
  }
  http.end();
  return false;
}
