#include <M5Cardputer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "esp_partition.h"
#include "esp_system.h"
#include <vector>
#include <string>
#include "config.h"
#include "net.h"
#include "sprite_player.h"
#include "pet_state.h"
#include "scenes.h"
#include "weather.h"
#include "dashscope.h"
#include "mic_stt.h"

// TLS + WebSocket 握手很吃栈;加大 loop 任务栈
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

static M5Canvas canvas(&M5Cardputer.Display);
static SpritePlayer player;

static std::string input, reply = "我在,主人。", petName = "小豆丁", emotion = "neutral";
static std::vector<std::string> replyLines;
static int scrollTop = 0;
static bool voiceOn = true, lastTtsFail = false;
static uint32_t kbdIgnoreUntil = 0;

// 思考/说话放到后台核(core 0),主循环(core 1)永不阻塞 → 背景动画一直跑
enum { PH_IDLE = 0, PH_THINKING = 1, PH_SPEAKING = 2 };
static volatile int gPhase = PH_IDLE;
static SemaphoreHandle_t gMtx = nullptr;
static std::string gJobMsg;                              // 主→任务:待处理的话(gMtx 保护)
static volatile bool gJobReady = false;
static std::string gResReply, gResEmotion, gResName;     // 任务→主:结果(gMtx 保护)
static volatile uint32_t gResSeq = 0;
static uint32_t gLastSeq = 0;
static volatile bool gTtsFailFlag = false;

// 录音中状态(底部角标用,不再全屏黑)
static volatile bool gRecording = false;
static uint32_t gRecStart = 0;
static const char* gPttStatus = "";

// 场景系统:10 场景混搭风,室内按作息自动切 + Fn+[/]/\ 手动
static int gSceneIdx = 0;
static bool gAutoScene = true;
static volatile int gIntimacy = -1;     // 亲密度(后台核拉)
static uint32_t gWxNext = 0;            // 下次拉天气/亲密度的时间
static bool gUseSD = false;             // 精灵是否从 SD 读(launcher 化前置)

static std::string transientAction;
static uint32_t transientUntil = 0;

static float roamX = 120, targetX = 120;
static int facing = 1, roamMode = 1;
static uint32_t roamUntil = 0;

static const int GROUND = 90, BAR_TOP = 93, BAR_Y = 96, LH = 13, VIS = 3, BARW = 232;

static int curHour() { struct tm t; if (!getLocalTime(&t, 0)) return -1; return t.tm_hour; }

static std::vector<std::string> wrapLines(const std::string& s, int maxW) {
  std::vector<std::string> out; std::string line; int w = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char ch = s[i]; int len = 1;
    if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3; else if (ch >= 0xC0) len = 2;
    std::string g = s.substr(i, len); i += len;
    if (g == "\n") { out.push_back(line); line.clear(); w = 0; continue; }
    int gw = canvas.textWidth(g.c_str());
    if (w + gw > maxW && !line.empty()) { out.push_back(line); line.clear(); w = 0; }
    line += g; w += gw;
  }
  if (!line.empty()) out.push_back(line);
  return out;
}

static void setReply(const std::string& t) {
  reply = t; canvas.setFont(&fonts::efontCN_12);
  replyLines = wrapLines(petName + "：" + t, BARW); scrollTop = 0;
}
static void setTransient(const char* a, uint32_t ms) { transientAction = a; transientUntil = millis() + ms; }

