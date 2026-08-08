#pragma once
#include <M5Cardputer.h>
#include <cstring>

// 抚摸交互 — 用 BMI270 加速度计检测主人手势(纯软件算法, 零新增堆分配)
// 省内存: 只用 ~32 字节静态变量(状态机), 无 malloc/vector
// 手势:
//   抚摸(PET  ): X/Y 轴高频小幅度正弦 → 眯眼开心(降焦虑)
//   摇晃(ROCK ): Z/X 轴低频大幅度摇摆 → 安抚入睡(开心)
//   粗暴(ABUSE): 冲击值(G) 飙升 → 头晕/生气(降亲密度)
// 触发回调给 main.cpp 播放动画/音效, 与语音输入(麦克风 I2S)硬件独立, 互不干扰
namespace PetTouch {

// 手势类型(回调用)
enum Gesture { NONE = 0, PET, ROCK, ABUSE };

// 手势回调: 触发时返回对应手势; 每帧调用, 空闲时检测
// 静态状态机, 不占堆
struct Detector {
  float prevMag = 0;      // 上次合成水平幅度
  uint32_t lastMs = 0;    // 上次采样时间
  uint32_t petAccum = 0;  // 抚摸持续时间累积(检测持续抚摸)
  uint32_t rockAccum = 0; // 摇晃持续时间累积
  uint32_t stillAccum = 0;// 静止持续时间累积(抚摸/摇晃前置: 需先静止)
  uint32_t cooldown = 0;  // 手势触发后冷却(防反复)
  bool init = false;
};

static Detector det;

// 初始化(需在 setup 调用一次)
inline void init() {
  if (!M5.Imu.begin()) return;
  std::memset(&det, 0, sizeof(det));
  det.lastMs = millis();
}

// 每帧调用: 检测手势, 返回 NONE 或触发的手势。空闲时调用, 不阻塞。
inline Gesture update() {
  if (!M5.Imu.isEnabled()) return NONE;
  uint32_t now = millis();
  if (now - det.lastMs < 50) return NONE;   // 50ms 采样一次(20Hz)
  det.lastMs = now;

  float ax, ay, az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return NONE;

  // 合成水平幅度(去重力, 检测水平晃动)
  float horizMag = sqrtf(ax*ax + ay*ay);        // 水平加速度(重力在z)
  float total = sqrtf(ax*ax + ay*ay + az*az);   // 总 G 值(检测冲击/摇晃)

  // 冷却期: 不重复触发
  if (now < det.cooldown) return NONE;

  // 静止检测: 总G接近1(±0.15) 且 水平接近0(<0.12) = 设备稳定静止。
  // 抚摸/摇晃需从静止开始, 避免操作设备时误触。
  bool still = (fabsf(total - 1.0f) < 0.15f) && (horizMag < 0.12f);
  if (still) det.stillAccum++; else det.stillAccum = 0;

  // --- 粗暴: 总G值相对重力(1.0)的偏差飙升(被摔/剧烈摇晃), 无需静止前置 ---
  if (fabsf(total - 1.0f) > 1.1f) {     // 强冲击 >1.1g(真正被摔/重晃)
    det.cooldown = now + 3000;
    return ABUSE;
  }

  // 抚摸/摇晃需设备先静止一段时间(约500ms), 确认是主动来摸而非随手操作
  if (det.stillAccum < 10) {            // 需静止 ≥500ms
    det.petAccum = 0; det.rockAccum = 0;
    return NONE;
  }

  // --- 抚摸: 水平方向明显波动(主人轻轻摸) ---
  // 水平幅度在 0.08~0.5 之间波动, 且相邻采样变化明显(>0.04), 持续约800ms
  {
    float p = det.prevMag;
    float delta = fabsf(horizMag - p);
    if (horizMag > 0.08f && horizMag < 0.5f && delta > 0.04f && delta < 0.35f) {
      det.petAccum++;
      if (det.petAccum >= 16) {         // 持续约 800ms 抚摸
        det.petAccum = 0;
        det.cooldown = now + 1200;      // 1.2秒冷却
        det.rockAccum = 0;
        return PET;
      }
    } else {
      det.petAccum = 0;
    }
  }
  det.prevMag = horizMag;

  // --- 摇晃: 总G值明显大幅度摇摆(抱怀里摇) ---
  {
    float gd = fabsf(total - 1.0f);
    if (gd > 0.35f && gd < 1.1f) {      // 明显摇晃(不是抚摸的小波动, 不是粗暴冲击)
      det.rockAccum++;
      if (det.rockAccum >= 20) {        // 持续约 1s 摇晃
        det.rockAccum = 0;
        det.cooldown = now + 2000;      // 2秒冷却
        det.petAccum = 0;
        return ROCK;
      }
    } else {
      det.rockAccum = 0;
    }
  }

  return NONE;
}

}  // namespace PetTouch
