# AGENTS.md — 项目规范（给所有 AI Agent 的硬性约束）

> 本文件由 AI 编码 Agent（Hermes / Claude Code / Codex 等）在修改本仓库时**必须**遵守。
> 它是项目的最高约束，优先级高于单个任务的功能需求。

## ⚠️ 第一原则：所有部署必须以「不爆内存」为第一约束

本固件运行在 **M5Stack Cardputer / StampS3（ESP32-S3，无 PSRAM）** 上，可用 RAM 仅约 **327KB**（当前固件已用 ~88KB，heap 余量紧张）。

**任何代码改动 / 功能新增 / 部署决策，第一判断标准永远是「会不会爆内存 / 是否会增加运行期内存峰值」**，而不是"功能能不能实现"。

### 硬性规则（违反即视为 FAIL）
1. **禁止大块堆分配**：禁止 `new` / `malloc` 大缓冲、禁止大 `std::vector`/`std::string` 扩容、禁止把整段音频/数据读进 RAM。
2. **静态缓冲优先**：需要缓冲时用 `static` / 编译期固定大小数组（BSS），且大小受控（KB 级）。
3. **流式处理**：音频、文件、网络数据一律**流式**（读一块处理一块 / 直接写 LittleFS / 边收边转发），绝不在 RAM 里累积。
4. **新增 RAM 预算**：每次改动，静态内存增量应控制在 **≤ 几 KB**；运行期峰值必须经过评估。超过 8KB 静态增量需先在 memory 里说明理由。
5. **复用已有机制**：优先复用现有轮询（`gCardNext` 60s）、后台核（`brainTask`）、LittleFS、`Speaker.tone()` 等，不重复造占用内存的新通道。
6. **主动对话等新功能**：智能逻辑放 Dell 后端，固件只做**已有的 HTTP 轮询 + 已有的蜂鸣**，固件侧零/极小新增内存。

### 内存基线（当前实测）
- RAM：**26.8%（88KB / 327KB）**
- Flash：**81.0%（1434KB / 1769KB）**
- LittleFS 剩余：约 **314KB**（录音临时 `/rec.pcm` 5s≈160KB 用完即删）

---

## 硬件与环境

- **设备**：M5Stack Cardputer ADV（ESP32-S3，无 PSRAM）
- **板卡**：`board = m5stack-stamps3`，`platform = espressif32@6.9.0`
- **固件编译烧录在 Mac**（`fmon@LAN_IP`，`~/.local/bin/pio`）；Dell 无串口/工具链慢
- **后端**：Dell（`LAN_IP`）Node/Express，PM2 托管 `xiaodouding-backend`（:8787）
- **Dell Hub**（`LAN_IP:4000/4004`）：行情/持仓/VPS/Claude 额度等 JSON API，局域网无鉴权
- **语音链路**：
  - STT：设备直连 Azure Speech（westus3，`gpio_reset_pin(43/46)` 复位 ES8311，双缓冲乒乓录音）
  - TTS：Dell 后端 `/tts` 转发 Azure（晓悠音色），设备流式播放（单缓冲阻塞）
- **宠物名**：**克劳德（clawd）**（已从"小豆丁"全局改名）

## 音频避坑（重要）
- **NS4150 D 类功放空闲产生电流声**：不用 Speaker 时必须 `Speaker.setVolume(0)` 静音（setup 里已做）。
- **Mic/Speaker 切换**：先 `Speaker.end()` + `gpio_reset_pin(43/46)` 复位 ES8311，再 `Mic.begin()`；禁止用 Wire I2C 写 ES8311 寄存器（会死机）。
- **录音**：双缓冲乒乓 + `isRecording()!=2` 等填满 + 补槽；`magnification=48`；流式写 LittleFS，不占 RAM。

## 版本与回退
- `v1.0.0-beta1` → 语音功能前文字稳定版
- `v1.0.0-beta2` → 当前（语音输入 + review 修复）
- 回退：`git checkout <tag>` 后同步 firmware 到 Mac 编译烧录

## 安全（GitHub 同步）
- **不推送任何密钥/内网 IP**：真实值只在本地工作副本（`config.h`/`stt.h`/`dell_hub.h` 本地未提交版本）。
- GitHub 仓库 `fmoncn/esp32claude` 是**占位符版**（`YOUR_AZURE_KEY`/`LAN_IP`）。
- 绝不在 commit/文档里出现 `7GGC...`、`10.10.10.x`、WiFi 密码、token。

## 前端/文档规范
- 回复 ≤ 60 字、紧凑成一段、不换行不分段、少用符号/emoji/markdown；要点用"1、2、3"连着写。
- 行情颜色：中国市场习惯**红涨绿跌**（涨红 0xF800 / 跌绿 0x07E0）。
