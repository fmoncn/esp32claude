#pragma once
#include <M5Cardputer.h>
#include <cstring>

// 抚摸交互 — 用 BMI270 加速度计检测主人手势(纯软件算法, 零新增堆分配)
// 省内存: 只用 ~60 字节静态变量(环形缓冲 + 状态机), 无 malloc/vector
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
  // 幅度环形缓冲(检测高频小幅度抚摸)
  float mag[16];          // 64 字节 (x/y 合成幅度历史)
  uint8_t idx = 0;        // 环形缓冲索引
  // 状态
  float prevMag = 0;      // 上次合成幅度
  float prevG = 0;        // 上次总 G 值
  uint32_t lastMs = 0;    // 上次采样时间
  uint32_t petAccum = 0;  // 抚摸持续时间累积(检测持续抚摸)
  uint32_t rockAccum = 0; // 摇晃持续时间累积
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
  float horiz = (ax * ax + ay * ay);        // 水平加速度平方(重力在z)
  float horizMag = sqrtf(horiz);
  float total = sqrtf(ax*ax + ay*ay + az*az);  // 总 G 值(检测冲击)

  // 存环形缓冲(检测高频小幅度)
  det.mag[det.idx] = horizMag;
  det.idx = (det.idx + 1) % 16;

  // 冷却期: 不重复触发
  if (now < det.cooldown) return NONE;

  // --- 粗暴: 总G值相对重力(1.0)的偏差飙升 → 被摔/剧烈摇晃 ---
  if (fabsf(total - 1.0f) > 0.9f) {   // 冲击 >0.9g 偏差(被摔/重晃)
    det.cooldown = now + 3000;         // 3秒冷却
    return ABUSE;
  }

  // --- 抚摸: 水平方向高频小幅度波动(主人轻轻摸, 手抖造成微小晃动) ---
  // 水平幅度在 0.05~0.35 之间波动, 且波动频率高(相邻采样变化大)
  {
    float p = det.prevMag;
    float delta = fabsf(horizMag - p);
    if (horizMag < 0.35f && delta > 0.02f && delta < 0.2f) {
      det.petAccum++;
      if (det.petAccum >= 8) {         // 持续约 400ms 抚摸
        det.petAccum = 0;
        det.cooldown = now + 800;      // 0.8秒冷却
        det.rockAccum = 0;
        return PET;
      }
    } else {
      det.petAccum = 0;
    }
  }
  det.prevMag = horizMag;

  // --- 摇晃: 总G值低频大幅度摇摆(抱在怀里左右/上下晃) ---
  // 总G值在 0.3~1.7 之间摆动, 幅度大且持续
  {
    float gd = fabsf(total - 1.0f);
    if (gd > 0.25f && gd < 0.9f) {     // 明显摇晃(不是抚摸的小波动, 不是粗暴的冲击)
      det.rockAccum++;
      if (det.rockAccum >= 12) {       // 持续约 600ms 摇晃
        det.rockAccum = 0;
        det.cooldown = now + 1500;     // 1.5秒冷却
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
