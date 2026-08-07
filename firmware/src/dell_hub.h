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

// 格式化重置剩余时间: <1天→小时(x.xh), 否则→天(x.xd)
static void fmtReset(long sec, char* buf, int n) {
  if (sec < 86400) snprintf(buf, n, "%.1fh", sec / 3600.0);
  else snprintf(buf, n, "%.1fd", sec / 86400.0);
}

// 拉取并填充当前场景对应的卡片数据
inline bool fetch(int sceneIdx) {
  cardOfScene() = cardForScene(sceneIdx);
  Card& c = cur();
  c.has = false;
  c.line1[0] = c.line2[0] = 0;

  switch (cardOfScene()) {
    case CARD_QUOTE: {
      // 行情: line1=标普 +0.47%  line2=纳指 +0.90% (去掉价格, 精简)
      JsonDocument d;
      if (!getJson("/api/indices", d)) { snprintf(c.line1, sizeof(c.line1), "行情获取中…"); return false; }
      JsonArray arr = d.as<JsonArray>();
      int n = arr.size(); if (n > 4) n = 4;
      if (n > 0) {
        JsonObject o0 = arr[0]; JsonObject o1 = arr[1];
        const char* n0 = o0["name"] | ""; const char* n1 = o1["name"] | "";
        float g0 = o0["pct"] | 0; float g1 = o1["pct"] | 0;
        snprintf(c.line1, sizeof(c.line1), "%s %+.1f%%", n0, g0);
        if (n > 1) snprintf(c.line2, sizeof(c.line2), "%s %+.1f%%", n1, g1);
        c.has = true;
      }
      break;
    }
    case CARD_DELL: {
      // Dell系统: line1=CPU 8% 内存47%  line2=盘50/15 运行69d
      JsonDocument d;
      if (!getJson("/api/sysinfo", d)) { snprintf(c.line1, sizeof(c.line1), "系统信息获取中…"); return false; }
      int cpu = d["cpu"]["pct"] | -1;
      int mem = d["mem"]["pct"] | -1;
      int dsys = d["disk_sys"]["pct"] | -1;
      int ddat = d["disk_data"]["pct"] | -1;
      float up = d["uptime_days"] | -1;
      snprintf(c.line1, sizeof(c.line1), "CPU %d%% 内存%d%%", cpu, mem);
      snprintf(c.line2, sizeof(c.line2), "盘%d/%d 运行%.0fd", dsys, ddat, up);
      c.has = true;
      break;
    }
    case CARD_VPS: {
      // VPS系统: line1=CPU 2% 磁盘47%  line2=内存 60%
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "VPS 获取中…"); return false; }
      int cpu = d["cpu"]["pct"] | -1;
      int disk = d["disk_sys"]["pct"] | -1;
      int mem = d["mem"]["pct"] | -1;
      snprintf(c.line1, sizeof(c.line1), "CPU %d%% 磁盘%d%%", cpu, disk);
      snprintf(c.line2, sizeof(c.line2), "内存 %d%%", mem);
      c.has = true;
      break;
    }
    case CARD_VPSMEM: {
      // VPS内存: line1=内存 60%  line2=0.6/0.9G
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "内存获取中…"); return false; }
      int mem = d["mem"]["pct"] | -1;
      float used = d["mem"]["used_gb"] | 0;
      float total = d["mem"]["total_gb"] | 0;
      snprintf(c.line1, sizeof(c.line1), "内存 %d%%", mem);
      snprintf(c.line2, sizeof(c.line2), "%.1f/%.1fG", used, total);
      c.has = true;
      break;
    }
    case CARD_QUOTA: {
      // Claude额度: line1=5|100%|4.9h  line2=7|0%|6.9d
      JsonDocument d;
      if (!getJson("/api/sysinfo/vps", d)) { snprintf(c.line1, sizeof(c.line1), "额度获取中…"); return false; }
      int p5 = d["claude"]["5h"]["pct"] | -1;
      int p7 = d["claude"]["7d"]["pct"] | -1;
      long r5 = d["claude"]["5h"]["reset_in_s"] | 0;
      long r7 = d["claude"]["7d"]["reset_in_s"] | 0;
      char r5s[8], r7s[8];
      fmtReset(r5, r5s, sizeof(r5s));
      fmtReset(r7, r7s, sizeof(r7s));
      snprintf(c.line1, sizeof(c.line1), "5|%d%%|%s", p5, r5s);
      snprintf(c.line2, sizeof(c.line2), "7|%d%%|%s", p7, r7s);
      c.has = true;
      break;
    }
    default: break;
  }
  return c.has;
}

}  // namespace Hub
