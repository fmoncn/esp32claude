#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <cstring>
#include <string>

// 天气:硬编码广东中山(22.5175, 113.3927),不依赖 IP 定位。
// 原因:IP 定位总解析到"广州市"(ISP 的 IP 注册在广州),但设备实际在中山。
//      干脆直接硬编码设备真实位置,还省一次网络往返。
// 数据源:open-meteo 免费 API(免 key)。注意其响应是 chunked 编码,
//      ArduinoJson 直接解析会失败,须先解码 chunked(参考 claude-pocket)。
namespace WX {
struct W { int t = -100; const char* cat = "sun"; const char* label = "获取中"; };
inline W& cur() { static W w; return w; }
inline float& gLat() { static float v = 22.5175f; return v; }   // 广东中山
inline float& gLon() { static float v = 113.3927f; return v; }
inline bool& located() { static bool b = false; return b; }
inline char* cityBuf() { static char c[24] = "广东中山"; return c; }

// 硬编码中山(不再 IP 定位;IP 定位会解析到广州,位置不准)
inline void geolocate() {
  gLat() = 22.5175f; gLon() = 113.3927f;
  strncpy(cityBuf(), "广东中山", 23); cityBuf()[23] = 0;
  located() = true;
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

// 解码 HTTP chunked 传输编码(open-meteo 用 Cloudflare 边缘,响应是 chunked)
inline void decodeChunked(const char* in, size_t inLen, std::string& out) {
  size_t pos = 0;
  while (pos < inLen) {
    // 找 chunk size 行末尾的 \r\n
    size_t crlf = std::string(in + pos, inLen - pos).find("\r\n");
    if (crlf == std::string::npos) break;
    // 解析十六进制 chunk size(可能带扩展 ';')
    std::string hex(in + pos, crlf);
    size_t semi = hex.find_first_of("; \t");
    if (semi != std::string::npos) hex.resize(semi);
    size_t sz = strtoul(hex.c_str(), nullptr, 16);
    if (sz == 0) break;  // 最后一个 chunk (size=0)
    pos += crlf + 2;
    if (pos + sz > inLen) break;
    out.append(in + pos, sz);
    pos += sz + 2;  // 跳过数据 + 尾部 \r\n
  }
}

// 拉当前天气(open-meteo,硬编码中山经纬度)
inline void fetch() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure tls; tls.setInsecure(); tls.setHandshakeTimeout(10); tls.setTimeout(15);
  if (!tls.connect("api.open-meteo.com", 443, 12000)) return;
  char query[192];
  snprintf(query, sizeof(query),
           "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code",
           gLat(), gLon());
  tls.print("GET ");
  tls.print(query);
  tls.print(" HTTP/1.1\r\nHost: api.open-meteo.com\r\nUser-Agent: xiaodouding/1.0\r\nAccept: application/json\r\nConnection: close\r\n\r\n");

  // 读完整响应到 raw(含头+body),再分离头、识别 chunked、解码 body
  std::string raw; raw.reserve(2048);
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    int n = tls.available();
    if (n <= 0) { if (!tls.connected() && tls.available() == 0) break; delay(5); continue; }
    char tmp[256]; int r = tls.read((uint8_t*)tmp, n > 256 ? 256 : n);
    if (r > 0) raw.append(tmp, r);
  }
  tls.stop();

  // 分离 HTTP 头
  size_t e = raw.find("\r\n\r\n");
  if (e == std::string::npos) return;
  std::string hdrs = raw.substr(0, e);
  std::string body = raw.substr(e + 4);

  // 识别 chunked 并解码
  bool chunked = hdrs.find("hunked") != std::string::npos;  // 大小写都含 "hunked"
  std::string decoded = body;
  if (chunked) { std::string d; decodeChunked(body.c_str(), body.size(), d); decoded = d; }

  // 解析 JSON
  JsonDocument doc;
  if (deserializeJson(doc, decoded)) return;
  JsonObjectConst cw = doc["current"];
  if (!cw.isNull() && cw["temperature_2m"].is<float>()) {
    cur().t = (int)lroundf(cw["temperature_2m"].as<float>());
    const char* cat; const char* label; mapCode(cw["weather_code"] | 0, cat, label);
    cur().cat = cat; cur().label = label;
  }
}
}  // namespace WX
