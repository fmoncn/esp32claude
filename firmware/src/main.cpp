#include <M5Cardputer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SPI.h>
#include <time.h>
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include <vector>
#include <string>
#include "config.h"
#include "net.h"
#include "sprite_player.h"
#include "pet_state.h"
#include "pinyin_ime.h"
#include "scenes.h"
#include "weather.h"
#include "dell_hub.h"
#include "stt.h"
#include "pet_touch.h"

// TLS + WebSocket 握手很吃栈;加大 loop 任务栈
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

static M5Canvas canvas(&M5Cardputer.Display);
static SpritePlayer player;

static std::string input, reply = "请输入文本...", petName = "克劳德", emotion = "neutral";
static std::vector<std::string> replyLines;
static int scrollTop = 0;
static uint8_t gBrightness = 50;          // 屏幕亮度 0-255(默认 50);-= 键调
static uint32_t gIdleSince = 0;           // 最后操作时间(省电调暗计时)
static bool gShutdownWarned = false;      // 已显示调暗提示
static bool gDimmed = false;              // 已进入待机调暗(亮度0,等待按键恢复)
static bool gAutoScroll = false;          // 自动滚动中(长回复)
static uint32_t gAutoScrollNext = 0;      // 下次推进时间
static int gInputMode = 1;                // 1=中文拼音 0=英文(Shift 切换)
static bool gTranslate = false;           // true=翻译模式(Alt 切换) false=聊天模式
static int gWifiRssi = -40;               // WiFi 信号缓存(启动+定期更新,避免render频繁读异常)
static int gLastCardScene = -1;           // 上次拉取卡片的场景(变化时重新拉)
static uint32_t gCardNext = 0;            // 卡片下次刷新时间(每60s)
static int gAutoScrollTarget = 0;         // 目标滚动位置(底部)
static uint32_t kbdIgnoreUntil = 0;
// 主动对话: 克劳德主动找主人说话(轮询后端 /proactive + 三连音提醒)
static uint32_t gProNext = 0;             // 下次主动轮询时间(每60s)
static char gProMsg[80] = {0};            // 主动消息缓冲(省内存:固定80字符)
static uint32_t gProRingUntil = 0;        // 三连音播报冷却(防反复)

// 思考/说话放到后台核(core 0),主循环(core 1)永不阻塞 → 背景动画一直跑
enum { PH_IDLE = 0, PH_THINKING = 1 };
static volatile int gPhase = PH_IDLE;
static SemaphoreHandle_t gMtx = nullptr;
static std::string gJobMsg;                              // 主→任务:待处理的话(gMtx 保护)
static volatile bool gJobReady = false;
static std::string gResReply, gResEmotion, gResName;     // 任务→主:结果(gMtx 保护)
static volatile uint32_t gResSeq = 0;
static uint32_t gLastSeq = 0;

// 录音中状态(底部角标用,不再全屏黑)

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

static const int GROUND = 90, BAR_TOP = 85, BAR_Y = 88, LH = 14, VIS = 3, BARW = 232;  // 14px字,回复3行,对话框略高

static int curHour() { struct tm t; if (!getLocalTime(&t, 0)) return -1; return t.tm_hour; }

static std::vector<std::string> wrapLines(const std::string& s, int maxW) {
  // 半单位制: 中文/全角=2单位, ASCII=1单位(英文约半宽)。每行 maxW/14≈16中文宽=32半单位。
  // 用单位计数, 不依赖 canvas.textWidth(U8g2返回值偏小)。ASCII不占单位会导致英文行超宽漏字。
  const int PER_HALF = (maxW / 14) * 2;   // 每行半单位数(中文2单位, 英文1单位)
  std::vector<std::string> out; std::string line; int units = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char ch = s[i]; int len = 1;
    if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3; else if (ch >= 0xC0) len = 2;
    std::string g = s.substr(i, len); i += len;
    if (g == "\n") { out.push_back(line); line.clear(); units = 0; continue; }
    // 中文(多字节)=2单位, ASCII=1单位(两个英文约等于一个中文宽度)
    int u = (len > 1 || (unsigned char)g[0] >= 0x80) ? 2 : 1;
    if (units + u > PER_HALF && !line.empty()) { out.push_back(line); line.clear(); units = 0; }
    line += g; units += u;
    if (units > PER_HALF) units = PER_HALF;  // 超长串保护
  }
  if (!line.empty()) out.push_back(line);
  return out;
}

