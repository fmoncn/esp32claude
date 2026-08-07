#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>

// Dell Hub 信息卡片 (切换场景联动显示)
// 数据源: Dell Hub (LAN_IP:4000) 的 JSON API, 局域网无鉴权, 设备直接拉取
//   行情    /api/indices        标普500/纳指100/上证/恒生
//   Dell系统 /api/sysinfo        CPU/内存/磁盘/运行天数
//   VPS     /api/sysinfo/vps     CPU/磁盘/内存 + Claude额度
namespace Hub {

enum CardType {
  CARD_QUOTE,      // 行情
  CARD_DELL,       // Dell 系统
  CARD_VPS,        // VPS 系统
  CARD_VPSMEM,     // VPS 内存
  CARD_QUOTA,      // Claude 额度
  CARD_COUNT,
};

struct Card {
  const char* title = "—";
  char line1[40] = {0};   // 第1行
  char line2[40] = {0};   // 第2行
  int trend = 0;          // 涨跌方向(行情): 1=红涨  -1=绿跌  0=平/其他
  bool has = false;
};

static const char* DELL_HUB = "http://LAN_IP:4000";

inline Card& cur() { static Card c; return c; }
inline CardType& cardOfScene() { static CardType t = CARD_QUOTE; return t; }

// 场景 → 卡片映射(10 场景, 4个指数各占一场景)
inline CardType cardForScene(int idx) {
  switch (idx % 10) {
    case 0: return CARD_DELL;     // 工作室 → Dell系统
    case 1: return CARD_QUOTE;    // 客厅   → 标普500
    case 2: return CARD_QUOTA;    // 卧室   → Claude额度
    case 3: return CARD_QUOTE;    // 高楼   → 纳指100
    case 4: return CARD_VPS;      // 沙漠   → VPS系统
    case 5: return CARD_QUOTE;    // 草原   → 上证指数
    case 6: return CARD_QUOTE;    // 海洋   → 恒生指数
    case 7: return CARD_QUOTA;    // 雪山   → Claude额度
    case 8: return CARD_DELL;     // 森林   → Dell系统
    default: return CARD_VPSMEM;  // 太空   → VPS内存
  }
}

// 拉取单个 API 的 JSON(先读完整响应到字符串, 再解析, 避免流式解析不完整)
static bool getJson(const char* path, JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  std::string url = std::string(DELL_HUB) + path;
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, url.c_str())) return false;
  http.setTimeout(6000);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    std::string body = http.getString().c_str();  // 完整读入(转 std::string)
    if (!body.empty()) {
      if (!deserializeJson(doc, body)) ok = true;
      else Serial.printf("[Hub] JSON parse error: %s\n", path);
    }
  }
  http.end();
  return ok;
}

// 格式化重置剩余时间(Claude格式): <1天→XhYm, 否则→XdYh
static void fmtReset(long sec, char* buf, int n) {
  if (sec < 86400) {
    long h = sec / 3600, m = (sec % 3600) / 60;
    snprintf(buf, n, "%ldh%02ldm", h, m);
  } else {
    long d = sec / 86400, h = (sec % 86400) / 3600;
    snprintf(buf, n, "%ldd%02ldh", d, h);
  }
}

// 拉取并填充当前场景对应的卡片数据
inline bool fetch(int sceneIdx) {
  cardOfScene() = cardForScene(sceneIdx);
  Card& c = cur();
  c.has = false;
  c.line1[0] = c.line2[0] = 0;
  c.trend = 0;

  switch (cardOfScene()) {
    case CARD_QUOTE: {
      // 行情: 每场景1个指数, 第1行=名称+价格, 第2行=涨跌(红涨绿跌)
      // 行情场景→指数序号: 客厅(1)→标普0, 高楼(3)→纳指1, 草原(5)→上证2, 海洋(6)→恒生3
      static const int qidx[10] = {0,0,0,1,0,2,3,0,0,0};  // 按场景序号映射指数
      JsonDocument d;
      if (!getJson("/api/indices", d)) { snprintf(c.line1, sizeof(c.line1), "行情获取中…"); return false; }
      JsonArray arr = d.as<JsonArray>();
      int n = arr.size(); if (n > 4) n = 4;
      int i = (sceneIdx >= 0 && sceneIdx < 10) ? qidx[sceneIdx] : 0;
      if (n > i) {
        const char* nm = arr[i]["name"] | "";
        // 用 as<> 显式读浮点/整数(避免 `| 0` 截断浮点)
        double pr = arr[i]["price"].as<double>(); double g = arr[i]["pct"].as<double>();
        snprintf(c.line1, sizeof(c.line1), "%s%.0f", nm, pr);   // 名称+价格(无逗号)
        snprintf(c.line2, sizeof(c.line2), "%+.2f%%", g);       // 涨跌
        c.trend = (g > 0.005) ? 1 : (g < -0.005) ? -1 : 0;      // 红涨绿跌平
        c.has = true;
      }
      break;
    }
    case CARD_DELL: {
      // Dell: line1=DELL内存 43%  line2=系统盘 50%
      JsonDocument d;
      if (!getJson("/api/sysinfo", d)) { snprintf(c.line1, sizeof(c.line1), "系统信息获取中…"); return false; }
      int mem = d["mem"]["pct"].as<int>(); if (mem < 0) mem = 0;
      int dsys = d["disk_sys"]["pct"].as<int>(); if (dsys < 0) dsys = 0;
      snprintf(c.line1, sizeof(c.line1), "DELL内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "系统盘 %d%%", dsys);
      c.has = true;
      break;
    }
    case CARD_VPS: {
      // VPS: line1=VPS内存 50%  line2=磁盘 47%
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "VPS 获取中…"); return false; }
      int mem = d["mem"]["pct"].as<int>(); if (mem < 0) mem = 0;
      int disk = d["disk_sys"]["pct"].as<int>(); if (disk < 0) disk = 0;
      snprintf(c.line1, sizeof(c.line1), "VPS内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "磁盘 %d%%", disk);
      c.has = true;
      break;
    }
    case CARD_VPSMEM: {
      // VPS内存(海洋): line1=VPS内存 50%  line2=磁盘 47%
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "内存获取中…"); return false; }
      int mem = d["mem"]["pct"].as<int>(); if (mem < 0) mem = 0;
      int disk = d["disk_sys"]["pct"].as<int>(); if (disk < 0) disk = 0;
      snprintf(c.line1, sizeof(c.line1), "VPS内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "磁盘 %d%%", disk);
      c.has = true;
      break;
    }
    case CARD_QUOTA: {
      // Claude额度: line1=Claude 89%(4h46m)  line2=99%(6d21h)
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "额度获取中…"); return false; }
      int p5 = d["claude"]["5h"]["pct"].as<int>(); if (p5 < 0) p5 = 0;
      int p7 = d["claude"]["7d"]["pct"].as<int>(); if (p7 < 0) p7 = 0;
      long r5 = d["claude"]["5h"]["reset_in_s"].as<long>();
      long r7 = d["claude"]["7d"]["reset_in_s"].as<long>();
      char r5s[12], r7s[12];
      fmtReset(r5, r5s, sizeof(r5s));
      fmtReset(r7, r7s, sizeof(r7s));
      snprintf(c.line1, sizeof(c.line1), "Claude %d%%(%s)", p5, r5s);
      snprintf(c.line2, sizeof(c.line2), "%d%%(%s)", p7, r7s);
      c.has = true;
      break;
    }
    default: break;
  }
  return c.has;
}

}  // namespace Hub
