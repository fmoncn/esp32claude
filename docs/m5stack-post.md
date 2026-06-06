# M5Stack community post (draft)

> Paste into community.m5stack.com (or the M5Stack forum / Reddit r/M5Stack).
> (GitHub: huaspirit123) and add a photo/GIF of your Cardputer.

---

**Title:** 小豆丁 (xiaodouding) — an LLM pixel pet that lives on the Cardputer

Hi everyone! I turned my **M5Stack Cardputer** into a little AI pixel pet and open-sourced it.

![pixel buddy](https://raw.githubusercontent.com/huaspirit123/xiaodouding/main/docs/mascot.png)

▶ **Try it right now in your browser (no hardware needed):** https://huaspirit123.github.io/xiaodouding/
— the pet roams the 10 scenes with a real clock & live weather; clone the repo for full chat on a real Cardputer.

**What it does**
- 💬 **Chats with memory** — an LLM brain (DeepSeek by default, any OpenAI-compatible
  endpoint works) running on your PC; it remembers facts about you across sessions.
- 🎙️ **Voice** — hold a key to talk (streaming speech-to-text), and it talks back (TTS),
  all on-device via DashScope. Push-to-talk up to ~1 minute.
- 🌆 **A little life** — it roams a "pixel holographic workstation" across **10 day/night
  scenes** (studio / living room / bedroom / city / desert / ocean / snow / forest / space…),
  switching indoor scenes by time of day, and reacts to your messages with moods.
- ⏰ **Real info on screen** — live clock, **real local weather** (open-meteo, auto-located),
  WiFi signal bars — animated continuously on the ESP32-S3's second core so it never freezes.

**Tech**
- Original Cardputer (ESP32-S3, no PSRAM). Firmware in PlatformIO (C++ / M5GFX).
- Node/Express "brain" on your LAN (keeps API keys off the device, holds memory).
- Device-direct voice (DashScope) + weather (open-meteo).
- Optional: run it alongside other apps via bmorcelli/Launcher (it's a normal app you can
  exit back to the menu).

The bundled character is an original generic mascot ("Pixel Buddy") — and it's a
**bring-your-own-character** design: drop in your own 64×72 sprite sheets and it's your pet.

**Live demo (browser):** 👉 https://huaspirit123.github.io/xiaodouding/
**Code + build instructions (Apache-2.0):** 👉 https://github.com/huaspirit123/xiaodouding

Feedback, scenes, and sprite art PRs very welcome. Have fun! 🤖
