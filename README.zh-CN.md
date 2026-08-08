# 克劳德 · Clawd 🦀

**一只住在 M5Stack Cardputer 里的 AI 像素宠物。会说话、会撒娇、会主动找你聊天、还喜欢被你抚摸。**

[English](README.md) · **中文** · ▶ [**在浏览器里试玩**](https://huaspirit123.github.io/xiaodouding/)

> 把你的掌上 Cardputer 变成一只活生生的电子宠物 —— 它记得你、理解你、会回应你的触摸，
> 还能在 10 个昼夜场景里自己过日子。**全部跑在无 PSRAM 的 ESP32-S3 上，极省内存。**

---

## ✨ 它为什么不一样？

**🐾 会摸你 (抚摸交互)**
内置 **BMI270 加速度计**感知你的动作 —— 轻轻抚摸它，它会眯眼撒娇"喵~最舒服了"；
抱在怀里摇晃，它会困倦入睡；粗暴晃动，它会委屈地"呜…头好晕"。**不需要任何额外硬件。**

**🗣️ 会说话 (语音)**
按住 `Opt` 键说话，设备直连 Azure 语音识别你的话；克劳德用温柔的声音回你。
语音输入经过反复优化 —— **首次实现不爆内存的流式录音**。

**💬 会主动找你 (主动对话)**
不只会被动回答。克劳德会根据自己的记忆和你的聊天记录，**在你空闲时主动开口**，
用轻柔的三连音提醒你 —— 像真正的宠物一样想念你。

**🧠 真记忆 + 真人格**
后端 Node "大脑" 有 3 层记忆（短期 + 滚动摘要 + 关于你的长期事实），
克劳德会记得你的喜好、计划、心情，用符合它性格的方式回应。

**🌍 真实世界数据**
时钟/日期（NTP）、**实时天气**（open-meteo 自动定位）、WiFi 信号。
还能显示**你的投资组合行情**（标普/纳指/上证/恒生）、持仓、收益、补仓信号。

**🎨 10 个昼夜场景**
"像素全息工作台"风格 —— 深蓝蓝图网格 + 霓虹辉光。室内场景按作息自动切换，
室外场景手动切换。克劳德在其中自由漫步、吃饭、睡觉、工作。

**🧵 永不卡顿**
思考和语音在**第二个 CPU 核心**运行，主循环持续渲染动画 —— 克劳德"思考"时也一直在动。

---

## 🧰 硬件

- **M5Stack Cardputer / Cardputer ADV**（ESP32-S3FN8，8MB Flash，**无 PSRAM**）
- 同局域网的一台电脑运行后端"大脑"
- 可选 microSD（多 app launcher 模式）

---

## 🚀 快速开始

### 1. 后端（大脑）

```bash
cd backend
cp .env.example .env          # 填 DEEPSEEK_API_KEY / AZURE_SPEECH_KEY
npm install
npm start                     # http://0.0.0.0:8787
```

### 2. 精灵图

```bash
python tools/gen_sprites.py   # 生成默认像素宠物
python tools/pack_sprites.py  # → 设备格式
```

### 3. 固件

```bash
cd firmware
cp src/config.h.example src/config.h   # 填 WiFi、后端 IP、Azure key
pio run -t upload            # 编译 + 烧录
pio run -t uploadfs          # 上传精灵到 LittleFS
```

---

## 🎮 操作

| 操作 | 按键 |
|------|------|
| 打字聊天 | 输入 + `Enter` |
| **语音输入** | 按住 `Opt` 说话，松开发送 |
| **抚摸** | 轻轻摸/摇晃设备（无需按键）|
| 切场景 | `Fn` + `[` / `]` |
| 场景跟作息 | `Fn` + `\` |
| 语音开关 | `Fn` + `V` |
| 音量 | `Fn` + `/` |
| 亮度 | `-` / `=` |
| 中英输入 | `Shift`（Aa）|
| 翻译模式 | `Alt` |
| 播放表情 | `Fn` + `1`…`0` |

---

## 🏗 架构

```
┌──────────────┐  WiFi/LAN   ┌────────────────────┐   HTTPS   ┌────────────┐
│  Cardputer    │  ────────►  │  backend (Node)    │ ────────► │  LLM       │
│  firmware     │  /chat     │  brain + 3层记忆   │           │ (DeepSeek) │
│ (C++/PIO)     │  ◄────────  │  data/<pet>.json   │           └────────────┘
└──────┬───────┘             └────────────────────┘
       │ 设备直连: Azure STT/TTS · open-meteo · Dell Hub(行情)
       ▼
  BMI270 加速度计(抚摸交互) · I2C 独立于语音 I2S
```

- **为什么用后端？** API key 不暴露在设备上，记忆有地方存，换模型只改一处。
- **为什么设备直连？** 语音（Azure）和天气（open-meteo）从设备干净 WiFi 直达；
  只有对话大脑走电脑。

---

## 🛠 技术亮点（省内存的硬功夫）

- **无 PSRAM（~327KB RAM）**：单全屏 canvas + 精灵按动作流式加载；TLS 很吃栈，
  loop 任务栈加大 + 思考/语音放第二核心。
- **语音输入不爆内存**：双缓冲乒乓录音 + `isRecording()` 等填满 + 流式写 LittleFS，
  只占 ~8KB 静态内存 —— 首次在无 PSRAM 上稳定实现语音输入。
- **抚摸交互零内存**：BMI270 加速度计手势检测（抚摸/摇晃/粗暴），只用 ~96B 静态缓冲。
- **主动对话零内存**：智能逻辑全在后端，固件只做已有 HTTP 轮询 + 内置 `tone()` 蜂鸣。
- **ES8311 codec** 用 GPIO 复位（禁用 I2C Wire 复位，避免死机），NS4150 功放静音消电流声。
- **`sim/` 浏览器孪生**：先在浏览器调好视觉，再移植到 `scenes.h`。

---

## 🎨 自定义精灵

精灵是每动作 64×72 帧的 sprite sheet。默认克劳德是原创程序生成艺术。
想换自己的角色：

1. 帧放 `sprites_src/frames/<action>/<action>_<i>.png`（RGBA, 64×72）+ `metadata.json`
2. `python tools/pack_sprites.py` 重新打包
3. `pio run -t uploadfs`

> ⚠️ 别提交有版权的角色。自制角色请留在本地。

---

## 📁 仓库结构

```
firmware/     ESP32-S3 固件(PlatformIO)。scenes.h 场景渲染, main.cpp 主程序, stt.h 语音,
              pet_touch.h 抚摸交互
backend/      Node/Express "大脑": LLM 对话 + 3层记忆 + /tts + /proactive
sim/          浏览器设备孪生 + 场景预览
tools/        gen_sprites.py(生成像素宠物) + pack_sprites.py(转设备格式)
sprites_src/  默认像素宠物源帧
```

---

## 📜 许可证

[Apache-2.0](LICENSE)。

---

## 🙏 致谢

LLM: [DeepSeek](https://deepseek.com)（可换任意 OpenAI 兼容端点）· 语音: [Azure Speech](https://azure.microsoft.com/products/ai-services/ai-speech/) ·
天气: [open-meteo](https://open-meteo.com) · 硬件: [M5Stack Cardputer](https://m5stack.com)

> 与上述公司无关联，请自带 API key。
