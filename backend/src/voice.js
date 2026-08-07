// voice.js — 方案A改造版
// STT: 本地 Whisper (常驻服务) 
// TTS: 微软 Azure TTS (dell_tts.py :8792) + 16kHz→24kHz 重采样
import { config } from './config.js';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';

const execFileAsync = promisify(execFile);

// ===== 常驻 Whisper 服务 (一个独立 Python HTTP 服务, 在 backend/scripts/whisper_server.py) =====
const WHISPER_SERVER = 'http://127.0.0.1:8796/transcribe';

// ===== dell_tts.py 配置 =====
const TTS_URL = 'http://127.0.0.1:8792/tts';
const TTS_TOKEN_FILE = path.join(os.homedir(), '.cardputer-dell-tts-token');

function readToken() {
  try { return fs.readFileSync(TTS_TOKEN_FILE, 'utf8').trim(); } catch { return ''; }
}

// ===== 工具: 解析 WAV, 提取 PCM =====
function wavToPcm16(wavBuffer) {
  // 解析 WAV header (支持标准 PCM)
  if (wavBuffer.length < 44) throw new Error('WAV 太短');
  if (wavBuffer.toString('ascii', 0, 4) !== 'RIFF' || wavBuffer.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('不是标准 WAV');
  }
  const fmt = wavBuffer.indexOf('fmt ');
  const data = wavBuffer.indexOf('data');
  const channels = wavBuffer.readUInt16LE(fmt + 10);
  const sampleRate = wavBuffer.readUInt32LE(fmt + 12);
  const bitsPerSample = wavBuffer.readUInt16LE(fmt + 22);
  // 只支持 16bit 单声道
  if (bitsPerSample !== 16) throw new Error('仅支持 16bit: ' + bitsPerSample);
  const pcmStart = data + 8;
  const pcm = wavBuffer.subarray(pcmStart);
  return { pcm, channels, sampleRate, bitsPerSample };
}

// ===== 工具: 16kHz → 24kHz 线性插值重采样 =====
function resample16to24(pcm16, srcRate) {
  const TARGET = 24000;
  const ratio = TARGET / srcRate; // 1.5 for 16k
  const n = pcm16.length / 2;
  const out = Buffer.alloc(Math.floor(n * ratio) * 2);
  for (let i = 0; i < out.length / 2; i++) {
    const pos = i / ratio;
    const i0 = Math.floor(pos);
    const i1 = Math.min(i0 + 1, n - 1);
    const frac = pos - i0;
    const s0 = pcm16.readInt16LE(i0 * 2);
    const s1 = pcm16.readInt16LE(i1 * 2);
    out.writeInt16LE(Math.round(s0 * (1 - frac) + s1 * frac), i * 2);
  }
  return out;
}

// ===== STT: 本地 Whisper 常驻服务 =====
export async function transcribe(audioBuffer, mime = 'audio/wav') {
  const res = await fetch(WHISPER_SERVER, {
    method: 'POST',
    headers: { 'Content-Type': mime || 'audio/wav' },
    body: Buffer.isBuffer(audioBuffer) ? audioBuffer : Buffer.from(audioBuffer),
  });
  if (!res.ok) throw new Error('Whisper STT ' + res.status + ': ' + (await res.text()).slice(0, 200));
  const data = await res.json();
  return String(data?.text || '').trim();
}

// ===== TTS: 微软 Azure (dell_tts.py) + 重采样到 24kHz 裸 PCM =====
export async function synthesize(text, voice) {
  const token = readToken();
  const res = await fetch(TTS_URL, {
    method: 'POST',
    headers: { 'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json' },
    body: JSON.stringify({ text, voice: voice || undefined }),
  });
  if (!res.ok) throw new Error('dell_tts ' + res.status + ': ' + (await res.text()).slice(0, 200));
  const wav = Buffer.from(await res.arrayBuffer());
  const { pcm, sampleRate } = wavToPcm16(wav);
  // 重采样到 24kHz (dell_tts 返回 16kHz)
  return resample16to24(pcm, sampleRate);
}

// 兼容小豆丁的 ttsSampleRate 引用
export const ttsSampleRate = 24000;
