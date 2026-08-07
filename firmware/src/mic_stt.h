#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#include <string>
#include "config.h"
#include "net.h"
#include "dashscope.h"  // DS::volRef

// 方案Y改造: STT 走后端代理(本地 Whisper), 不再流式直连 DashScope。
// 流程: 按住 Opt 录音(存 LittleFS /rec.pcm) → 松开 → 整段 POST 后端 /stt → 返回文本。
// ⚠️ 重要: 绝不能在录音/上传过程中调用 LittleFS.end()! 因为固件的精灵(sprites)
//    就是从 LittleFS 读取的, 卸载文件系统会导致精灵无法渲染(没画面/没声音)。
namespace STT {
static const uint32_t SR = 16000;

inline void switchToMic() {
  M5.Speaker.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);
  auto mc = M5.Mic.config(); mc.sample_rate = SR; mc.magnification = 48; M5.Mic.config(mc);
  M5.Mic.begin(); delay(60);
}
inline void switchToSpeaker() {
  M5.Mic.end(); delay(40);
  gpio_reset_pin(GPIO_NUM_43); gpio_reset_pin(GPIO_NUM_46); delay(10);
  M5.Speaker.begin(); M5.Speaker.setVolume(DS::volRef()); delay(20);
}

typedef void (*StatusCb)(const char*);
static const char* g_lastErr = "";

// ===== 点击切换录音 (参考 claude-pocket tap-to-toggle + 主循环驱动) =====
// 按一次 Opt: 开始录音(开麦克风); 再按一次 Opt: 停止并处理(上传STT)。
// 录音由主循环每帧调 updateRec() 拉数据, 非阻塞, 不占 core。
enum RecState { REC_IDLE = 0, REC_RECORDING = 1 };
static RecState gRecState = REC_IDLE;
static bool gRecOpen = false;       // 麦克风是否已开
static uint32_t gRecStartMs = 0;    // 录音起始时间(最大时长保护)
static int16_t gRecBuf[2][1600];    // 双缓冲, 各100ms @16k
static int gRecIdx = 0;
static uint32_t gRecBytes = 0;      // 已录字节数
static bool gRecSpoke = false;

inline void recOpen() {
  switchToMic();
  // 清空旧的录音文件(用 wb 打开一次覆盖)
  File clr = LittleFS.open("/rec.pcm", "wb"); if (clr) clr.close();
  M5.Mic.record(gRecBuf[0], 1600, SR);
  M5.Mic.record(gRecBuf[1], 1600, SR);
  gRecIdx = 0; gRecBytes = 0; gRecSpoke = false;
  gRecOpen = true; gRecStartMs = millis();
  gRecState = REC_RECORDING;
}
// 主循环每帧调用: 拉一块录音数据写入 /rec.pcm; 返回已录字节数
inline uint32_t recUpdate() {
  if (gRecState != REC_RECORDING || !gRecOpen) return 0;
  if (millis() - gRecStartMs > 55000) { return 0xFFFFFFFF; }  // 超时信号
  while (M5.Mic.isRecording() == 2) { delay(1); }
  File f = LittleFS.open("/rec.pcm", "ab");  // 追加写
  if (f) {
    int32_t sum = 0;
    for (int i = 0; i < 1600; i++) sum += abs(gRecBuf[gRecIdx][i]) / 1600;
    if (sum > 200) gRecSpoke = true;
    f.write((uint8_t*)gRecBuf[gRecIdx], 1600 * 2);
    gRecBytes += 1600 * 2;
    f.close();
  }
  M5.Mic.record(gRecBuf[gRecIdx], 1600, SR);
  gRecIdx ^= 1;
  return gRecBytes;
}
// 停止录音, 切回扬声器; 返回录音字节数
inline uint32_t recClose() {
  if (!gRecOpen) return 0;
  switchToSpeaker();
  gRecState = REC_IDLE; gRecOpen = false;
  uint32_t bytes = gRecBytes;
  Serial.printf("[REC] closed, bytes=%u, spoke=%d\n", bytes, gRecSpoke ? 1 : 0);
  gRecBytes = 0;
  return bytes;
}
inline bool recActive() { return gRecState == REC_RECORDING; }

