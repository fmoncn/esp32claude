import express from 'express';
import { config } from './config.js';
import { loadPet } from './store.js';
import { describeState } from './pet.js';
import { respondToPet } from './brain.js';
import { transcribe, synthesize } from './voice.js';

const app = express();
app.use(express.json({ limit: '16kb' }));

// CORS:允许网页模拟器从浏览器调用
app.use((req, res, next) => {
  res.set('Access-Control-Allow-Origin', '*');
  res.set('Access-Control-Allow-Headers', 'content-type, x-pet-token');
  res.set('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});

// 可选共享口令校验
app.use((req, res, next) => {
  if (!config.petToken) return next();
  if (req.headers['x-pet-token'] === config.petToken) return next();
  return res.status(401).json({ error: 'unauthorized' });
});

app.get('/health', (req, res) => {
  res.json({
    ok: true,
    model: config.deepseek.model,
    hasKey: Boolean(config.deepseek.apiKey),
    hasVoice: Boolean(config.dashscope.apiKey),
  });
});

app.get('/pet/:id', async (req, res) => {
  const pet = await loadPet(req.params.id);
  if (!pet) return res.status(404).json({ error: 'no such pet' });
  res.json({ name: pet.name, stats: pet.stats, state: describeState(pet), facts: pet.facts });
});

// 文字对话
app.post('/chat', async (req, res) => {
  try {
    const { petId, message } = req.body || {};
    if (!petId || typeof message !== 'string' || !message.trim()) {
      return res.status(400).json({ error: 'petId 和 message 必填' });
    }
    const out = await respondToPet(petId, message);
    res.json({ reply: out.reply, emotion: out.emotion, name: out.name, stats: out.stats });
  } catch (err) {
    console.error('[/chat] error:', err.message);
    res.status(502).json({ error: '宠物大脑暂时不在线', detail: err.message });
  }
});

// 语音对话:POST 原始音频(WAV/PCM)为 body,?petId=xxx
// 返回:body = 24kHz mono int16 裸 PCM;文字/情绪放在响应头(base64)
app.post('/voice', express.raw({ type: () => true, limit: '10mb' }), async (req, res) => {
  try {
    const petId = req.query.petId;
    if (!petId) return res.status(400).json({ error: 'petId 必填(query)' });
    if (!req.body || !req.body.length) return res.status(400).json({ error: '音频 body 为空' });

    const heard = await transcribe(req.body, req.headers['content-type'] || 'audio/wav');
    if (!heard) return res.status(422).json({ error: '没听清(空转写)' });

    const out = await respondToPet(petId, heard);
    const pcm = await synthesize(out.reply);

    res.set({
      'Content-Type': 'audio/L16; rate=24000; channels=1',
      'X-Pet-Emotion': out.emotion,
      'X-Pet-Audio-Rate': String(config.dashscope.ttsSampleRate),
      'X-Pet-Name-B64': Buffer.from(out.name).toString('base64'),
      'X-Pet-Reply-B64': Buffer.from(out.reply).toString('base64'),
      'X-Pet-Heard-B64': Buffer.from(heard).toString('base64'),
    });
    res.send(pcm);
  } catch (err) {
    console.error('[/voice] error:', err.message);
    res.status(502).json({ error: '语音处理失败', detail: err.message });
  }
});

// 纯文字 → 语音(语音回复开启时朗读)。返回 24kHz mono int16 裸 PCM
app.post('/tts', async (req, res) => {
  try {
    const { text, voice } = req.body || {};
    if (!text || typeof text !== 'string' || !text.trim()) {
      return res.status(400).json({ error: 'text 必填' });
    }
    const pcm = await synthesize(text.slice(0, 500), voice || undefined);
    res.set({
      'Content-Type': 'audio/L16; rate=24000; channels=1',
      'X-Pet-Audio-Rate': String(config.dashscope.ttsSampleRate),
    });
    res.send(pcm);
  } catch (err) {
    console.error('[/tts] error:', err.message);
    res.status(502).json({ error: 'TTS 失败', detail: err.message });
  }
});

app.listen(config.port, '0.0.0.0', () => {
  const k = config.deepseek.apiKey ? 'set' : 'MISSING';
  const v = config.dashscope.apiKey ? 'set' : 'MISSING';
  console.log(`🐣 Cardpet on http://0.0.0.0:${config.port}  (brain: ${k}, voice: ${v})`);
});
