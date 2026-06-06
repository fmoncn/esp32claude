# 小豆丁 · xiaodouding

**English** · [中文](README.zh-CN.md) · ▶ [**Try it live in your browser**](https://huaspirit123.github.io/xiaodouding/)

**An LLM-powered pixel pet for the M5Stack Cardputer.** It chats with you (with persistent
memory), talks back with voice, and lives an autonomous little life — roaming a holographic
"pixel workstation" across 10 day/night scenes, showing the real time, real weather, and WiFi
signal, and reacting to your messages with moods.

[![demo — Pixel Buddy across the 10 scenes](docs/demo.gif)](https://huaspirit123.github.io/xiaodouding/)

> ▶ **[Play with it live, in your browser →](https://huaspirit123.github.io/xiaodouding/)** — the pet roams, real clock & weather, no install. (Chat needs the local backend.)

> The bundled character is **Pixel Buddy**, an original generic mascot. Bring your own art —
> see [Sprites / bring your own character](#sprites--bring-your-own-character).

---

## ✨ Features

- **Real conversation + memory** — talks via an LLM brain (DeepSeek by default; any
  OpenAI-compatible endpoint works). 3-layer memory: short term + rolling summary + facts
  about you. Replies come back with an *emotion* that drives the on-screen animation.
- **Voice** — hold a key to talk (streaming speech-to-text), and the pet speaks its reply
  (text-to-speech), all on-device via Alibaba DashScope. Push-to-talk up to ~1 minute.
- **Alive when idle** — the pet walks around, does activities by time-of-day schedule
  (work / eat / sleep…), and animates continuously on a second CPU core so the UI never
  freezes while it "thinks" or speaks.
- **10 scenes, one cohesive style** — "pixel holographic workstation": deep-blue blueprint
  grid + neon glow + crisp pixel art. Indoor (studio/living room/bedroom) auto-switch by
  time of day; outdoor (city/desert/grassland/ocean/snow/forest/space) switch manually.
- **Real info on screen** — live clock & date (NTP), **real weather** (device pulls
  [open-meteo](https://open-meteo.com), auto-located by IP), WiFi signal bars, bond meter.
- **Long-text paging**, volume control, voice on/off — all from the Cardputer keyboard.

## 🧰 Hardware

- **M5Stack Cardputer (original / StampS3, ESP32-S3FN8)** — 8MB flash, **no PSRAM**.
- A **microSD card** is optional but recommended (sprites can run from SD; also enables the
  optional multi-app launcher setup).
- A computer on the **same LAN** to run the backend "brain".

## 🏗 Architecture

```
 ┌──────────────┐   WiFi/LAN    ┌─────────────────────┐   HTTPS    ┌────────────┐
 │  Cardputer   │  ───────────► │  backend (Node/Express)         │  LLM        │
 │  firmware    │  /chat        │  brain + 3-layer memory ───────► │ (DeepSeek…) │
 │ (C++/PlatformIO)             │  data/<petId>.json  │           └────────────┘
 │              │  ◄─────────── │  reply + emotion    │
 └──────┬───────┘               └─────────────────────┘
        │  HTTPS (device-direct, voice + weather)
        ▼
   DashScope (STT/TTS)   ·   open-meteo (weather)
```

- **Why a backend?** Keeps your API keys off the device, gives memory a place to live, and
  lets you swap the model in one place. The brain is plain OpenAI-style chat — point it at
  DeepSeek, OpenAI, or a local Ollama by editing `backend/src/config.js`.
- **Why device-direct voice/weather?** DashScope + open-meteo are reachable from the device's
  clean WiFi; only the chat brain goes through your computer.

## 📁 Repo layout

```
firmware/      ESP32-S3 firmware (PlatformIO). scenes.h = 10-scene renderer, main.cpp = app.
backend/       Node/Express "brain": LLM chat + memory + voice proxy.
sim/           Browser "device twin" + scene previewer (great for tuning visuals fast).
tools/         gen_sprites.py (make the generic mascot) + pack_sprites.py (→ device format).
sprites_src/   Source frames for the bundled mascot (regenerate or replace with your own).
```

## 🚀 Quick start

### 1. Backend (the brain)

```bash
cd backend
cp .env.example .env          # fill in DEEPSEEK_API_KEY (and DASHSCOPE_API_KEY for voice)
npm install
npm start                     # serves on http://0.0.0.0:8787
```

Find your computer's LAN IP (e.g. `192.168.1.20`) — you'll put it in the firmware config.

### 2. Sprites

The bundled generic mascot is already generated, but to (re)build or customize:

```bash
pip install pillow
python tools/gen_sprites.py   # → sprites_src/   (the original Pixel Buddy)
python tools/pack_sprites.py  # → firmware/data/sprites/*.bin + firmware/src/sprites_meta.h
```

### 3. Firmware

```bash
cd firmware
cp src/config.h.example src/config.h     # set WiFi, BACKEND_URL (your LAN IP), DashScope key
pio run -t upload                        # build + flash (PlatformIO)
pio run -t uploadfs                      # upload sprites to the device's LittleFS
```

Open the Cardputer: type and press **Enter** to chat. (See controls below.)

## 🎮 Controls (Cardputer keyboard)

| Key | Action |
|-----|--------|
| type + `Enter` | send a chat message |
| hold `Opt` | push-to-talk: speak, release to send |
| `Fn` + `,` / `.` | page long replies up / down |
| `Fn` + `[` / `]` | previous / next scene |
| `Fn` + `\` | scenes follow the daily schedule again |
| `Fn` + `/` | cycle volume |
| `Fn` + `V` | voice replies on / off |
| `Fn` + `1`…`0` | trigger action animations |

## 🎨 Sprites / bring your own character

The device plays per-action sprite sheets (64×72 frames). The bundled **Pixel Buddy** is
original procedural art. To use your own character:

1. Put your frames under `sprites_src/frames/<action>/<action>_<i>.png` (RGBA, 64×72) plus a
   `sprites_src/metadata.json` (see the generated one for the format & the 34 action names).
   Tip: AI-generate a sprite sheet, or draw your own — keep the action names the same.
2. `python tools/pack_sprites.py` → repacks to the device format.
3. `pio run -t uploadfs` (or copy `firmware/data/sprites/` to the SD card root as `/sprites/`).

> ⚠️ Please don't commit copyrighted/trademarked characters to this repo. Keep those local.

## 🔊 Voice & 🌤 Weather

Voice (STT + TTS) and weather are optional and run device-direct. Leave `DASHSCOPE_API_KEY`
empty to disable voice (text chat still works). Weather auto-locates by IP via open-meteo
(no key); edit the coordinates in `firmware/src/weather.h` to pin a city.

## 🧩 Optional: run alongside other apps (launcher)

Want a "phone-like" setup where the pet is one of several apps you can switch between? Flash
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) (use its web flasher, pick
*M5Stack → Cardputer*), copy `firmware/.pio/build/cardputer/firmware.bin` to the SD card, and
install it from the launcher's SD menu. The pet's `Fn+Q` returns to the launcher. Sprites must
live on the SD card in this mode (the firmware auto-migrates them on first boot).

## 🛠 Tech notes (the hard-won bits)

- **No PSRAM** (~300KB heap): one full-screen canvas, sprite frames streamed per-action; TLS
  is heavy so the loop task stack is enlarged and the chat/voice run on the **second core**.
- **Half-duplex audio**: mic and speaker share a pin — the firmware switches between them and
  resets the relevant GPIOs around each switch.
- **Streaming STT** over WebSocket (paraformer-realtime) so a minute of speech fits without
  buffering the whole clip.
- The `sim/` browser twin renders the same scenes — tune visuals there (fast), then port to
  `scenes.h`.

## 💬 Community

- **M5Stack forum** show & tell: https://community.m5stack.com/topic/8246/
- Questions & ideas: [GitHub Discussions](https://github.com/huaspirit123/xiaodouding/discussions) or [issues](https://github.com/huaspirit123/xiaodouding/issues)
- Built your own pet with it? Share it — PRs and "here's mine" posts very welcome.

## 🤝 Contributing

PRs welcome — see [CONTRIBUTING.md](CONTRIBUTING.md). Good first issues: more scenes, nicer
mascot art, additional LLM/voice backends, English/i18n of the on-device strings.

## 📜 License

[Apache-2.0](LICENSE). The bundled **Pixel Buddy** mascot art is original and also Apache-2.0.

## 🙏 Credits & notes

- LLM: [DeepSeek](https://deepseek.com) (default, swappable). Voice: Alibaba
  [DashScope](https://dashscope.console.aliyun.com) (Qwen ASR/TTS). Weather:
  [open-meteo](https://open-meteo.com). Hardware: [M5Stack Cardputer](https://m5stack.com).
- Not affiliated with or endorsed by any of the above. Bring your own API keys.