static void setReply(const std::string& t) {
  reply = t; canvas.setFont(&fonts::efontCN_14);
  replyLines = wrapLines(petName + "：" + t, BARW); scrollTop = 0;
  // 长回复自动滚动到底部(输出完所有文字后,逐行滚动显示全部内容)
  if ((int)replyLines.size() > VIS) {
    gAutoScroll = true;
    gAutoScrollTarget = (int)replyLines.size() - VIS;
    gAutoScrollNext = millis() + 6000;  // 第一屏停留 6 秒看清再开始滚
  } else { gAutoScroll = false; }
}
static void setTransient(const char* a, uint32_t ms) { transientAction = a; transientUntil = millis() + ms; }

static void render() {
  Scenes::draw(canvas, millis(), gSceneIdx, (int)roamX);   // 天空+场景+网格+落地光圈
  Scenes::drawPanels(canvas, gSceneIdx, gIntimacy);        // HUD:时钟/日期/天气/亲密度(全真实)
  player.draw(canvas, (int)roamX - SPR_W / 2, GROUND - SPR_H, facing);  // 角色在最前

  canvas.setFont(&fonts::efontCN_14);
  // 右上角 WiFi 信号(4 格随强度);未连显示红叉;用缓存值避免频繁读异常
  { const int wx = 205, wy = 10; bool up = WiFi.status() == WL_CONNECTED; long rs = up ? gWifiRssi : 0;
    int bars = !up ? 0 : (rs >= -55 ? 4 : rs >= -65 ? 3 : rs >= -73 ? 2 : 1);
    uint16_t ac = !up ? 0x7BEF : (bars >= 3 ? 0x07E0 : bars == 2 ? 0xFFE0 : 0xFD20);
    for (int i = 0; i < 4; i++) { int bh = 2 + i * 2;
      canvas.fillRect(wx + i * 3, wy - bh, 2, bh, (up && i < bars) ? ac : 0x39C7); }
    if (!up) { canvas.drawLine(wx, 2, wx + 10, 10, 0xF800); canvas.drawLine(wx + 10, 2, wx, 10, 0xF800); } }

  // 右上角电池电量图标(百分比填充,右对齐,稳定显示不闪)
  { const int bx = 220, by = 2, bw = 15, bh = 9;  // 电池右对齐(最右侧,凸点到 237)
    int level = M5.Power.getBatteryLevel();
    if (level < 0) level = 0; if (level > 100) level = 100;
    uint16_t col = level <= 20 ? 0xF800 : 0x07E0;  // 低电量红,正常绿
    // 电池外壳
    canvas.drawRect(bx, by, bw, bh, 0x7BEF);
    canvas.fillRect(bx + bw, by + 2, 2, bh - 4, 0x7BEF);  // 正极凸点
    // 电量填充(4格,稳定显示)
    int filled = (level + 24) / 25;  // 0-4 格
    for (int i = 0; i < 4; i++) {
      if (i < filled) canvas.fillRect(bx + 2 + i * 3, by + 2, 2, bh - 4, col);
      else canvas.fillRect(bx + 2 + i * 3, by + 2, 2, bh - 4, 0x39C7);  // 空格
    }
  }

  // 对话框(背景=地面延伸色,与场景一体化;文字=Claude 橙色)
  canvas.fillRect(0, BAR_TOP, 240, 135 - BAR_TOP, 0x08A4);   // 地面延伸色
  // 拼音输入候选栏(组合中时显示在输入区上方)
  if (pinyinIME.isComposing() || pinyinIME.hasCandidates()) {
    canvas.setFont(&fonts::efontCN_14);
    canvas.setTextColor(0xFC4B, 0x08A4);   // 亮橙
    canvas.setCursor(4, BAR_Y);
    canvas.print("[");
    canvas.print(pinyinIME.getComposing());
    canvas.print("] ");
    int pageSize = pinyinIME.getPage() < pinyinIME.getTotalPages() - 1
                   ? 5 : (pinyinIME.getCandCount() - pinyinIME.getPage() * 5);
    if (pageSize > 5) pageSize = 5;
    if (pageSize < 0) pageSize = 0;
    for (int i = 0; i < pageSize; i++) {
      char num[4]; snprintf(num, sizeof(num), "%d.", i + 1);
      canvas.print(num);
      canvas.print(pinyinIME.candAt(i));
      canvas.print(" ");
    }
    if (pinyinIME.getTotalPages() > 1) {
      char pg[24]; snprintf(pg, sizeof(pg), "(%d/%d)", pinyinIME.getPage() + 1, pinyinIME.getTotalPages());
      canvas.print(pg);
    }
  } else if (!input.empty()) {
    // 输入显示 2 行(长输入自动换行);光标一闪一闪(终端风格);前缀显示中/EN 模式
    canvas.setTextColor(0xD3AB, 0x08A4);   // Claude橙
    std::string prompt = gTranslate ? "译> " : (gInputMode ? "中> " : "EN> ");
    auto ilines = wrapLines(prompt + input, BARW);
    if (ilines.empty()) ilines.push_back(prompt);
    for (int i = 0; i < 2 && i < (int)ilines.size(); i++) {
      canvas.setCursor(4, BAR_Y + i * LH);
      canvas.print(ilines[i].c_str());
      if (i == (int)ilines.size() - 1) {
        if ((millis() / 500) % 2) canvas.print("▍");  // 终端闪烁光标(方块)
      }
    }
  } else {
    for (int i = 0; i < VIS; i++) {
      int li = scrollTop + i;
      if (li < 0 || li >= (int)replyLines.size()) break;
      canvas.setTextColor(li == 0 ? 0xD3AB : 0xFC4B, 0x08A4);  // 第0行含"名字:"用Claude橙, 其余亮橙
      canvas.setCursor(4, BAR_Y + i * LH);
      canvas.print(replyLines[li].c_str());   // 名字前缀已包含在 replyLines 第0行(wrapLines时拼入)
    }
    if (scrollTop > 0) canvas.fillTriangle(232, BAR_Y + 2, 236, BAR_Y + 2, 234, BAR_Y - 2, 0x7BCF);
    if (scrollTop + VIS < (int)replyLines.size())
      canvas.fillTriangle(232, BAR_Y + 2 * LH - 2, 236, BAR_Y + 2 * LH - 2, 234, BAR_Y + 2 * LH + 2, 0x7BCF);
  }

  // P1 体验改进: THINKING/SPEAKING 状态加可爱的动态视觉反馈(14岁女孩等待时不会觉得卡死)
  if (gPhase == PH_THINKING) {
    // 思考中: 三个动态省略号(循环 . .. ...) 显示等待
    static const char* dots[3] = {".", "..", "..."};
    int d = (millis() / 400) % 3;
    canvas.setTextColor(0xD3AB, 0x08A4);  // Claude橙
    canvas.setCursor(4, BAR_Y);
    canvas.print("克劳德思考中");
    canvas.print(dots[d]);
  }
  canvas.pushSprite(0, 0);
}

