#pragma once
#include <M5Cardputer.h>
#include <cstring>

// 抚摸交互 — 用 BMI270 加速度计检测主人手势(纯软件算法, 零新增堆分配)
// 省内存: 只用 ~48 字节静态变量(状态机), 无 malloc/vector
// 手势(需从静止状态开始, 避免操作设备误触):
//   抚摸(PET ): 上下移动 3 次 或 左右移动 3 次以上 → 眯眼开心
//   粗暴(ABUSE): 剧烈运动(总G大偏差) 3 次以上 → 头晕/生气
// 触发回调给 main.cpp 播放动画/音效, 与语音输入(麦克风 I2S)硬件独立, 互不干扰
namespace PetTouch {

enum Gesture { NONE = 0, PET, ABUSE };

// 状态机: 不占堆
struct Detector {
  // 静止基准(设备静止时的加速度)
  float refZ = 1.0f;      // 静止时 Z 轴基准(重力, 设备平放≈1g)
  float refX = 0.0f;      // 静止时 X 轴基准
  // 方向计数
  int8_t dirZ = 0;        // Z 轴偏移方向: +1(向上偏) / -1(向下偏) / 0(接近基准)
  int8_t dirX = 0;        // X 轴偏移方向: +1(向左) / -1(向右) / 0
  uint8_t halfZ = 0;      // Z 轴已完成半程次数(2半程=1次移动)
  uint8_t halfX = 0;      // X 轴已完成半程次数
  uint8_t upDown = 0;     // 上下移动完整次数
  uint8_t leftRight = 0;  // 左右移动完整次数
  uint8_t abuseCount = 0; // 剧烈运动次数
  uint32_t lastMs = 0;    // 上次采样时间
  uint32_t stillAccum = 0;// 静止持续时间累积(前置: 需先静止)
  uint32_t lastMoveMs = 0;// 上次检测到移动的时间(超时重置计数)
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
  float total = sqrtf(ax*ax + ay*ay + az*az);  // 总 G 值

  // 冷却期: 不重复触发
  if (now < det.cooldown) return NONE;

  // 静止检测: 总G接近1(±0.12) 且 水平接近0(<0.12) = 设备稳定静止
  bool still = (fabsf(total - 1.0f) < 0.12f) && (sqrtf(ax*ax + ay*ay) < 0.12f);
  if (still) {
    det.stillAccum++;
    if (det.stillAccum >= 10) {          // 静止 ≥500ms → 记录基准 + 进入可检测
      det.refZ = az; det.refX = ax;
      det.upDown = 0; det.leftRight = 0; det.abuseCount = 0;
      det.halfZ = 0; det.halfX = 0;
    }
    return NONE;
  }

  // 设备在动 → 停止静止累积, 需重新静止才可检测(防操作设备误触)
  det.stillAccum = 0;
  // 需已静止过(基准已建立)才开始计数
  if (det.refZ == 0.0f && det.refX == 0.0f) return NONE;  // 初始基准未建立

  // 移动超时重置(超过 2 秒没连续动作 → 清计数, 防跨时段误计)
  if (now - det.lastMoveMs > 2000) { det.upDown = 0; det.leftRight = 0; det.abuseCount = 0; det.halfZ = 0; det.halfX = 0; }
  det.lastMoveMs = now;

  // --- 方向计数(上下/左右移动次数) ---
  // Z 轴相对基准偏移(上下): >0.25 → 向上, <-0.25 → 向下
  float dz = az - det.refZ;
  int8_t nz = (dz > 0.25f) ? 1 : (dz < -0.25f) ? -1 : 0;
  if (nz != 0 && nz != det.dirZ) {        // 方向变化(从+到-或从-到+)
    if (det.dirZ != 0) det.halfZ++;       // 完成半程
    det.dirZ = nz;
    if (det.halfZ >= 2) { det.halfZ = 0; det.upDown++; }  // 2半程=1次上下移动
  }
  // X 轴相对基准偏移(左右): >0.25 → 左, <-0.25 → 右
  float dx = ax - det.refX;
  int8_t nx = (dx > 0.25f) ? 1 : (dx < -0.25f) ? -1 : 0;
  if (nx != 0 && nx != det.dirX) {
    if (det.dirX != 0) det.halfX++;
    det.dirX = nx;
    if (det.halfX >= 2) { det.halfX = 0; det.leftRight++; }
  }

  // --- 粗暴: 总G大偏差(剧烈运动) ---
  // 剧烈运动时总G偏差大(>0.8g), 每次触发一个"冲击"算一次
  if (fabsf(total - 1.0f) > 0.8f) {
    det.abuseCount++;
    if (det.abuseCount >= 3) {           // 剧烈运动 3 次
      det.abuseCount = 0;
      det.cooldown = now + 3000;
      return ABUSE;
    }
  }

  // --- 抚摸: 上下 3 次 或 左右 3 次以上 ---
  if (det.upDown >= 3 || det.leftRight >= 3) {
    det.upDown = 0; det.leftRight = 0;
    det.cooldown = now + 1200;
    return PET;
  }

  return NONE;
}

}  // namespace PetTouch