static void render() {
  Scenes::draw(canvas, millis(), gSceneIdx, (int)roamX);   // 天空+场景+网格+落地光圈
  Scenes::drawPanels(canvas, gSceneIdx, gIntimacy);        // HUD:时钟/日期/天气/亲密度(全真实)
  player.draw(canvas, (int)roamX - SPR_W / 2, GROUND - SPR_H, facing);  // 角色在最前

  canvas.setFont(&fonts::efontCN_12);
  // 右上角喇叭音量图标:声波数=音量(小/中/大);静音显示 X
  { uint16_t col = voiceOn ? (lastTtsFail ? 0xFD20 : 0x07E0) : 0x7BEF;
    canvas.fillRect(176, 4, 3, 4, col);
    canvas.fillTriangle(179, 2, 179, 10, 184, 6, col);
    if (voiceOn) { int v = DS::volRef(), lv = v < 120 ? 1 : (v < 200 ? 2 : 3);
      for (int w = 0; w < lv; w++) canvas.drawLine(186 + w * 2, 4 - w, 186 + w * 2, 8 + w, col); }
    else { canvas.drawLine(186, 3, 192, 9, 0xF800); canvas.drawLine(192, 3, 186, 9, 0xF800); } }

  // 右上角 WiFi 信号(4 格随强度);未连显示红叉
  { const int wx = 222, wy = 10; bool up = WiFi.status() == WL_CONNECTED; long rs = up ? WiFi.RSSI() : 0;
    int bars = !up ? 0 : (rs >= -55 ? 4 : rs >= -65 ? 3 : rs >= -73 ? 2 : 1);
    uint16_t ac = !up ? 0x7BEF : (bars >= 3 ? 0x07E0 : bars == 2 ? 0xFFE0 : 0xFD20);
    for (int i = 0; i < 4; i++) { int bh = 2 + i * 2;
      canvas.fillRect(wx + i * 3, wy - bh, 2, bh, (up && i < bars) ? ac : 0x39C7); }
    if (!up) { canvas.drawLine(wx, 2, wx + 10, 10, 0xF800); canvas.drawLine(wx + 10, 2, wx, 10, 0xF800); } }

  canvas.fillRect(0, BAR_TOP, 240, 135 - BAR_TOP, 0x0000);
  canvas.drawFastHLine(0, BAR_TOP - 1, 240, 0x18C3);
  if (gRecording) {  // 录音中:底部红色提示 + 闪烁录音点(画面上半照常动)
    canvas.setFont(&fonts::efontCN_12);
    if ((millis() / 400) % 2) canvas.fillCircle(10, BAR_Y + 3, 4, 0xF800);
    else canvas.drawCircle(10, BAR_Y + 3, 4, 0xF800);
    canvas.setTextColor(0xF800, 0x0000); canvas.setCursor(22, BAR_Y);
    char b[48]; snprintf(b, sizeof(b), "正在听  %s  %lus", gPttStatus, (unsigned long)((millis() - gRecStart) / 1000));
    canvas.print(b);
    canvas.setTextColor(0x7BEF, 0x0000); canvas.setCursor(22, BAR_Y + LH);
    canvas.print("松开 Opt 结束");
  } else if (!input.empty()) {
    canvas.setTextColor(0x07FF, 0x0000); canvas.setCursor(4, BAR_Y);
    canvas.print(("> " + input + "_").c_str());
  } else {
    for (int i = 0; i < VIS; i++) {
      int li = scrollTop + i;
      if (li < 0 || li >= (int)replyLines.size()) break;
      canvas.setTextColor(0xCE7C, 0x0000); canvas.setCursor(4, BAR_Y + i * LH);
      canvas.print(replyLines[li].c_str());
      if (li == 0) { canvas.setTextColor(0x3FE6, 0x0000); canvas.setCursor(4, BAR_Y); canvas.print((petName + "：").c_str()); }
    }
    if (scrollTop > 0) canvas.fillTriangle(232, BAR_Y + 2, 236, BAR_Y + 2, 234, BAR_Y - 2, 0x7BCF);
    if (scrollTop + VIS < (int)replyLines.size())
      canvas.fillTriangle(232, BAR_Y + 2 * LH - 2, 236, BAR_Y + 2 * LH - 2, 234, BAR_Y + 2 * LH + 2, 0x7BCF);
  }
  canvas.pushSprite(0, 0);
}

static void brainTask(void*);  // 定义在下方

