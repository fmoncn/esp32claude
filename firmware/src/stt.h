#pragma once
#include <M5Cardputer.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>

// 语音识别 STT — 设备直连 Azure Speech (westus3)
// 方案参考 M5Claw(已验证能正常录音+识别): 
//   1. ES8311 codec 软复位(I2C 写寄存器0x18) 消除 mic/speaker 切换电流声
//   2. Mic.config() 设增益 magnification=64 + 降噪 noise_filter=64
//   3. 录音等 isRecording() 完成才写满块(1600 samples) + 重新 record
// 触发: Opt 键
namespace STT {

constexpr uint32_t SAMPLE_RATE   = 16000;
constexpr int      CHUNK_SAMPLES = 1600;   // 100ms @ 16kHz
constexpr uint32_t RECORD_MS     = 5000;   // 固定录音 5 秒
constexpr uint32_t MIN_SAMPLES   = SAMPLE_RATE / 2;  // 0.5秒最短
constexpr const char* REC_PATH   = "/rec.pcm";

static int16_t g_chunk[CHUNK_SAMPLES];  // 3.2KB
static File g_rec;
static uint32_t g_samples = 0;
static uint32_t g_recStartMs = 0;
static bool g_recording = false;
static bool g_firstRec = false;

enum State { STT_IDLE, STT_LISTEN, STT_PROCESS };
static State g_state = STT_IDLE;

inline void stopAndRecognize();  // 前置声明

inline State& state() { return g_state; }

// ES8311 codec 简单切换 mic (先用最简单方式验证固定5秒录音, 避免 Wire 复位死机)
static void es8311Switch(bool wantMic) {
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.end();
  M5Cardputer.Mic.end();
  delay(20);
  if (wantMic) {
    auto cfg = M5Cardputer.Mic.config();
    cfg.sample_rate = SAMPLE_RATE;
    cfg.magnification = 64;         // 增益放大(解决信号弱)
    cfg.noise_filter_level = 64;    // 降噪(解决噪声/电流声)
    cfg.task_priority = 1;
    M5Cardputer.Mic.config(cfg);
    M5Cardputer.Mic.begin();   // 配好后 begin
  } else {
    M5Cardputer.Speaker.begin();
  }
}

// 开始录音 — 按Opt触发, 固定录5秒后自动STT
inline void start() {
  if (g_state != STT_IDLE) return;
  es8311Switch(true);   // 复位 codec + 配 Mic + begin
  if (!LittleFS.begin()) return;
  LittleFS.remove(REC_PATH);
  g_rec = LittleFS.open(REC_PATH, "w");
  if (!g_rec) { M5Cardputer.Mic.end(); return; }
  g_samples = 0;
  g_recording = true;
  g_firstRec = false;
  g_recStartMs = millis();  // 记录开始时间, 固定录5秒
  g_state = STT_LISTEN;
}

// 驱动录音状态机(主循环每帧调用) — 固定录 RECORD_MS 毫秒后自动停止
inline void update() {
  if (g_state != STT_LISTEN) return;
  static uint32_t lastChunk = 0;
  uint32_t now = millis();

  // 首次 record 初始化 DMA
  if (!g_firstRec) {
    memset(g_chunk, 0, sizeof(g_chunk));
    M5Cardputer.Mic.record(g_chunk, CHUNK_SAMPLES, SAMPLE_RATE, false);
    g_firstRec = true;
    lastChunk = now;
    return;
  }

  // 固定录音时长到达 → 自动停止并识别
  if (now - g_recStartMs >= RECORD_MS) {
    stopAndRecognize();
    return;
  }

  // 每 ~95ms 检查; 等 isRecording() 完成(DMA填满)才写 + 重新 record
  if (now - lastChunk >= 95 && !M5Cardputer.Mic.isRecording()) {
    // 写满的一块(1600 samples × 2字节)
    size_t bytes = CHUNK_SAMPLES * 2;
    if (g_rec) g_rec.write((const uint8_t*)g_chunk, bytes);
    g_samples += CHUNK_SAMPLES;
    memset(g_chunk, 0, sizeof(g_chunk));
    M5Cardputer.Mic.record(g_chunk, CHUNK_SAMPLES, SAMPLE_RATE, false);
    lastChunk = now;
  }
}

// 停止录音
inline void stopAndRecognize() {
  if (g_state != STT_LISTEN) return;
  g_recording = false;
  // 写最后一帧(若有)
  if (g_firstRec && !M5Cardputer.Mic.isRecording()) {
    size_t bytes = CHUNK_SAMPLES * 2;
    if (g_rec) g_rec.write((const uint8_t*)g_chunk, bytes);
    g_samples += CHUNK_SAMPLES;
  }
  if (g_rec) g_rec.close();
  M5Cardputer.Mic.end();
  g_state = STT_PROCESS;
}

// 构造 WAV 头(16kHz 16bit mono, 44字节)
static void makeWavHeader(uint8_t* hdr, uint32_t pcmBytes) {
  memset(hdr, 0, 44);
  memcpy(hdr, "RIFF", 4);
  uint32_t riff = 36 + pcmBytes;
  memcpy(hdr+4, &riff, 4);
  memcpy(hdr+8, "WAVE", 4);
  memcpy(hdr+12, "fmt ", 4);
  uint32_t fmt = 16; memcpy(hdr+16, &fmt, 4);
  uint16_t pcm=1, mono=1, bits=16;
  memcpy(hdr+20, &pcm, 2); memcpy(hdr+22, &mono, 2);
  memcpy(hdr+24, &SAMPLE_RATE, 4);
  uint32_t br = SAMPLE_RATE*2; memcpy(hdr+28, &br, 4);
  uint16_t align=2; memcpy(hdr+32, &align, 2);
  memcpy(hdr+34, &bits, 2);
  memcpy(hdr+36, "data", 4);
  memcpy(hdr+40, &pcmBytes, 4);
}

// 上传 PCM 到 Azure STT, 返回识别文本
inline std::string recognize() {
  if (g_samples < MIN_SAMPLES) return "";
  WiFiClientSecure c;
  c.setInsecure();
  if (!c.connect("westus3.stt.speech.microsoft.com", 443, 15000)) return "";
  uint32_t pcmBytes = g_samples * 2;
  uint8_t hdr[44]; makeWavHeader(hdr, pcmBytes);
  size_t total = 44 + pcmBytes;
  c.printf("POST /speech/recognition/conversation/cognitiveservices/v1?language=zh-CN HTTP/1.1\r\n");
  c.printf("Host: westus3.stt.speech.microsoft.com\r\n");
  c.printf("Ocp-Apim-Subscription-Key: YOUR_AZURE_KEY\r\n");
  c.println("Content-Type: audio/wav");
  c.println("Accept: application/json");
  c.println("Connection: close");
  c.printf("Content-Length: %d\r\n\r\n", (int)total);
  c.write(hdr, 44);
  File rf = LittleFS.open(REC_PATH, "r");
  static uint8_t fbuf[2048];
  while (rf.available()) {
    int r = rf.read(fbuf, sizeof(fbuf));
    if (r <= 0) break;
    c.write(fbuf, r);
  }
  rf.close();
  // 读响应
  String resp;
  uint32_t t0 = millis();
  while (c.connected() && millis() - t0 < 12000) {
    while (c.available()) { char ch = c.read(); resp += ch; }
    if (resp.indexOf("\r\n\r\n") >= 0 && resp.indexOf("DisplayText") >= 0) break;
  }
  c.stop();
  LittleFS.remove(REC_PATH);
  // 解析 DisplayText
  int di = resp.indexOf("DisplayText");
  if (di >= 0) {
    int s = resp.indexOf("\"", di+12);
    int e = resp.indexOf("\"", s+1);
    if (s >= 0 && e > s) {
      std::string t = resp.substring(s+1, e).c_str();
      return t;
    }
  }
  return "";
}

// 完成后回到空闲
inline void reset() { g_state = STT_IDLE; g_samples = 0; }

}  // namespace STT