// 上传 LittleFS 里的 /rec.pcm 到后端 /stt, 返回识别文本
// ⚠️ 后端 Whisper 需要标准 WAV(RIFF/WAVE) 格式, 先构造 44 字节 WAV 头再发 PCM。
inline std::string uploadToStt(uint32_t pcmBytes) {
  if (WiFi.status() != WL_CONNECTED) return "";

  // 用 WiFiClient 直接流式发送(分块读文件写socket, 避免 malloc 大块→OOM)
  std::string host = backendHost();
  WiFiClient client;
  if (!client.connect(backendIP(), backendPort())) return "";

  File f = LittleFS.open("/rec.pcm", "r");
  if (!f) { client.stop(); return ""; }
  size_t fsize = f.size();

  // 构造 44 字节 WAV 头 (16-bit mono PCM @16000), 参考 claude-pocket make_wav_header
  uint8_t wav[44];
  uint32_t data_bytes = (uint32_t)fsize;
  uint32_t sample_count = data_bytes / 2;
  memcpy(wav, "RIFF", 4);
  wav[4]=0x24; wav[5]=0x00; wav[6]=0x00; wav[7]=0x00;            // 36+data low
  uint32_t riff = 36 + data_bytes;
  memcpy(wav+4, &riff, 4);                                       // RIFF size
  memcpy(wav+8, "WAVE", 4);
  memcpy(wav+12, "fmt ", 4);
  uint32_t fmt16 = 16; uint16_t pcm1=1, ch1=1, blk=2, bits16=16;
  uint32_t sr=16000, br=32000;
  memcpy(wav+16, &fmt16, 4);
  memcpy(wav+20, &pcm1, 2); memcpy(wav+22, &ch1, 2);
  memcpy(wav+24, &sr, 4); memcpy(wav+28, &br, 4);
  memcpy(wav+32, &blk, 2); memcpy(wav+34, &bits16, 2);
  memcpy(wav+36, "data", 4);
  memcpy(wav+40, &data_bytes, 4);
  (void)sample_count;

  // 发送 HTTP 头
  client.print("POST "); client.print((backendPath() + "/stt").c_str()); client.println(" HTTP/1.1");
  client.print("Host: "); client.println(host.c_str());
  client.println("Content-Type: audio/wav");
  client.print("Content-Length: "); client.println(44 + fsize);
  if (std::strlen(PET_TOKEN) > 0) { client.print("x-pet-token: "); client.println(PET_TOKEN); }
  client.println("Connection: close");
  client.println();

  // 先发 44 字节 WAV 头
  client.write(wav, 44);

  // 分块读文件发送(每块1KB, 零大块内存)
  static uint8_t chunk[1024];
  client.setTimeout(60000);
  while (f.available()) {
    int n = f.read(chunk, sizeof(chunk));
    if (n > 0) client.write(chunk, n);
    else break;
  }
  f.close();

  // 读响应: 先解析响应头拿 Content-Length, 再读完整 body(等够字节)
  uint32_t t0 = millis();
  String head;
  int contentLen = -1;
  while (client.connected() && millis() - t0 < 15000) {
    if (client.available()) {
      char c = client.read();
      head += c;
      // 解析 Content-Length
      int cli = head.indexOf("Content-Length:");
      if (contentLen < 0 && cli >= 0) {
        contentLen = head.substring(cli + 16).toInt();
        // 若头里含 \r\n 边界截断
        int nl = head.indexOf('\r', cli + 16);
        if (nl > 0) contentLen = head.substring(cli + 16, nl).toInt();
      }
      if (head.endsWith("\r\n\r\n")) break;  // 头结束
    } else delay(1);
  }
  // 读 body: 若已知 Content-Length 就等够, 否则读到连接关闭
  String body;
  if (contentLen > 0) {
    while ((int)body.length() < contentLen && client.connected() && millis() - t0 < 20000) {
      if (client.available()) body += (char)client.read();
      else delay(1);
    }
  } else {
    while (client.connected() && millis() - t0 < 20000) {
      if (client.available()) body += (char)client.read();
      else delay(2);
    }
  }
  client.stop();

  // 解析 JSON
  if (body.isEmpty()) return "";
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  const char* t = doc["text"] | "";
  return std::string(t).empty() ? "" : t;
}

}  // namespace STT