static void brainTask(void*);  // 定义在下方


void setup() {
  Serial.begin(115200);
  delay(800);  // 等 USB CDC Serial 就绪, 确保诊断输出可见
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  // NS4150 D类功放空闲时产生电流声(嗡嗡): 静音消除(参考 M5Claw 避坑)
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(0);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(gBrightness);  // 默认亮度 50/255
  gIdleSince = millis();  // 省电关机:开机即开始空闲计时
  canvas.setColorDepth(16); canvas.createSprite(240, 135);
  PetTouch::init();  // 抚摸交互:初始化 BMI270 加速度计(零内存,硬件I2C独立于语音I2S)
  LittleFS.begin(false);  // 别 format-on-fail(launcher 模式下无 littlefs 分区,精灵已在 SD)
  // 修复: 精灵一律走内置 Flash(LittleFS), 不用 SD 卡。
  // 原因: 插了 SD 卡但初始化不完整时(sdCommand no token), SD 读精灵会全部失败,
  //       导致形象变灰/无动画。LittleFS 已烧录正确的 Clawd 精灵。
  gUseSD = false;
  player.begin((fs::FS&)LittleFS); player.setAction("idle");
  pinyinIME.init("/config/pinyin_dict.bin");  // 拼音输入法字典(精简单字,放 LittleFS)
  gMtx = xSemaphoreCreateMutex();
  // 后台核跑思考+朗读,24KB 栈(HTTPS/TLS 很吃栈),钉在 core 0(主循环在 core 1)
  xTaskCreatePinnedToCore(brainTask, "brain", 24 * 1024, nullptr, 1, nullptr, 0);
  setReply("连接 WiFi 中…"); render();

  if (wifiConnect()) {
    gWifiRssi = WiFi.RSSI();  // 缓存启动 RSSI
    Serial.printf("[WIFI] connected, RSSI=%d dBm, channel=%d\n", gWifiRssi, WiFi.channel());
    configTzTime("CST-8", "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");
    delay(300); gSceneIdx = Scenes::autoIdx(curHour());  // 按作息选初始场景
    setReply("请输入文本...");
    setTransient("waving", 2500);
  } else {
    setReply("连不上 WiFi…去 config.h 检查。");
    setTransient("sad", 0xFFFFFFFF);
  }
}

