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
  bool has = false;
};

static const char* DELL_HUB = "http://LAN_IP:4000";

inline Card& cur() { static Card c; return c; }
inline CardType& cardOfScene() { static CardType t = CARD_QUOTE; return t; }

// 场景 → 卡片映射(10 场景分配 5 种卡片, 按场景特点)
inline CardType cardForScene(int idx) {
  switch (idx % 10) {
    case 0: return CARD_DELL;     // 工作室 → Dell系统
    case 1: return CARD_QUOTE;    // 客厅   → 行情
    case 2: return CARD_QUOTA;    // 卧室   → Claude额度
    case 3: return CARD_QUOTE;    // 高楼   → 行情
    case 4: return CARD_VPS;      // 沙漠   → VPS系统
    case 5: return CARD_DELL;     // 草原   → Dell系统
    case 6: return CARD_VPSMEM;   // 海洋   → VPS内存
    case 7: return CARD_QUOTA;    // 雪山   → Claude额度
    case 8: return CARD_DELL;     // 森林   → Dell系统
    default: return CARD_QUOTE;   // 太空   → 行情
  }
}

// 拉取单个 API 的 JSON
static bool getJson(const char* path, JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) return false;
  std::string url = std::string(DELL_HUB) + path;
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, url.c_str())) return false;
  http.setTimeout(6000);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    if (!deserializeJson(doc, http.getStream())) ok = true;
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

  switch (cardOfScene()) {
    case CARD_QUOTE: {
      // 行情分2页(按场景奇偶): 页0=纳指/标普  页1=恒指/上证
      JsonDocument d;
      if (!getJson("/api/indices", d)) { snprintf(c.line1, sizeof(c.line1), "行情获取中…"); return false; }
      JsonArray arr = d.as<JsonArray>();
      int n = arr.size(); if (n > 4) n = 4;
      bool pg2 = (sceneIdx % 2) == 1;   // 奇数场景→第2页
      if (n >= 4) {
        int i1 = pg2 ? 3 : 1;  // line1: 页0=纳指[1], 页1=恒指[3]
        int i2 = pg2 ? 2 : 0;  // line2: 页0=标普[0], 页1=上证[2]
        const char* nm1 = arr[i1]["name"] | ""; float pr1 = arr[i1]["price"] | 0; float g1 = arr[i1]["pct"] | 0;
        const char* nm2 = arr[i2]["name"] | ""; float pr2 = arr[i2]["price"] | 0; float g2 = arr[i2]["pct"] | 0;
        snprintf(c.line1, sizeof(c.line1), "%s%.0f %+.1f%%", nm1, pr1, g1);
        snprintf(c.line2, sizeof(c.line2), "%s%.0f %+.1f%%", nm2, pr2, g2);
        c.has = true;
      }
      break;
    }
    case CARD_DELL: {
      // Dell: line1=DELL内存 43%  line2=系统盘 50%
      JsonDocument d;
      if (!getJson("/api/sysinfo", d)) { snprintf(c.line1, sizeof(c.line1), "系统信息获取中…"); return false; }
      int mem = d["mem"]["pct"] | -1;
      int dsys = d["disk_sys"]["pct"] | -1;
      snprintf(c.line1, sizeof(c.line1), "DELL内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "系统盘 %d%%", dsys);
      c.has = true;
      break;
    }
    case CARD_VPS: {
      // VPS: line1=VPS内存 50%  line2=磁盘 47%
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "VPS 获取中…"); return false; }
      int mem = d["mem"]["pct"] | -1;
      int disk = d["disk_sys"]["pct"] | -1;
      snprintf(c.line1, sizeof(c.line1), "VPS内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "磁盘 %d%%", disk);
      c.has = true;
      break;
    }
    case CARD_VPSMEM: {
      // VPS内存(海洋): line1=VPS内存 50%  line2=磁盘 47%
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "内存获取中…"); return false; }
      int mem = d["mem"]["pct"] | -1;
      int disk = d["disk_sys"]["pct"] | -1;
      snprintf(c.line1, sizeof(c.line1), "VPS内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "磁盘 %d%%", disk);
      c.has = true;
      break;
    }
    case CARD_QUOTA: {
      // Claude额度: line1=Claude 89%(4h46m)  line2=99%(6d21h)
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "额度获取中…"); return false; }
      int p5 = d["claude"]["5h"]["pct"] | -1;
      int p7 = d["claude"]["7d"]["pct"] | -1;
      long r5 = d["claude"]["5h"]["reset_in_s"] | 0;
      long r7 = d["claude"]["7d"]["reset_in_s"] | 0;
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
