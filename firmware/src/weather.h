#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <cstring>

// 设备直连 open-meteo(免 key,设备在干净 WiFi 能连)。后台核每 ~30min 拉一次。
// 经纬度用 IP 自动定位(ip-api,免 key,HTTP),不再硬编码城市。
namespace WX {
struct W { int t = -100; const char* cat = "sun"; const char* label = "获取中"; };
inline W& cur() { static W w; return w; }
inline float& gLat() { static float v = 39.9f; return v; }
inline float& gLon() { static float v = 116.4f; return v; }
inline bool& located() { static bool b = false; return b; }
inline char* cityBuf() { static char c[24] = ""; return c; }

// IP 定位:拿当前公网 IP 的城市经纬度(中文城市名)
inline void geolocate() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, "http://ip-api.com/json/?fields=status,lat,lon,city&lang=zh-CN")) return;
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    JsonDocument d;
    if (!deserializeJson(d, http.getStream()) && !strcmp(d["status"] | "", "success")) {
      gLat() = d["lat"] | 39.9f; gLon() = d["lon"] | 116.4f;
      strncpy(cityBuf(), d["city"] | "", 23); cityBuf()[23] = 0;
      located() = true;
    }
  }
  http.end();
}

inline void mapCode(int code, const char*& cat, const char*& label) {
  if (code == 0) { cat = "sun"; label = "晴"; }
  else if (code <= 2) { cat = "suncloud"; label = "晴间多云"; }
  else if (code <= 3) { cat = "cloud"; label = "多云"; }
  else if (code <= 48) { cat = "fog"; label = "雾"; }
  else if (code <= 57) { cat = "rain"; label = "毛毛雨"; }
  else if (code <= 67) { cat = "rain"; label = "雨"; }
  else if (code <= 77) { cat = "snow"; label = "雪"; }
  else if (code <= 82) { cat = "rain"; label = "阵雨"; }
  else if (code <= 86) { cat = "snow"; label = "阵雪"; }
  else { cat = "storm"; label = "雷雨"; }
}

// 用 IP 定位到的经纬度拉当地天气(open-meteo 新接口 current=,不用 filter)
inline void fetch() {
  if (WiFi.status() != WL_CONNECTED) return;
  char url[180];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,weather_code",
           gLat(), gLon());
  WiFiClientSecure tls; tls.setInsecure(); tls.setHandshakeTimeout(10);
  HTTPClient http; http.setTimeout(8000);
  if (!http.begin(tls, url)) return;
  int code = http.GET();
  if (code == 200) {
    JsonDocument d;
    if (!deserializeJson(d, http.getStream())) {
      JsonObjectConst cw = d["current"];
      if (!cw.isNull() && cw["temperature_2m"].is<float>()) {  // 真拿到数字才更新,否则保持 -100(显示 --)
        cur().t = (int)lroundf(cw["temperature_2m"].as<float>());
        const char* cat; const char* label; mapCode(cw["weather_code"] | 0, cat, label);
        cur().cat = cat; cur().label = label;
      }
    }
  }
  http.end();
}
}  // namespace WX
