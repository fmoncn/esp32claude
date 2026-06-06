#pragma once
#include <M5Cardputer.h>
#include <LittleFS.h>
#include <cstring>
#include <string>
#include "sprites_meta.h"

// 从 LittleFS 按需读帧;逐像素直接写进目标画布缓冲,跳过色键 = 可靠透明 + 支持镜像。
class SpritePlayer {
 public:
  void begin(fs::FS& fs = LittleFS) { fs_ = &fs; }  // 精灵来源:内置 Flash 或 SD 卡

  static const ActionMeta* find(const char* name) {
    for (int i = 0; i < ACTION_COUNT; i++)
      if (strcmp(ACTIONS[i].name, name) == 0) return &ACTIONS[i];
    return nullptr;
  }

  void setAction(const char* name) {
    if (cur_ && strcmp(cur_->name, name) == 0) return;
    const ActionMeta* m = find(name);
    if (!m) return;
    if (file_) file_.close();
    std::string path = std::string("/sprites/") + name + ".bin";
    file_ = fs_->open(path.c_str(), "r");
    if (!file_) { cur_ = nullptr; return; }
    uint16_t n = 0;
    file_.read((uint8_t*)&n, 2);
    cur_ = m; nframes_ = n ? n : m->frames; idx_ = 0; done_ = false; lastMs_ = millis();
    loadFrame_(0);
  }

  const char* current() const { return cur_ ? cur_->name : ""; }
  bool finished() const { return done_; }

  void update(uint32_t now) {
    if (!cur_) return;
    const uint32_t interval = 1000 / (cur_->fps ? cur_->fps : 8);
    if (now - lastMs_ < interval) return;
    lastMs_ = now;
    if (idx_ + 1 >= nframes_) {
      if (cur_->loop) { idx_ = 0; loadFrame_(0); } else { done_ = true; }
    } else { idx_++; loadFrame_(idx_); }
  }

  // (x,y)=左上角;直接写画布缓冲,跳过 SPR_TRANSPARENT;facing=-1 水平镜像
  void draw(M5Canvas& dst, int x, int y, int facing = 1) {
    if (!cur_) return;
    uint16_t* db = (uint16_t*)dst.getBuffer();
    if (!db) return;
    const int dw = dst.width(), dh = dst.height();
    for (int j = 0; j < SPR_H; j++) {
      int dy = y + j;
      if (dy < 0 || dy >= dh) continue;
      uint16_t* drow = db + dy * dw;
      const uint16_t* srow = frameBuf_ + j * SPR_W;
      for (int i = 0; i < SPR_W; i++) {
        int dx = x + i;
        if ((unsigned)dx >= (unsigned)dw) continue;
        uint16_t px = srow[facing < 0 ? (SPR_W - 1 - i) : i];
        if (px != SPR_TRANSPARENT) drow[dx] = px;
      }
    }
  }

 private:
  void loadFrame_(uint16_t i) {
    if (!file_) return;
    file_.seek(2 + (size_t)i * SPR_W * SPR_H * 2);
    file_.read((uint8_t*)frameBuf_, SPR_W * SPR_H * 2);
  }

  uint16_t frameBuf_[SPR_W * SPR_H];
  fs::FS* fs_ = &LittleFS;
  fs::File file_;
  const ActionMeta* cur_ = nullptr;
  uint16_t nframes_ = 0, idx_ = 0;
  uint32_t lastMs_ = 0;
  bool done_ = false;
};
