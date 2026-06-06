#pragma once
#include <M5Cardputer.h>
#include <time.h>
#include <math.h>
#include <cstring>
#include "weather.h"

// 混搭风「像素全息工程台」:深蓝底(蓝图) + 青线框网格 + 霓虹色(赛博) + 像素块 + 金(角色)。
// 10 场景(室内3 + 室外7),全息投影概念,统一风格。从已验证的网页预览 1:1 移植。
namespace Scenes {
static const int BOT = 92, GROUND = 90;  // 场景区 0..92,角色脚 90

struct Meta { const char* name; bool indoor; int hz; };
static const Meta SCN[] = {
  {"工作室", 1, 66}, {"客厅", 1, 66}, {"卧室", 1, 64},
  {"高楼大厦", 0, 74}, {"沙漠", 0, 60}, {"草原", 0, 60}, {"海洋", 0, 56},
  {"雪山", 0, 70}, {"森林", 0, 64}, {"太空", 0, 50},
};
inline int count() { return sizeof(SCN) / sizeof(SCN[0]); }
inline const char* name(int i) { return SCN[i].name; }
inline bool indoor(int i) { return SCN[i].indoor; }
// 作息自动选室内场景:夜→卧室,午/晚→客厅,白天→工作室
inline int autoIdx(int h) { if (h < 0) return 0; if (h >= 23 || h < 7) return 2; if (h == 12 || h == 13 || (h >= 18 && h < 22)) return 1; return 0; }

/* ---------- 原语 ---------- */
inline void sky(M5Canvas& c, int hz, bool night) {
  int r0 = night ? 5 : 7, g0 = night ? 7 : 11, b0 = night ? 15 : 24;
  int r1 = night ? 9 : 13, g1 = night ? 18 : 29, b1 = night ? 34 : 51;
  for (int y = 0; y < hz; y++) { float f = (float)y / hz;
    c.drawFastHLine(0, y, 240, c.color565(r0 + (r1 - r0) * f, g0 + (g1 - g0) * f, b0 + (b1 - b0) * f)); }
  c.fillRect(0, hz, 240, BOT - hz, c.color565(10, 20, 34));  // 地面填实(清残影)
}
inline void grid(M5Canvas& c, int hz) {
  uint16_t g = c.color565(24, 70, 95);
  for (int i = -6; i <= 6; i++) c.drawLine(120 + i * 7, hz, 120 + i * 40, BOT, g);
  for (int r = 0; r < 5; r++) { int y = hz + r * r * 1.6 + 2; if (y < BOT) c.drawFastHLine(0, y, 240, g); }
}
inline void stars(M5Canvas& c, int n, int h) {
  for (int i = 0; i < n; i++) { int x = (i * 73) % 240, y = (i * 37) % h;
    c.drawPixel(x, y, c.color565(150 + (i % 3) * 35, 190 + (i % 3) * 20, 255)); }
}
inline void wxIcon(M5Canvas& c, int x, int y, const char* cat, float t) {
  uint16_t gold = c.color565(255, 200, 58), cl = c.color565(53, 214, 255);
  auto cloud = [&](uint16_t col) { c.fillRect(x + 3, y + 8, 11, 4, col); c.fillRect(x + 5, y + 5, 7, 4, col); };
  if (!strcmp(cat, "sun")) { c.fillRect(x + 5, y + 5, 6, 6, gold);
    for (int a = 0; a < 8; a++) { float ang = a * 0.785f + t * .5f; c.fillRect(x + 8 + cosf(ang) * 7, y + 8 + sinf(ang) * 7, 2, 2, gold); } }
  else if (!strcmp(cat, "suncloud")) { c.fillRect(x + 8, y + 3, 5, 5, gold); cloud(c.color565(159, 198, 230)); }
  else if (!strcmp(cat, "cloud")) cloud(c.color565(143, 176, 204));
  else if (!strcmp(cat, "fog")) { for (int i = 0; i < 4; i++) c.fillRect(x + 2, y + 4 + i * 3, 12, 1, c.color565(159, 182, 204)); }
  else if (!strcmp(cat, "rain")) { cloud(c.color565(127, 158, 192)); for (int i = 0; i < 3; i++) c.fillRect(x + 4 + i * 4, y + 12, 1, 3, cl); }
  else if (!strcmp(cat, "snow")) { cloud(c.color565(184, 207, 230)); for (int i = 0; i < 3; i++) c.fillRect(x + 4 + i * 4, y + 13, 2, 2, c.color565(255, 255, 255)); }
  else { cloud(c.color565(127, 158, 192)); c.fillRect(x + 7, y + 11, 2, 5, gold); }
}

/* ---------- 10 个场景元素 ---------- */
inline void scStudio(M5Canvas& c, float t, int hz) {
  uint16_t grn = c.color565(60, 200, 130), ln = c.color565(53, 214, 255), gd = c.color565(255, 200, 58);
  for (int col = 0; col < 6; col++) { int x = 12 + col * 9;
    for (int i = 0; i < 6; i++) { int y = ((i * 11 + (int)(t * 22) + col * 7) % 62); c.fillRect(x, y, 1, 4, grn); } }
  c.drawRect(150, 18, 72, 30, ln);
  int cx = 186, cy = 36;
  for (int k = 0; k < 3; k++) { float rx = fabsf(cosf(t * .9f + k)) * 16; c.drawEllipse(cx, cy, (int)(rx < 2 ? 2 : rx), 7, ln); }
  c.fillRect(20, hz - 12, 40, 12, c.color565(60, 50, 40)); c.drawRect(20, hz - 12, 40, 12, gd);
  c.drawRect(64, hz - 8, 18, 8, ln);
}
inline void scLiving(M5Canvas& c, float t, int hz) {
  uint16_t ln = c.color565(53, 214, 255), ne = c.color565(255, 58, 140), gd = c.color565(255, 200, 58);
  c.drawRect(14, 12, 64, 34, ln);
  for (int i = 0; i <= 64; i += 2) { int y = 29 + (int)(sinf(i * .25f + t * 4) * 9); c.drawPixel(14 + i, y, ln); }
  c.fillRect(150, hz - 16, 76, 16, c.color565(90, 30, 60)); c.drawRect(150, hz - 16, 76, 16, ne);
  c.drawRect(150, hz - 26, 12, 12, ne);
  c.drawLine(96, hz, 96, hz - 28, gd); c.fillCircle(96, hz - 30, 4, gd);
}
inline void scBedroom(M5Canvas& c, float t, int hz) {
  uint16_t ln = c.color565(53, 214, 255);
  c.fillRect(150, 16, 40, 30, c.color565(6, 10, 22)); stars(c, 16, 46);
  c.fillCircle(176, 26, 5, c.color565(232, 236, 255)); c.drawRect(150, 16, 40, 30, ln);
  c.fillRect(20, hz - 14, 76, 14, c.color565(30, 50, 80)); c.drawRect(20, hz - 14, 76, 14, ln);
  c.fillRect(24, hz - 22, 16, 8, c.color565(207, 224, 240));
}
inline void scCity(M5Canvas& c, float t, int hz) {
  uint16_t gd = c.color565(255, 200, 58);
  for (int i = 0; i < 10; i++) { int bw = 16 + ((i * 53) % 14), bh = 18 + ((i * 97) % 40), bx = i * 25 - 6, by = hz - bh;
    c.drawRect(bx, by, bw, bh, i % 2 ? c.color565(53, 214, 255) : c.color565(255, 58, 140));
    for (int wx = bx + 3; wx < bx + bw - 2; wx += 5) for (int wy = by + 3; wy < hz - 3; wy += 6)
      if ((wx * 7 + wy * 13 + i) % 5 < 2) c.fillRect(wx, wy, 2, 3, gd); }
  int fx = (int)(t * 40) % 260 - 10; c.fillRect(fx, 22, 4, 1, c.color565(255, 58, 140));
}
inline void scDesert(M5Canvas& c, float t, int hz) {
  uint16_t gd = c.color565(255, 200, 58), ln = c.color565(53, 214, 255);
  for (int k = 0; k < 3; k++) c.drawCircle(192, 20, 6 + k * 4, gd);
  for (int d = 0; d < 3; d++) { uint16_t col = c.color565(255 - d * 30, 200 - d * 30, 100 - d * 20); int py = hz + d * 9;
    for (int x = 0; x <= 240; x += 8) { int y = hz + d * 9 - (int)(sinf(x * .03f + d) * 7); if (x) c.drawLine(x - 8, py, x, y, col); py = y; } }
  for (int i = 0; i < 5; i++) c.fillRect((i * 60 + (int)(t * 10)) % 240, hz - 6, 8, 1, c.color565(255, 220, 150));
  c.drawLine(40, hz, 40, hz - 7, ln);
}
inline void scGrass(M5Canvas& c, float t, int hz) {
  uint16_t hill = c.color565(60, 150, 200), gr = c.color565(94, 224, 138), gd = c.color565(255, 200, 58);
  int py = hz; for (int x = 0; x <= 240; x += 24) { int y = hz - 10 - (int)(sinf(x * .04f) * 7); if (x) c.drawLine(x - 24, py, x, y, hill); py = y; }
  for (int i = 0; i < 40; i++) { int x = (i * 23) % 240, y = hz + 2 + (i * 31) % 26, sw = (int)(sinf(t * 2 + i) * 2); c.drawLine(x, y, x + sw, y - 5, gr); }
  c.fillRect(196, 16, 8, 8, gd);
}
inline void scOcean(M5Canvas& c, float t, int hz) {
  uint16_t gd = c.color565(255, 200, 58);
  for (int r = 0; r < 6; r++) { uint16_t col = c.color565(53 - r * 6, 214 - r * 24, 255 - r * 20); int py = hz + r * 6;
    for (int x = 0; x <= 240; x += 4) { int y = hz + r * 6 + (int)(sinf(x * .12f + t * 1.4f + r) * 2); if (x) c.drawLine(x - 4, py, x, y, col); py = y; } }
  c.fillRect(196, 16, 7, 7, gd);
  for (int i = 0; i < 10; i++) c.fillRect((i * 53 + (int)(t * 30)) % 240, hz + 6 + (i % 4) * 7, 2, 1, c.color565(255, 255, 255));
}
inline void scSnow(M5Canvas& c, float t, int hz) {
  for (int k = 0; k < 3; k++) { uint16_t col = c.color565(120 - k * 20, 200 - k * 20, 255 - k * 20);
    c.drawLine(0 + k * 30, hz, 40 + k * 60, hz - 30 - k * 6, col); c.drawLine(40 + k * 60, hz - 30 - k * 6, 90 + k * 60, hz, col); }
  uint16_t au = c.color565(120, 255, 200); int py = 14;
  for (int x = 0; x <= 240; x += 6) { int y = 14 + (int)(sinf(x * .05f + t) * 4); if (x) c.drawLine(x - 6, py, x, y, au); py = y; }
  for (int i = 0; i < 24; i++) { int x = (i * 47 + (int)(t * 8)) % 240, y = (i * 29 + (int)(t * 20)) % hz; c.drawPixel(x, y, c.color565(255, 255, 255)); }
}
inline void scForest(M5Canvas& c, float t, int hz) {
  uint16_t tr = c.color565(94, 224, 138), gd = c.color565(255, 200, 58);
  for (int i = 0; i < 7; i++) { int x = 10 + i * 34, bh = 20 + (i % 3) * 8; c.drawLine(x, hz, x, hz - bh, tr);
    for (int k = 0; k < 3; k++) c.drawRect(x - 8 + k, hz - bh - 6 + k * 5, 16 - k * 2, 8, tr); }
  for (int i = 0; i < 8; i++) { int x = (int)((i * 53 + sinf(t + i) * 20 + 240)) % 240, y = 30 + (int)(cosf(t * .7f + i) * 16); c.fillRect(x, y, 2, 2, gd); }
}
inline void scSpace(M5Canvas& c, float t, int hz) {
  uint16_t ln = c.color565(53, 214, 255), gd = c.color565(255, 200, 58);
  stars(c, 40, BOT);
  c.fillCircle(150, 24, 13, c.color565(58, 74, 138)); c.drawEllipse(150, 24, 21, 7, gd);
  int mx = (int)(t * 60) % 280 - 20; c.drawLine(mx, 14, mx - 10, 20, c.color565(255, 255, 255));
  for (int k = 0; k < 2; k++) c.drawEllipse(200, 44, 18 - k * 6, 8 - k * 3, ln);
}

inline void drawScene(M5Canvas& c, float t, int idx, int hz) {
  switch (idx) {
    case 0: scStudio(c, t, hz); break; case 1: scLiving(c, t, hz); break; case 2: scBedroom(c, t, hz); break;
    case 3: scCity(c, t, hz); break; case 4: scDesert(c, t, hz); break; case 5: scGrass(c, t, hz); break;
    case 6: scOcean(c, t, hz); break; case 7: scSnow(c, t, hz); break; case 8: scForest(c, t, hz); break;
    case 9: scSpace(c, t, hz); break;
  }
}

/* ---------- 背景:天空+场景+网格+落地光圈 ---------- */
inline void draw(M5Canvas& c, uint32_t ms, int idx, int roamX) {
  float t = ms / 1000.0f;
  int hz = SCN[idx].hz;
  struct tm tmv; int h = getLocalTime(&tmv, 0) ? tmv.tm_hour : -1;
  bool night = !SCN[idx].indoor && (h >= 20 || (h >= 0 && h < 6));
  sky(c, hz, night);
  drawScene(c, t, idx, hz);
  grid(c, hz);
  int scan = (int)(t * 16) % BOT; c.drawFastHLine(0, scan, 240, c.color565(40, 110, 150));  // 扫描线
  c.drawEllipse(roamX, GROUND, 24, 4, c.color565(53, 160, 200));                            // 落地光圈
}

/* ---------- 统一 HUD:时钟/日期/天气/亲密度(全真实) ---------- */
inline void drawPanels(M5Canvas& c, int idx, int intimacy) {
  const int X = 4, Y = 4, W = 124, H = 42;
  uint16_t bgp = c.color565(8, 18, 34), ln = c.color565(53, 214, 255), scr = c.color565(223, 244, 255),
           ne = c.color565(255, 58, 140), dim = c.color565(120, 150, 175);
  c.fillRect(X, Y, W, H, bgp); c.drawRect(X, Y, W, H, ln);
  static const char* WD[] = {"日", "一", "二", "三", "四", "五", "六"};
  struct tm tmv; bool ht = getLocalTime(&tmv, 0);
  char hhmm[8], date[24];
  if (ht) { snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    snprintf(date, sizeof(date), "%d/%d 周%s", tmv.tm_mon + 1, tmv.tm_mday, WD[(tmv.tm_wday >= 0 && tmv.tm_wday < 7) ? tmv.tm_wday : 0]); }
  else { strcpy(hhmm, "--:--"); strcpy(date, "--"); }
  c.setFont(&fonts::efontCN_12);
  // 行1:时钟 + 天气图标 + 温度
  c.setTextColor(scr, bgp); c.setCursor(X + 4, Y + 2); c.print(hhmm);
  wxIcon(c, X + 66, Y + 1, WX::cur().cat, (float)millis() / 1000);
  c.setTextColor(scr, bgp); c.setCursor(X + 86, Y + 2);
  if (WX::cur().t > -100) c.printf("%d°", WX::cur().t); else c.print("--");
  // 行2:日期
  c.setTextColor(dim, bgp); c.setCursor(X + 4, Y + 14); c.print(date);
  // 行3:城市 + 天气描述(城市来自 IP 定位,确认定位对不对)
  c.setTextColor(ln, bgp); c.setCursor(X + 4, Y + 26);
  if (WX::cityBuf()[0]) { c.print(WX::cityBuf()); c.print(" "); }
  c.print(WX::cur().label);
  // 亲密度细条
  c.fillRect(X + 4, Y + 38, W - 8, 2, c.color565(40, 50, 64));
  if (intimacy >= 0) c.fillRect(X + 4, Y + 38, (W - 8) * (intimacy > 100 ? 100 : intimacy) / 100, 2, ne);
}

}  // namespace Scenes