// 把一句话交给后台核去思考+朗读;立即返回,主循环继续跑动画。打字和语音共用
static void submitJob(const std::string& msg) {
  if (msg.empty()) return;
  gIdleSince = millis();  // 对话=有操作,重置省电计时
  xSemaphoreTake(gMtx, portMAX_DELAY);
  gJobMsg = msg; gJobReady = true;
  xSemaphoreGive(gMtx);
  setReply("(在想…)");
  input.clear();
}

// 按住 Opt 说话: 开始录音(状态机), 松开/5秒后自动上传 Azure STT → 提交对话
static void doPTT() {
  if (STT::state() != STT::STT_IDLE) return;
  gIdleSince = millis();
  STT::start();
  setReply("请说话…松开Opt发送");
}

// 轻柔爱的三连音(上行琶音 C5→E5→G5, 温暖感), 用内置 tone 零运行期内存
static void ringProactive() {
  M5Cardputer.Speaker.setVolume(60);   // 轻柔音量
  M5Cardputer.Speaker.tone(523, 150); delay(170);  // C5
  M5Cardputer.Speaker.tone(659, 150); delay(170);  // E5
  M5Cardputer.Speaker.tone(784, 200); delay(220);  // G5
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(0);    // 播完静音(消 NS4150 电流声)
}

