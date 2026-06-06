# Contributing to 小豆丁 (xiaodouding)

Thanks for your interest! This is a hobby/maker project — PRs and ideas are very welcome.

## Ground rules

- **Never commit secrets.** `backend/.env` and `firmware/src/config.h` are gitignored — keep
  your real WiFi password and API keys there. Use the `.example` templates as reference.
- **Never commit personal data.** `backend/data/` (per-pet memory & chat logs) is gitignored.
- **No copyrighted/trademarked art.** The bundled mascot is original. If you make a themed
  character (a movie hero, a game character, etc.), keep it in your own local `sprites_src/` —
  don't open a PR adding it here.

## Dev setup

See the [README Quick start](README.md#-quick-start). In short:

- **Firmware** — PlatformIO (`platform = espressif32@6.9.0`, `board = m5stack-stamps3`).
  `pio run` to build, `pio run -t upload` to flash, `pio run -t uploadfs` for sprites.
- **Backend** — Node ≥18. `cp .env.example .env`, `npm install`, `npm start`.
- **Visuals** — iterate in `sim/scenes.html` (browser), then port to `firmware/src/scenes.h`.
- **Sprites** — `python tools/gen_sprites.py` then `python tools/pack_sprites.py` (needs Pillow).

## Good first contributions

- New scenes (extend `SCENES[]` in `scenes.h` + the sim).
- Nicer / more expressive mascot art (`tools/gen_sprites.py` or your own `sprites_src/`).
- Additional LLM or voice backends (the brain is OpenAI-style chat in `backend/src/`).
- i18n of the on-device strings (currently mixed Chinese/English).
- Bug fixes, docs, wiring guides.

## Style

- Match the surrounding code. Keep files small and focused.
- Firmware: no heavy heap churn (no PSRAM); prefer compositor-friendly drawing.
- Explain *why* in comments where the hardware/ESP-IDF behavior is non-obvious.

## PRs

1. Fork, branch, make your change.
2. Confirm the firmware still builds (`pio run`) and the backend starts.
3. Open a PR describing what changed and how you tested it (which board, what you saw).