// 首次开机把精灵图从内置 Flash(LittleFS)拷到 SD 卡(为 launcher 化做准备;用户免手动拷)
static void migrateSpritesToSD() {
  if (!SD.exists("/sprites")) SD.mkdir("/sprites");
  static uint8_t buf[1024];
  for (int i = 0; i < ACTION_COUNT; i++) {
    String p = String("/sprites/") + ACTIONS[i].name + ".bin";
    if (SD.exists(p)) continue;
    File src = LittleFS.open(p, "r"); if (!src) continue;
    File dst = SD.open(p, "w"); if (!dst) { src.close(); continue; }
    while (src.available()) { int n = src.read(buf, sizeof(buf)); if (n > 0) dst.write(buf, n); else break; }
    dst.close(); src.close();
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  canvas.setColorDepth(16); canvas.createSprite(240, 135);
  M5.Speaker.begin(); M5.Speaker.setVolume(DS::volRef());
  LittleFS.begin(false);  // 别 format-on-fail(launcher 模式下无 littlefs 分区,精灵已在 SD)
  // SD 卡:精灵图改放 SD(launcher 化前置);首次开机自动从内置 Flash 迁移,用户免手动拷
  SPI.begin(40, 39, 14, 12);
  bool sdOk = SD.begin(12, SPI, 25000000);
  if (sdOk) migrateSpritesToSD();
  bool useSD = sdOk && SD.exists("/sprites/idle.bin");
  gUseSD = useSD;
  player.begin(useSD ? (fs::FS&)SD : (fs::FS&)LittleFS); player.setAction("idle");
  gMtx = xSemaphoreCreateMutex();
  // 后台核跑思考+朗读,24KB 栈(HTTPS/TLS 很吃栈),钉在 core 0(主循环在 core 1)
  xTaskCreatePinnedToCore(brainTask, "brain", 24 * 1024, nullptr, 1, nullptr, 0);
  setReply("连接 WiFi 中…"); render();

  if (wifiConnect()) {
    configTzTime("CST-8", "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");
    delay(300); gSceneIdx = Scenes::autoIdx(curHour());  // 按作息选初始场景
    setReply(std::string("我在,主人。") + (gUseSD ? "〔精灵已上 SD〕" : "〔SD 未就绪,走内置〕") + "打字或按住 Opt 说话;Fn+[ ] 切场景;Fn+V 语音。");
    setTransient("waving", 2500);
  } else {
    setReply("连不上 WiFi…去 config.h 检查。");
    setTransient("sad", 0xFFFFFFFF);
  }
}

// 把一句话交给后台核去思考+朗读;立即返回,主循环继续跑动画。打字和语音共用
static void submitJob(const std::string& msg) {
  if (msg.empty()) return;
  xSemaphoreTake(gMtx, portMAX_DELAY);
  gJobMsg = msg; gJobReady = true;
  xSemaphoreGive(gMtx);
  setReply("(在想…)");
  input.clear();
}

// 后台核(core 0):askPet(DeepSeek) → 出结果 → DS::speak(TTS)。全程不碰屏幕/键盘/精灵
static void brainTask(void*) {
  for (;;) {
    if (gJobReady) {
      std::string msg;
      xSemaphoreTake(gMtx, portMAX_DELAY); msg = gJobMsg; gJobReady = false; xSemaphoreGive(gMtx);

      gPhase = PH_THINKING;
      PetReply r = askPet(msg);
      if (r.intimacy >= 0) gIntimacy = r.intimacy;
      xSemaphoreTake(gMtx, portMAX_DELAY);
      gResReply = r.reply; gResEmotion = r.emotion; gResName = r.name; gResSeq++;
      xSemaphoreGive(gMtx);

      gPhase = PH_SPEAKING;
      if (voiceOn) {
        std::string er = DS::speak(r.reply);
        gTtsFailFlag = !er.empty();
        if (!er.empty()) {
          xSemaphoreTake(gMtx, portMAX_DELAY);
          gResReply = r.reply + "  〔TTS:" + er + "〕"; gResSeq++;
          xSemaphoreGive(gMtx);
        }
      } else gTtsFailFlag = false;

      kbdIgnoreUntil = millis() + 300;  // 说完冷却,防喇叭噪声触发假按键
      gPhase = PH_IDLE;
    }
    // WiFi 断了就在后台尝试重连(每 15s)
    static uint32_t reconNext = 0;
    if (WiFi.status() != WL_CONNECTED && millis() > reconNext) { WiFi.reconnect(); reconNext = millis() + 15000; }
    // 空闲时拉天气 + 亲密度(开机一次 + 每 30 分钟);先 IP 定位。TLS 在后台核,不卡主循环
    if (gPhase == PH_IDLE && WiFi.status() == WL_CONNECTED && millis() > gWxNext) {
      if (!WX::located()) WX::geolocate();
      WX::fetch();
      int iv = fetchIntimacy(); if (iv >= 0) gIntimacy = iv;
      gWxNext = millis() + 1800000UL;
    }
    delay(15);
  }
}

// 录音期间的每帧回调:刷新动画 + 底部录音角标(画面不再全屏黑)
static void pttTick(const char* status) {
  gPttStatus = status;
  player.update(millis());
  render();
}

// 按住 Opt 说话:边录边把 PCM 流式发给 DashScope 实时识别(WSS),最长 ~55s
static void doPTT() {
  gRecording = true; gRecStart = millis(); gPttStatus = "连接中…";
  player.setAction("thinking");            // 倾听姿态(录音全程在动)
  render();
  std::string heard = STT::streamListen(pttTick);
  gRecording = false;

  if (!heard.empty()) submitJob(heard);
  else { setReply(std::string("(没听清:") + STT::g_lastErr + ")"); lastTtsFail = false; }
  input.clear();
  kbdIgnoreUntil = millis() + 400;
}

// 退出回 launcher = bmorcelli 自己删 app 时的官方动作:整块抹掉 otadata。
// otadata 失效 → 引导程序回落到 factory 分区(=launcher);bootToApp=false 时稳停菜单。
// 无 otadata 分区 = 整机直刷模式(无 OTA)→ 没有 launcher 可回,返回 false 不重启。
static bool bootBackToLauncher() {
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) return false;
  esp_partition_erase_range(otadata, 0, otadata->size);  // 整块 0x2000,和 launcher 的 ClearOtaBoot 一致
  esp_restart();
  return false;
}