// 抚摸交互反馈: 手势 → 动画 + 音效(内置tone零内存) + 固定互动模板(零内存)
static bool handleTouch(PetTouch::Gesture g) {
  // 随机模板: 用手势类型挑一句(固定字符串常量, 零堆分配)
  int r = (int)(esp_random() % 3);   // 0/1/2 随机
  switch (g) {
    case PetTouch::PET: {  // 抚摸(上下/左右3次): 眯眼开心 + 满足音 + 撒娇
      M5Cardputer.Speaker.setVolume(20);   // 轻柔音量
      player.setAction("happy");
      setTransient("happy", 3000);
      M5Cardputer.Speaker.tone(659, 120); delay(130);   // E5 满足
      static const char* t[] = { "喵~最舒服了", "好喜欢被摸", "再摸摸嘛" };
      setReply(t[r]);  break;
    }
    case PetTouch::ABUSE: {  // 粗暴(剧烈运动3次): 头晕/生气 + 警示音(稍大) + 委屈
      M5Cardputer.Speaker.setVolume(30);   // 警示音稍大一点(30/255)
      player.setAction("surprised");
      setTransient("surprised", 4000);
      M5Cardputer.Speaker.tone(196, 180); delay(200);   // G3 低音
      M5Cardputer.Speaker.tone(147, 250); delay(280);   // D3 更低
      static const char* t[] = { "呜…头好晕", "别晃我啦", "我有点疼…" };
      setReply(t[r]);  break;
    }
    default: M5Cardputer.Speaker.stop(); return false;
  }
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(0);  // 播完静音
  return true;
}

