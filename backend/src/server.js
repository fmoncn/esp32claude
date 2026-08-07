import express from 'express';
import { config } from './config.js';
import { loadPet } from './store.js';
import { describeState } from './pet.js';
import { respondToPet } from './brain.js';

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
    res.json({ reply: out.reply, emotion: out.emotion, name: out.name, stats: out.stats });
  } catch (err) {
    console.error('[/chat] error:', err.message);
    res.status(502).json({ error: '宠物大脑暂时不在线', detail: err.message });
  }
});

app.listen(config.port, '0.0.0.0', () => {
  const k = config.deepseek.apiKey ? 'set' : 'MISSING';
  console.log(`🐣 Cardpet on http://0.0.0.0:${config.port}  (brain: ${k})`);
});
