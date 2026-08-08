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
  CARD_HOLD,       // ETF持仓(总持仓/今日盈亏)
  CARD_GAIN,       // ETF收益(实盘收益/YTD)
  CARD_ACTION,     // ETF操作(今日无需操作/补仓信号)
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

// 场景 → 卡片映射(场景0=日期/天气原HUD, 指数连续2-5)
inline CardType cardForScene(int idx) {
  switch (idx % 10) {
    case 0: return CARD_DELL;     // 工作室 → 日期/天气(实际用drawPanels原HUD)
    case 1: return CARD_QUOTA;    // 客厅   → Claude额度
    case 2: return CARD_QUOTE;    // 卧室   → 标普500(指数0)
    case 3: return CARD_QUOTE;    // 高楼   → 纳指100(指数1)
    case 4: return CARD_QUOTE;    // 沙漠   → 上证指数(指数2)
    case 5: return CARD_QUOTE;    // 草原   → 恒生指数(指数3)
    case 6: return CARD_HOLD;     // 海洋   → 总持仓/今日盈亏
    case 7: return CARD_GAIN;     // 雪山   → 实盘收益/YTD
    case 8: return CARD_ACTION;   // 森林   → 今日无需操作
    default: return CARD_VPS;     // 太空   → VPS系统
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
      // 连续场景2-5→指数: 2→标普0, 3→纳指1, 4→上证2, 5→恒生3
      static const int qidx[10] = {0,0,0,1,2,3,0,0,0,0};  // 按场景序号映射指数
      JsonDocument d;
      if (!getJson("/api/indices", d)) { snprintf(c.line1, sizeof(c.line1), "行情获取中…"); return false; }
      JsonArray arr = d.as<JsonArray>();
      int n = arr.size(); if (n > 4) n = 4;
      int i = (sceneIdx >= 0 && sceneIdx < 10) ? qidx[sceneIdx] : 0;
      if (n > i) {
        const char* nm = arr[i]["name"] | "";
        // 用 as<> 显式读浮点/整数(避免 `| 0` 截断浮点)
        double pr = arr[i]["price"].as<double>(); double g = arr[i]["pct"].as<double>();
        snprintf(c.line1, sizeof(c.line1), "%s  %.0f", nm, pr);   // 名称+价格(空2格,无逗号)
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
    case CARD_HOLD: {
      // ETF持仓: 第1行=总持仓(万), 第2行=今日盈亏(红涨绿跌)
      // 数据源: etf-monitor(4004) /api/data, 总持仓=total_invested,
      //         今日盈亏=Σ(amount×change_pct)
      if (WiFi.status() != WL_CONNECTED) { snprintf(c.line1, sizeof(c.line1), "持仓获取中…"); return false; }
      WiFiClient client; HTTPClient http;
      if (!http.begin(client, "http://LAN_IP:4004/api/data")) { snprintf(c.line1, sizeof(c.line1), "持仓获取中…"); return false; }
      http.setTimeout(6000);
      int code = http.GET();
      if (code != 200) { http.end(); snprintf(c.line1, sizeof(c.line1), "持仓获取中…"); return false; }
      std::string body = http.getString().c_str();
      http.end();
      JsonDocument d;
      if (body.empty() || deserializeJson(d, body)) { snprintf(c.line1, sizeof(c.line1), "持仓获取中…"); return false; }
      double total = d["snapshot"]["total_invested"].as<double>();
      // 累加今日盈亏 = Σ(amount × change_pct)
      double dayPnl = 0;
      JsonArray det = d["snapshot"]["details"].as<JsonArray>();
      for (JsonObject h : det) {
        double amt = h["amount"].as<double>();
        double chg = h["change_pct"].as<double>();
        dayPnl += amt * chg / 100.0;
      }
      snprintf(c.line1, sizeof(c.line1), "总持仓 %.2f万", total / 10000.0);
      snprintf(c.line2, sizeof(c.line2), "盈亏 %+.0f元", dayPnl);
      c.trend = (dayPnl > 0.5) ? 1 : (dayPnl < -0.5) ? -1 : 0;  // 红涨绿跌
      c.has = true;
      break;
    }
    case CARD_GAIN: {
      // ETF收益: 第1行=实盘收益%, 第2行=YTD%
      if (WiFi.status() != WL_CONNECTED) { snprintf(c.line1, sizeof(c.line1), "收益获取中…"); return false; }
      WiFiClient client; HTTPClient http;
      if (!http.begin(client, "http://LAN_IP:4004/api/data")) { snprintf(c.line1, sizeof(c.line1), "收益获取中…"); return false; }
      http.setTimeout(6000);
      int code = http.GET();
      if (code != 200) { http.end(); snprintf(c.line1, sizeof(c.line1), "收益获取中…"); return false; }
      std::string body = http.getString().c_str();
      http.end();
      JsonDocument d;
      if (body.empty() || deserializeJson(d, body)) { snprintf(c.line1, sizeof(c.line1), "收益获取中…"); return false; }
      double gain = d["ytd_data"]["actual"]["overall_pct"].as<double>();
      double ytd = d["ytd_data"]["ytd_pct"].as<double>();
      snprintf(c.line1, sizeof(c.line1), "实盘收益 %+.2f%%", gain);
      snprintf(c.line2, sizeof(c.line2), "YTD %+.2f%%", ytd);
      c.trend = (gain > 0.005) ? 1 : (gain < -0.005) ? -1 : 0;
      c.has = true;
      break;
    }
    case CARD_ACTION: {
      // ETF操作: 无补仓信号→今日无需操作; 有信号→显示补仓建议
      if (WiFi.status() != WL_CONNECTED) { snprintf(c.line1, sizeof(c.line1), "操作获取中…"); return false; }
      WiFiClient client; HTTPClient http;
      if (!http.begin(client, "http://LAN_IP:4004/api/data")) { snprintf(c.line1, sizeof(c.line1), "操作获取中…"); return false; }
      http.setTimeout(6000);
      int code = http.GET();
      if (code != 200) { http.end(); snprintf(c.line1, sizeof(c.line1), "操作获取中…"); return false; }
      std::string body = http.getString().c_str();
      http.end();
      JsonDocument d;
      if (body.empty() || deserializeJson(d, body)) { snprintf(c.line1, sizeof(c.line1), "操作获取中…"); return false; }
      JsonArray sig = d["signals"].as<JsonArray>();
      if (sig.size() == 0) {
        snprintf(c.line1, sizeof(c.line1), "今日无需操作");
        snprintf(c.line2, sizeof(c.line2), "无补仓信号");
        c.has = true;
      } else {
        // 有补仓信号: 取第一条简短建议
        const char* msg = sig[0].as<const char*>();
        snprintf(c.line1, sizeof(c.line1), "补仓信号");
        snprintf(c.line2, sizeof(c.line2), "%s", msg ? msg : "有信号");
        c.has = true;
      }
      break;
    }
    default: break;
  }
  return c.has;
}

// 主动对话: 拉后端 /proactive 看克劳德是否要主动找主人说话。
// 返回 true 表示有主动消息(已存入 msg 静态缓冲), false 表示此刻不打扰。
// 省内存: 只用一块 80 字符静态缓冲, 不堆分配。
inline bool fetchProactive(char* msg, size_t n) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client; HTTPClient http;
  if (!http.begin(client, "http://LAN_IP:8787/proactive/girl")) return false;
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  std::string body = http.getString().c_str();
  http.end();
  if (body.empty()) return false;
  JsonDocument d;
  if (deserializeJson(d, body)) return false;
  if (!(d["has"] | false)) return false;
  const char* m = d["message"] | "";
  if (!m[0]) return false;
  snprintf(msg, n, "%s", m);
  return true;
}

}  // namespace Hub