// 后台核(core 0): askPet(DeepSeek) → 出结果 → 刷新文字。不碰屏幕/键盘/精灵
static void brainTask(void*) {
  for (;;) {
    if (gJobReady) {
      std::string msg;
      xSemaphoreTake(gMtx, portMAX_DELAY); msg = gJobMsg; gJobReady = false; xSemaphoreGive(gMtx);

      gPhase = PH_THINKING;
      if (gTranslate) {  // 翻译模式:中英互译(不查记忆/不更新亲密度)
        std::string translation;
        bool ok = translateText(msg, translation);
        xSemaphoreTake(gMtx, portMAX_DELAY);
        gResReply = ok ? translation : "(翻译失败,请检查网络)";
        gResEmotion = ok ? "neutral" : "sad";
        gResName = ""; gResSeq++;
        xSemaphoreGive(gMtx);
      } else {  // 聊天模式
        PetReply r = askPet(msg);
        if (r.intimacy >= 0) gIntimacy = r.intimacy;
        xSemaphoreTake(gMtx, portMAX_DELAY);
        gResReply = r.reply; gResEmotion = r.emotion; gResName = r.name; gResSeq++;
        xSemaphoreGive(gMtx);
      }

      kbdIgnoreUntil = millis() + 300;  // 回复后冷却,防误触
      gPhase = PH_IDLE;
    }
    // WiFi 断了就在后台尝试重连(每 15s)
    static uint32_t reconNext = 0;
    if (WiFi.status() != WL_CONNECTED && millis() > reconNext) { WiFi.reconnect(); reconNext = millis() + 15000; }
    // 定期更新 RSSI 缓存(每 15s)+ 打印诊断
    static uint32_t rssiDiagNext = 0;
    if (WiFi.status() == WL_CONNECTED && millis() > rssiDiagNext) {
      int r = WiFi.RSSI(); if (r >= 0) r = -40;  // 异常值回退
      gWifiRssi = r;
      Serial.printf("[RSSI] %d dBm\n", gWifiRssi);
      rssiDiagNext = millis() + 15000;
    }
    // 场景卡片: 切换场景重新拉取 + 每 60s 刷新
    if (WiFi.status() == WL_CONNECTED && (gSceneIdx != gLastCardScene || millis() > gCardNext)) {
      Hub::fetch(gSceneIdx);
      gLastCardScene = gSceneIdx;
      gCardNext = millis() + 60000;
    }
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
  // 录音期间(STT_LISTEN)跳过 Opt 检测: 避免与 STT::update 竞争消费 keysState
  if (STT::state() != STT::STT_IDLE) return;
  // Opt 键优先检测(修饰键可能不触发 isChange/isPressed; 用 keysState 的 opt)
  {
    auto os = M5Cardputer.Keyboard.keysState();
    if (os.opt) {
      doPTT();   // 按住 Opt 说话
      return;
    }
  }
  if (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) return;
  auto st = M5Cardputer.Keyboard.keysState();
  // 思考/说话中不收其他键
  if (gPhase != PH_IDLE) return;
  gIdleSince = millis();  // 按键=有操作,重置省电计时
  // Ctrl 键: 切换场景(按一下换一个)
  if (st.ctrl) { gAutoScene = false; gSceneIdx = (gSceneIdx + 1) % Scenes::count();
                 setReply(std::string("场景：") + Scenes::name(gSceneIdx)); return; }
  // Aa 键(Shift): 切换中/英文输入
  if (st.shift) { gInputMode = !gInputMode; pinyinIME.clear();
                  setReply(gInputMode ? "中文输入" : "英文输入"); return; }
  // Alt 键: 切换 聊天/翻译 模式(中英互译)
  if (st.alt) { gTranslate = !gTranslate; pinyinIME.clear();
                setReply(gTranslate ? "翻译模式：中英互译" : "聊天模式"); return; }
  if (st.fn) {
    for (char c : st.word) {
      if (c == ',') { gAutoScroll = false; if (scrollTop > 0) scrollTop--; return; }
      if (c == '.') { gAutoScroll = false; if (scrollTop + VIS < (int)replyLines.size()) scrollTop++; return; }
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
  // 退格(提前处理): UTF-8 安全删除(中文汉字3字节,不能只删1字节否则乱码)
  if (st.del) {
    if (pinyinIME.isComposing()) { pinyinIME.backspace(); }     // 拼音组合中→删拼音字母
    else if (!input.empty()) {
      // 从末尾往前找出完整 UTF-8 字符起始
      size_t n = input.size(), start = n - 1;
      while (start > 0 && ((unsigned char)input[start] & 0xC0) == 0x80) start--;  // 跳过连续字节
      input.erase(start);                                      // 删整个 UTF-8 字符
    }
    return;
  }
  // ---- 输入分流(中文拼音 / 英文) ----
  for (char c : st.word) {
    if (c == 0x2a || c == 0x08) continue;  // 过滤退格键 HID 值,避免乱码框
    if (c == 0x00 || c == 0x60) {  // Esc 键 或 `(反引号)键 → 退出回 launcher (0x60=`ASCII, 0x35是数字5勿拦截)
      setReply("退出中…回到 launcher"); render();
      bootBackToLauncher();
      setReply("当前是整机直刷模式,没有 launcher 可回。"); return;
    }
    if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z') {
      if (gInputMode) { pinyinIME.addChar(c); }  // 中文→拼音
      else { input += c; gAutoScroll = false; }  // 英文→直接上屏
      continue;
    }
    if (c >= '1' && c <= '9' && pinyinIME.hasCandidates()) {  // 数字→选候选
      const char* picked = pinyinIME.select(c - '0');
      if (picked && picked[0]) input += picked;
      continue;
    }
    if (c == ' ') {  // 空格→选第1个候选
      if (pinyinIME.hasCandidates()) {
        const char* picked = pinyinIME.select(1);
        if (picked && picked[0]) input += picked;
      } else if (input.empty()) { input += ' '; }
      continue;
    }
    if (c == '/' || c == '?') { if (pinyinIME.hasCandidates()) pinyinIME.nextPage(); continue; }
    // 亮度快捷键: - 降低10%, = 增加10% (从待机调暗状态按 -/= 会先恢复亮度)
    if (c == '-') {
      if (gDimmed) { gDimmed = false; M5Cardputer.Display.setBrightness(gBrightness); gIdleSince = millis(); }
      int b = gBrightness; b = (b > 26) ? b - 26 : 0;  // 到最低 0 停止,不循环
      gBrightness = b; M5Cardputer.Display.setBrightness(b); continue; }
    if (c == '=') {
      if (gDimmed) { gDimmed = false; M5Cardputer.Display.setBrightness(gBrightness); gIdleSince = millis(); }
      int b = gBrightness; b = (b < 229) ? b + 26 : 255;  // 到最高 255 停止,不循环
      gBrightness = b; M5Cardputer.Display.setBrightness(b); continue; }
    if (pinyinIME.isComposing()) { input += pinyinIME.getComposing(); pinyinIME.clear(); }  // 标点→上屏拼音
    gAutoScroll = false;  // 用户开始打字→停止循环滚动
    input += c;
  }
  // 回车
  if (st.enter) {
    if (pinyinIME.isComposing()) { input += pinyinIME.getComposing(); pinyinIME.clear(); }
    if (!input.empty()) submitJob(input);
  }
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

// 省电调暗:10分钟无操作 → 屏幕亮度降为0(不关机,不深睡);按 -= 亮度键恢复
static void checkIdleDim() {
  uint32_t now = millis();
  const uint32_t IDLE_MS = 600000UL;       // 10 分钟
  if (now - gIdleSince < IDLE_MS) return;  // 仍在活跃期内
  if (gDimmed) return;                      // 已调暗,等待按键恢复
  // 超过 10 分钟: 亮度降为 0(省电), 不关机
  gDimmed = true;
  gShutdownWarned = true;
  M5Cardputer.Display.setBrightness(0);
}

void loop() {
  M5Cardputer.update();
  handleKeyboard();
  checkIdleDim();
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
    setTransient(emotionToAction(emotion), 6000);  // 说完后情绪再停留一会儿
  }

  // 长回复自动滚动:逐行滚动,慢速;滚到底回到顶部循环,直到用户手动滚动/新输入
  if (gAutoScroll) {
    if (now >= gAutoScrollNext) {
      if (scrollTop < gAutoScrollTarget) { scrollTop++; gAutoScrollNext = now + 6000; }
      else { scrollTop = 0; gAutoScrollNext = now + 6000; }  // 滚到底→回到顶部循环
    }
  }

  if (gPhase == PH_THINKING) {
    player.setAction("thinking");                  // 思考中:沉思动画(背景照常动)
  } else if (!transientAction.empty() && now < transientUntil) {
    player.setAction(transientAction.c_str());
    const ActionMeta* m = SpritePlayer::find(transientAction.c_str());
    if (m && !m->loop && player.finished()) transientUntil = 0;
  } else {
    transientAction.clear();
    if (input.empty()) roamStep(now, curHour()); else player.setAction("idle");
  }

  // 语音录音状态机: 录音中每帧驱动; 录音完成后识别并提交对话
  STT::update();
  if (STT::state() == STT::STT_PROCESS) {
    setReply("识别中…");
    std::string text = STT::recognize();
    STT::reset();
    if (!text.empty()) {
      setReply("你说：" + text);
      submitJob(text);  // 提交给 LLM 对话
    } else {
      setReply("没听清,请再试");
    }
  }

  // 抚摸交互: 空闲时检测手势(不干扰语音/思考), 触发动画+音效
  if (gPhase == PH_IDLE && STT::state() == STT::STT_IDLE) {
    PetTouch::Gesture g = PetTouch::update();
    if (g != PetTouch::NONE) {
      gIdleSince = millis();  // 有操作,重置省电计时
      handleTouch(g);
    }
  }

  // 主动对话: 每60s轮询后端 /proactive, 空闲时克劳德主动找主人说话
  if (WiFi.status() == WL_CONNECTED && millis() > gProNext) {
    gProNext = millis() + 60000;
    if (gPhase == PH_IDLE && STT::state() == STT::STT_IDLE) {  // 空闲才提醒
      if (Hub::fetchProactive(gProMsg, sizeof(gProMsg))) {
        setReply(std::string("克劳德：") + gProMsg);   // 显示主动消息
        if (millis() > gProRingUntil) {               // 三连音提醒(冷却防反复)
          ringProactive();
          gProRingUntil = millis() + 15000;
        }
        setTransient("thinking", 5000);                // 克劳德沉思一下
      }
    }
  }

  player.update(now);
  render();
  delay(5);
}
