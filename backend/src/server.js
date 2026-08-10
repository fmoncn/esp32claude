import express from 'express';
import { config } from './config.js';
import { loadPet } from './store.js';
import { describeState } from './pet.js';
import { respondToPet } from './brain.js';
import { proactiveMessage } from './proactive.js';
import { sceneGreet } from './scene.js';
import { translateText } from './llm.js';

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
// ?boot=1 表示开机主动: 每次开机克劳德主动聊当天 hub 数据(无视空闲/冷却)
app.get('/proactive/:id', async (req, res) => {
  try {
    const boot = req.query.boot === '1';
    const out = await proactiveMessage(req.params.id, boot);
    res.json(out);  // { has:false } 表示此刻不打扰; { has:true, message, emotion } 表示有主动话
  } catch (err) {
    console.error('[/proactive] error:', err.message);
    res.json({ has: false });  // 出错时不打扰,静默
  }
});

// 切场景主动: 用户切场景时, 克劳德聊该场景左上角 hub 卡片信息
app.get('/scene/:id/:idx', async (req, res) => {
  try {
    const idx = Number(req.params.idx) || 0;
    const out = await sceneGreet(req.params.id, idx);
    res.json(out);  // { has:false } 表示无数据/出错; { has:true, message } 表示克劳德的话
  } catch (err) {
    console.error('[/scene] error:', err.message);
    res.json({ has: false });
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

app.listen(config.port, '0.0.0.0', () => {
  const k = config.deepseek.apiKey ? 'set' : 'MISSING';
  console.log(`🐣 Cardpet on http://0.0.0.0:${config.port}  (brain: ${k})`);
});
