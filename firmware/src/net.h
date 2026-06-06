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
};

// 从 BACKEND_URL 推出基址(去掉末尾 /chat)
inline std::string backendBase() {
  std::string u = BACKEND_URL; size_t p = u.rfind("/chat");
  return p == std::string::npos ? u : u.substr(0, p);
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

inline bool wifiConnect(uint32_t timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

// 把一句话发给大脑,返回 {reply, emotion, name}
inline PetReply askPet(const std::string& message) {
  PetReply out;
  if (WiFi.status() != WL_CONNECTED) {
    out.reply = "(没连上 WiFi…)";
    out.emotion = "sad";
    return out;
  }

  WiFiClient client; // 本地后端走纯 HTTP,省内存(无 PSRAM,别碰 TLS)
  HTTPClient http;
  if (!http.begin(client, BACKEND_URL)) {
    out.reply = "(连不上大脑)";
    out.emotion = "sad";
    return out;
  }
  http.addHeader("Content-Type", "application/json");
  if (std::strlen(PET_TOKEN) > 0) http.addHeader("x-pet-token", PET_TOKEN);
  http.setTimeout(30000);

  JsonDocument req;
  req["petId"] = PET_ID;
  req["message"] = message;
  std::string body;
  serializeJson(req, body);

  int code = http.POST((uint8_t*)body.data(), body.size());
  if (code == 200) {
    JsonDocument res;
    DeserializationError err = deserializeJson(res, http.getStream());
    if (!err) {
      out.ok = true;
      const char* reply = res["reply"] | "……";
      const char* emotion = res["emotion"] | "neutral";
      const char* name = res["name"] | "";
      out.reply = reply;
      out.emotion = emotion;
      out.name = name;
      float iv = res["stats"]["intimacy"] | -1.0f; out.intimacy = iv < 0 ? -1 : (int)(iv + 0.5f);
    } else {
      out.reply = "(没听懂大脑的话)";
      out.emotion = "sleepy";
    }
  } else {
    out.reply = std::string("(大脑出错 ") + std::to_string(code) + ")";
    out.emotion = "sad";
  }
  http.end(); // 每次都收尾,释放 socket/堆
  return out;
}
