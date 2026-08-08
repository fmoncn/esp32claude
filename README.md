# Clawd 🦀 — An AI Pixel Pet That Lives in Your Pocket

**A living, breathing AI companion for the M5Stack Cardputer.** It talks, remembers, comes to you when you're idle, and loves being touched. And it runs entirely on an ESP32-S3 with **no PSRAM**.

**[English](README.md)** · **[中文](README.zh-CN.md)**

---

![Clawd across several of its 10 day/night scenes](docs/clawd_scenes.png)

> Turn your handheld Cardputer into a pet that actually feels alive — it remembers you, understands your mood, reacts to your touch, and lives its own little life across 10 scenes. **Zero-PSRAM, extreme memory efficiency.**

---

## ✨ Why Clawd is different

### 🐾 It loves being touched
Clawd uses the onboard **BMI270 accelerometer** to *feel* your gestures — no extra hardware.
Stroke it gently and it'll purr *"喵~最舒服了"*. Rock it in your arms and it'll drift off to sleep. Handle it roughly and it'll protest *"呜…头好晕"*.

### 🗣️ It listens to you
Hold `Opt` and speak — your voice is recognized on-device via **Azure Speech-to-Text** and filled into the chat box automatically. Push-to-talk, hands-free.

### 💬 It comes to you
Clawd isn't just reactive. Based on your chat history and what it remembers, it'll **initiate conversation when you're idle**, nudging you with a soft three-note chime — like a pet that actually misses you.

### 🧠 Real memory, real personality
A Node.js "brain" with **3-layer memory** (short-term + rolling summary + long-term facts about you). Clawd remembers your plans, your worries, your preferences — and responds in a warm, human way.

### 🌍 Real-world data on screen
Live clock & date (NTP), **real weather** (auto-located via open-meteo), WiFi signal — plus **your investment portfolio** (S&P / Nasdaq / SSE / Hang Seng), holdings, gains, and rebalancing signals.

### 🎨 10 day/night scenes
A "pixel holographic workstation" — deep-blue blueprint grids, neon glow, crisp pixel art. Indoor scenes auto-switch by time of day; outdoor scenes switch manually. Clawd roams, eats, sleeps, and works inside them.

### 🧵 Never lags
Thinking runs on the **second CPU core**, so the main loop keeps rendering animation — Clawd keeps moving even while it "thinks".

---

## 🧰 Hardware

| Component | Detail |
|-----------|--------|
| **Board** | M5Stack Cardputer / Cardputer ADV (ESP32-S3FN8, 8MB flash, **no PSRAM**) |
| **Backend** | Any computer on the same LAN runs the Node "brain" |
| **Optional** | microSD (multi-app launcher mode) |

---

## 🚀 Quick Start

### 1. Backend (the brain)

```bash
cd backend
cp .env.example .env          # add your API keys
npm install
npm start                     # serves on http://0.0.0.0:8787
```

### 2. Sprites

```bash
python tools/gen_sprites.py   # generate the default pixel pet
python tools/pack_sprites.py  # → device format
```

### 3. Firmware

```bash
cd firmware
cp src/config.h.example src/config.h   # set WiFi, backend IP
pio run -t upload            # build + flash
pio run -t uploadfs          # upload sprites to LittleFS
```

---

## 🎮 Controls

| Action | Key |
|--------|-----|
| Type & chat | type + `Enter` |
| **Voice input** | hold `Opt`, speak, release to send |
| **Touch / pet it** | stroke or rock the device (no key needed) |
| Next / prev scene | `Fn` + `]` / `[` |
| Scenes follow schedule | `Fn` + `\` |
| CN / EN input | `Shift` (Aa) |
| Translate mode | `Alt` |
| Page long replies | `Fn` + `.` / `,` |
| Play an emotion | `Fn` + `1`…`0` |
| Brightness | `-` / `=` |

---

## 🏗 Architecture

```
┌──────────────┐  WiFi/LAN   ┌────────────────────┐   HTTPS   ┌────────────┐
│  Cardputer    │  ────────►  │  backend (Node)    │ ────────► │  LLM       │
│  firmware     │  /chat     │  brain + 3-layer    │           │ (DeepSeek) │
│ (C++/PIO)     │  ◄────────  │  memory            │           └────────────┘
└──────┬───────┘             └────────────────────┘
       │  device-direct: Azure STT · open-meteo · Dell Hub (indices)
       ▼
  BMI270 accelerometer (pet-touch) · I2C independent of voice I2S
```

- **Why a backend?** Keeps API keys off the device, gives memory a home, and lets you swap models in one place.
- **Why device-direct?** Voice input (Azure STT) and weather (open-meteo) reach the device directly over clean WiFi; only the chat brain goes through your computer.

---

## 🛠 Engineering Highlights (the hard-won parts)

Clawd is built for **no-PSRAM ESP32-S3** (~327KB RAM). These are the tricks that make it work:

- **No PSRAM** — single full-screen canvas, sprites streamed per action; TLS is heavy, so the loop stack is enlarged and thinking runs on the second core.
- **Voice input without OOM** — double-buffered ping-pong recording + `isRecording()` wait + streaming to LittleFS, only ~8KB static RAM. The first stable no-PSRAM voice input.
- **Pet-touch with ~zero RAM** — BMI270 gesture detection (stroke / rock / rough) using just ~32B of static state.
- **Proactive chat with ~zero RAM** — all the intelligence lives in the backend; the firmware just polls HTTP and plays the built-in `tone()` chime.
- **ES8311 codec** reset via GPIO (not I2C Wire, avoids crashes); NS4150 amp muted to kill idle noise.

---

## 📁 Repo Layout

```
firmware/     ESP32-S3 firmware (PlatformIO). scenes.h = scene renderer,
              main.cpp = app, stt.h = voice input, pet_touch.h = touch interaction
backend/      Node/Express "brain": LLM chat + 3-layer memory + proactive + translate
sim/          Browser device twin + scene preview
tools/        gen_sprites.py (generate pet) + pack_sprites.py (device format)
sprites_src/  Source frames for the bundled pixel pet
```

---

## 📜 License

[Apache-2.0](LICENSE).

---

## 🙏 Credits

This project is a fork/derivative of the **[小豆丁 (xiaodouding)](https://github.com/huaspirit123/xiaodouding)** pixel pet by **[@huaspirit123](https://github.com/huaspirit123)** — huge thanks to the original author for the foundation this pet is built on.

Also thanks to the projects it leans on: LLM — [DeepSeek](https://deepseek.com) (or any OpenAI-compatible endpoint) · Voice input — [Azure Speech](https://azure.microsoft.com/products/ai-services/ai-speech/) · Weather — [open-meteo](https://open-meteo.com) · Hardware — [M5Stack Cardputer](https://m5stack.com).

> Not affiliated with or endorsed by any of the above. Bring your own API keys.
