#pragma once
#include <M5Cardputer.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>

// 语音识别 STT (设备直连 Azure Speech, westus3)
// 录音用非阻塞状态机(每100ms读一块写LittleFS), 避免 M5.Mic 异步时序问题
// 流程: Opt键开始 → 录音(VAD) → 直连Azure STT → 返回文本
namespace STT {

constexpr uint32_t SAMPLE_RATE    = 16000;
constexpr int      CHUNK_SAMPLES  = 1600;   // 100ms @ 16kHz
constexpr uint32_t MAX_SAMPLES    = 16000 * 10;  // 10秒上限
constexpr uint32_t MIN_SAMPLES    = 8000;       // 0.5秒最短
constexpr const char* REC_PATH    = "/rec.pcm";

// 静音结束检测(RMS)
constexpr int SILENCE_RMS = 400;
constexpr int SILENCE_CHUNKS = 20;  // ~2秒静音结束

static int16_t g_chunk[CHUNK_SAMPLES];  // 3.2KB
static File g_rec;
static uint32_t g_samples = 0;
static int g_silent = 0;
static bool g_heard = false;
static bool g_recording = false;
static uint16_t g_lastRms = 0;

enum State { STT_IDLE, STT_LISTEN, STT_PROCESS };
static State g_state = STT_IDLE;

inline void stopAndRecognize();  // 前置声明(update里用到)

inline State& state() { return g_state; }
inline bool& recording() { return g_recording; }
inline uint16_t level() { return g_lastRms; }

// 计算 RMS(音量)
static uint16_t calcRms(const int16_t* buf, size_t n) {
  long sum = 0; for (size_t i = 0; i < n; i++) sum += (long)buf[i] * buf[i];
  return n ? (uint16_t)(sqrt((double)sum / n)) : 0;
}

// 开始录音
inline void start() {
  if (g_state != STT_IDLE) return;
  M5.Speaker.end();  // 释放共享 ES8311 总线给 Mic
  M5.Mic.begin();
  delay(50);
  if (!LittleFS.begin()) return;
  LittleFS.remove(REC_PATH);
  g_rec = LittleFS.open(REC_PATH, "w");
  if (!g_rec) return;
  g_samples = 0; g_silent = 0; g_heard = false;
  g_recording = true;
  g_state = STT_LISTEN;
}

// 驱动录音状态机(主循环每帧调用)
inline void update() {
  if (g_state != STT_LISTEN) return;
  static uint32_t lastChunk = 0;
  uint32_t now = millis();
  if (now - lastChunk < 100) return;
  lastChunk = now;

  size_t got = M5.Mic.record(g_chunk, CHUNK_SAMPLES, SAMPLE_RATE, false);
  if (got == 0) return;
  g_lastRms = calcRms(g_chunk, got);
  if (g_rec) g_rec.write((const uint8_t*)g_chunk, got * 2);
  g_samples += got;

  // VAD: 静音检测
  if (g_lastRms < SILENCE_RMS) g_silent++;
  else { g_silent = 0; g_heard = true; }

  if ((g_heard && g_samples >= MIN_SAMPLES && g_silent >= SILENCE_CHUNKS) ||
      g_samples >= MAX_SAMPLES) {
    stopAndRecognize();
  }
}

// 停止录音并识别
inline void stopAndRecognize() {
  if (g_state != STT_LISTEN) return;
  g_recording = false;
  if (g_rec) g_rec.close();
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  g_state = STT_PROCESS;
}

// 上传 PCM 到 Azure STT, 返回识别文本
inline std::string recognize() {
  if (g_samples < MIN_SAMPLES) return "";
  WiFiClientSecure c;
  c.setInsecure();
  if (!c.connect("westus3.stt.speech.microsoft.com", 443, 10000)) return "";
  // 构造 WAV 头(16kHz 16bit mono)
  uint8_t hdr[44];
  uint32_t n = g_samples * 2;
  memcpy(hdr, "RIFF", 4);
  hdr[4]=n+36&0xff; hdr[5]=(n+36>>8)&0xff; hdr[6]=(n+36>>16)&0xff; hdr[7]=(n+36>>24)&0xff;
  memcpy(hdr+8, "WAVE", 4); memcpy(hdr+12, "fmt ", 4);
  hdr[16]=16; hdr[17]=0; hdr[18]=0; hdr[19]=0;
  hdr[20]=1; hdr[21]=0; hdr[22]=1; hdr[23]=0;
  hdr[24]=0x00; hdr[25]=0x3e; hdr[26]=0; hdr[27]=0;   // 16000
  hdr[28]=0x00; hdr[29]=0x7d; hdr[30]=0; hdr[31]=0;   // 32000
  hdr[32]=2; hdr[33]=0; hdr[34]=16; hdr[35]=0;
  memcpy(hdr+36, "data", 4);
  hdr[40]=n&0xff; hdr[41]=(n>>8)&0xff; hdr[42]=(n>>16)&0xff; hdr[43]=(n>>24)&0xff;

  size_t total = 44 + n;
  c.print("POST /speech/recognition/conversation/cognitiveservices/v1?language=zh-CN HTTP/1.1\r\n");
  c.print("Host: westus3.stt.speech.microsoft.com\r\n");
  c.print("Ocp-Apim-Subscription-Key: YOUR_AZURE_KEY\r\n");
  c.print("Content-Type: audio/wav; codecs=audio/pcm; samplerate=16000\r\n");
  c.print("Accept: application/json\r\n");
  c.printf("Content-Length: %d\r\n\r\n", (int)total);
  c.write(hdr, 44);
  // 流式上传 PCM
  File rf = LittleFS.open(REC_PATH, "r");
  static uint8_t fbuf[1024];
  while (rf.available()) {
    int r = rf.read(fbuf, sizeof(fbuf));
    if (r <= 0) break;
    c.write(fbuf, r);
  }
  rf.close();
  // 读响应
  String resp;
  uint32_t t0 = millis();
  while (c.connected() && millis() - t0 < 10000) {
    while (c.available()) { char ch = c.read(); resp += ch; }
  }
  c.stop();
  LittleFS.remove(REC_PATH);
  // 解析 DisplayText
  JsonDocument doc;
  int di = resp.indexOf("{");
  if (di >= 0 && !deserializeJson(doc, (const char*)resp.c_str() + di)) {
    const char* t = doc["DisplayText"] | "";
    return std::string(t);
  }
  return "";
}

// 完成后回到空闲
inline void reset() { g_state = STT_IDLE; g_samples = 0; }

}  // namespace STT