static void handleKeyboard() {
  if (millis() < kbdIgnoreUntil) return;
  if (gPhase != PH_IDLE) return;  // 思考/说话中不收键(也防喇叭噪声触发假按键)
  if (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) return;
  auto st = M5Cardputer.Keyboard.keysState();
  if (st.opt) { doPTT(); return; }  // 按住 Opt 说话
  if (st.fn) {
    for (char c : st.word) {
      if (c == ',') { if (scrollTop > 0) scrollTop--; return; }
      if (c == '.') { if (scrollTop + VIS < (int)replyLines.size()) scrollTop++; return; }
      if (c == 'v' || c == 'V') { voiceOn = !voiceOn; return; }
      if (c == '/') { int v = DS::volRef(); v = (v < 120) ? 160 : (v < 200 ? 230 : 90);
                      DS::volRef() = v; M5.Speaker.setVolume(v); return; }
      if (c == '[') { gAutoScene = false; gSceneIdx = (gSceneIdx + Scenes::count() - 1) % Scenes::count();
                      setReply(std::string("场景：") + Scenes::name(gSceneIdx)); return; }
      if (c == ']') { gAutoScene = false; gSceneIdx = (gSceneIdx + 1) % Scenes::count();
                      setReply(std::string("场景：") + Scenes::name(gSceneIdx)); return; }
      if (c == '\\') { gAutoScene = true; gSceneIdx = Scenes::autoIdx(curHour());
                       setReply("场景：跟随作息自动切"); return; }
      if (c == 'q' || c == 'Q') {  // 退出回 launcher(二次确认,防手滑重启)
        static uint32_t armUntil = 0;
        if (millis() < armUntil) { setReply("退出中…回到 launcher"); render();
          bootBackToLauncher();  // 成功则重启不返回;失败=整机模式:
          setReply("当前是整机直刷模式,没有 launcher 可回(装了 launcher 后此键才有效)。"); }
        else { armUntil = millis() + 3000; setReply("再按一次 Fn+Q 退出回 launcher。"); }
        return;
      }
      const char* a = hotkeyAction(c); if (a) { setTransient(a, 4000); return; }
    }
    return;
  }
  for (char c : st.word) input += c;
  if (st.del && !input.empty()) input.pop_back();
  if (st.enter && !input.empty()) submitJob(input);
}

static void roamStep(uint32_t now, int hour) {
  if (roamMode == 0) {
    int dir = (targetX > roamX) ? 1 : -1; facing = dir;
    bool run = fabsf(targetX - roamX) > 70;
    player.setAction(run ? "running" : "walking");
    roamX += dir * (run ? 1.3f : 0.7f);
    if (fabsf(targetX - roamX) < 2.5f) {
      roamMode = 1; player.setAction(pickActivity(hour)); roamUntil = now + 2500 + random(4500);
    }
  } else {
    if (now > roamUntil) {
      if (hour < 7 || hour >= 23) { player.setAction("sleeping"); roamUntil = now + 6000; return; }
      roamMode = 0; targetX = 44 + random(152); if (random(10) < 3) targetX = roamX;
    }
  }
}

void loop() {
  M5Cardputer.update();
  handleKeyboard();
  uint32_t now = millis();

  if (gAutoScene) { int want = Scenes::autoIdx(curHour()); if (want != gSceneIdx) gSceneIdx = want; }  // 室内按作息自动切

  // 后台核出了结果 → 主循环安全地刷新文字 + 设情绪
  if (gResSeq != gLastSeq) {
    std::string rp, em, nm;
    xSemaphoreTake(gMtx, portMAX_DELAY);
    rp = gResReply; em = gResEmotion; nm = gResName; gLastSeq = gResSeq;
    xSemaphoreGive(gMtx);
    emotion = em; if (!nm.empty()) petName = nm;
    setReply(rp);
    lastTtsFail = gTtsFailFlag;
    setTransient(emotionToAction(emotion), 6000);  // 说完后情绪再停留一会儿
  }

  if (gPhase == PH_THINKING) {
    player.setAction("thinking");                  // 思考中:沉思动画(背景照常动)
  } else if (gPhase == PH_SPEAKING) {
    const char* a = emotionToAction(emotion);      // 说话中:按这句话的情绪做反应
    player.setAction(a);
    const ActionMeta* m = SpritePlayer::find(a);
    if (m && !m->loop && player.finished()) player.setAction("idle");  // 非循环表情放完→回 idle,保持在动
  } else if (!transientAction.empty() && now < transientUntil) {
    player.setAction(transientAction.c_str());
    const ActionMeta* m = SpritePlayer::find(transientAction.c_str());
    if (m && !m->loop && player.finished()) transientUntil = 0;
  } else {
    transientAction.clear();
    if (input.empty()) roamStep(now, curHour()); else player.setAction("idle");
  }

  player.update(now);
  render();
  delay(5);
}
