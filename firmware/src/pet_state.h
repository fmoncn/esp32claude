#pragma once
#include <Arduino.h>
#include <string>

// 后端返回的情绪 -> 精灵动作名
inline const char* emotionToAction(const std::string& e) {
  if (e == "happy") return "happy";
  if (e == "sad") return "sad";
  if (e == "angry") return "angry";
  if (e == "surprised") return "surprised";
  if (e == "thinking") return "thinking";
  if (e == "sleepy") return "sleepy";
  if (e == "love" || e == "in_love") return "in_love";
  if (e == "excited") return "victory";
  return "idle";  // neutral / 未知
}

// 作息:按小时挑日常动作(hour<0 表示时间未同步)
inline const char* scheduleAction(int hour, uint32_t ms) {
  const bool altA = (ms / 8000) % 2 == 0;  // 每 8s 在两个工作动作间切
  if (hour < 0) return "idle";
  if (hour < 6) return "sleeping";
  if (hour < 7) return "idle";
  if (hour < 8) return "eating";
  if (hour < 11) return altA ? "reading" : "thinking";   // 上午工作
  if (hour < 12) return "playing";                        // 摸鱼
  if (hour < 13) return "eating";                         // 午餐
  if (hour < 14) return "playing";                        // 午休摸鱼
  if (hour < 18) return altA ? "reading" : "thinking";    // 下午工作
  if (hour < 19) return "eating";                         // 晚餐
  if (hour < 21) return "exercising";                     // 健身
  if (hour < 22) return "dancing";                        // 陪伴
  if (hour < 23) return "playing";                        // 深夜摸鱼
  return "sleeping";
}

// Fn + 数字 -> 炫技动作
inline const char* hotkeyAction(char c) {
  switch (c) {
    case '1': return "blast";
    case '2': return "chest_blast";
    case '3': return "missile_launch";
    case '4': return "laser_attack";
    case '5': return "flying";
    case '6': return "dancing";
    case '7': return "victory";
    case '8': return "waving";
    case '9': return "jumping";
    case '0': return "running";
    default: return nullptr;
  }
}

// 漫游中停下来做的事(按作息 + 随机)
inline const char* pickActivity(int hour) {
  if (hour < 7 || hour >= 23) return "sleeping";
  if (hour == 8 || hour == 12 || hour == 18) return (random(10) < 6) ? "eating" : "idle";
  if ((hour >= 9 && hour < 12) || (hour >= 14 && hour < 18)) {
    const char* a[] = {"reading", "thinking", "playing", "idle", "drinking"};
    return a[random(5)];
  }
  if (hour >= 19 && hour < 21) { const char* a[] = {"exercising", "dancing", "idle"}; return a[random(3)]; }
  if (hour >= 21) { const char* a[] = {"playing", "idle", "waving"}; return a[random(3)]; }
  const char* a[] = {"idle", "waving", "playing"};
  return a[random(3)];
}
