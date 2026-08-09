#pragma once
#include <M5Cardputer.h>

// 克劳德三连音交互系统 — 全部用内置 tone,零运行期内存(Flash 常量音符表)
//
// 设计: 每个音效是 2-3 个音符的轻柔旋律, 配合克劳德的文字回复/动画,
//       让交互更像电子宠物。播放完自动静音(消 NS4150 电流声)。
// 用法: Tunes::play(Tunes::GREET);  // 主动问候三连音
namespace Tunes {

// 音符序列类型: {频率Hz, 时长ms}
struct Note { uint16_t freq; uint16_t dur; };

// ---- 场景音效(每个都是轻柔旋律, 2-3音) ----
// 主动问候: 上行琶音 C5-E5-G5 (温暖呼唤)
static const Note GREET[] = { {523,150}, {659,150}, {784,200} };
// 回复对话: 下行 G5-E5-C5 (附和回应)
static const Note REPLY[] = { {784,120}, {659,120}, {523,180} };
// 抚摸开心: 高八度上行 C6-E6-G6 (愉悦)
static const Note PET[]   = { {1047,120}, {1319,120}, {1568,180} };
// 撒娇示爱: 上扬 E5-G5-C6 (撒娇)
static const Note COQUET[]= { {659,120}, {784,120}, {1047,180} };
// 困倦入睡: 慢速下行 E5-C5-G4 (困意)
static const Note SLEEP[] = { {659,180}, {523,180}, {392,260} };
// 唤醒精神: 轻快上行 C5-D5-E5 (醒来)
static const Note WAKE[]  = { {523,100}, {587,100}, {659,160} };
// 收到消息/注意: 跳跃 C5-G5-C6
static const Note NOTICE[]= { {523,120}, {784,120}, {1047,180} };
// 粗暴抗拒: 低音警示 G3-D3 (保留, 稍大音量)
static const Note ABUSE[] = { {196,180}, {147,250} };

// 每场景的温柔音量(0-255)
static const uint8_t VOL_GENTLE = 25;   // 抚摸/撒娇等温柔交互
static const uint8_t VOL_NORMAL = 50;   // 主动问候/回复
static const uint8_t VOL_WARN   = 70;   // 粗暴警示

// 播放一个音符序列(阻塞式, 播放后静音)
// 返回播完后的延迟补偿毫秒(供调用方决定是否同步等待)
static void play(const Note* seq, int count, uint8_t vol) {
  M5Cardputer.Speaker.setVolume(vol);
  for (int i = 0; i < count; i++) {
    M5Cardputer.Speaker.tone(seq[i].freq, seq[i].dur);
    delay(seq[i].dur + 20);   // 音符间留 20ms 间隙
  }
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(0);   // 播完静音(消电流声)
}

// 便捷封装
static void greet()  { play(GREET,  3, VOL_NORMAL); }
static void reply()  { play(REPLY,  3, VOL_NORMAL); }
static void pet()    { play(PET,    3, VOL_GENTLE); }
static void coquet() { play(COQUET, 3, VOL_GENTLE); }
static void sleep()  { play(SLEEP,  3, VOL_GENTLE); }
static void wake()   { play(WAKE,   3, VOL_GENTLE); }
static void notice() { play(NOTICE, 3, VOL_NORMAL); }
static void abuse()  { play(ABUSE,  2, VOL_WARN); }

}  // namespace Tunes
