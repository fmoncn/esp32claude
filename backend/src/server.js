import express from 'express';
import { config } from './config.js';
import { loadPet } from './store.js';
import { describeState } from './pet.js';
import { respondToPet } from './brain.js';
import { proactiveMessage } from './proactive.js';
import { translateText } from './llm.js';
import { createAzureTTSStream } from './azure_tts.js';

const app = express();
app.use(express.json({ limit: '16kb' }));

// 请求日志中间件: 记录所有请求(便于调试设备连接)
app.use((req, res, next) => {
  const t0 = Date.now();
  res.on('finish', () => {
    console.log(`[${new Date().toISOString()}] ${req.method} ${req.originalUrl} -> ${res.statusCode} (${Date.now()-t0}ms) from ${req.ip}`);
  });
  next();
});

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
    res.json({ reply: out.reply, emotion: out.emotion, name: out.name, stats: out.stats, suggestions: out.suggestions || [] });
  } catch (err) {
    console.error('[/chat] error:', err.message);
    res.status(502).json({ error: '宠物大脑暂时不在线', detail: err.message });
  }
});

// 主动对话: 克劳德根据空闲时长+记忆主动找主人说话(固件轮询拉取)
app.get('/proactive/:id', async (req, res) => {
  try {
    const out = await proactiveMessage(req.params.id);
    res.json(out);  // { has:false } 表示此刻不打扰; { has:true, message, emotion } 表示有主动话
  } catch (err) {
    console.error('[/proactive] error:', err.message);
    res.json({ has: false });  // 出错时不打扰,静默
  }
});

// 中英互译(Alt 翻译模式)
app.post('/translate', async (req, res) => {
  try {
    const { message } = req.body || {};
    if (typeof message !== 'string' || !message.trim()) {
      return res.status(400).json({ error: 'message 必填' });
    }
    const out = await translateText(message);
    if (!out.ok) throw new Error(out.error || '翻译失败');
    res.json({ translation: out.translation, from: out.from });
  } catch (err) {
    console.error('[/translate] error:', err.message);
    res.status(502).json({ error: '翻译服务暂时不可用', detail: err.message });
  }
});

// TTS: 调 Azure Speech (westus3), 流式转发音频给设备(边收边传, 首字节快达)
app.post('/tts', async (req, res) => {
  const { text } = req.body || {};
  if (typeof text !== 'string' || !text.trim()) {
    return res.status(400).json({ error: 'text 必填' });
  }
  const t0 = Date.now();
  try {
    const azureStream = await createAzureTTSStream(text.trim().slice(0, 200));
    // 告诉设备是 24kHz 16bit 单声道 WAV 流
    res.set('Content-Type', 'audio/wav');
    res.set('X-TTS-SampleRate', '24000');
    res.set('X-TTS-Bits', '16');
    res.set('X-TTS-Channels', '1');
    // 流式转发: 收到 Azure 一块就立刻转发给设备, 不做整体缓冲
    const reader = azureStream.getReader();
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (value) res.write(Buffer.from(value));
    }
    res.end();
    console.log(`[/tts] OK (${Date.now() - t0}ms, ${text.slice(0, 10)}…)`);
  } catch (err) {
    console.error('[/tts] error:', err.message);
    if (!res.headersSent) res.status(502).json({ error: 'TTS 服务暂时不可用', detail: err.message });
    else res.end();
  }
});

app.listen(config.port, '0.0.0.0', () => {
  const k = config.deepseek.apiKey ? 'set' : 'MISSING';
  console.log(`🐣 Cardpet on http://0.0.0.0:${config.port}  (brain: ${k})`);
});
