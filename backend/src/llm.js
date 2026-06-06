import { config } from './config.js';

async function callDeepSeek(messages, { json = false } = {}) {
  if (!config.deepseek.apiKey) {
    throw new Error('DEEPSEEK_API_KEY 未配置,请在 backend/.env 里填上');
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), config.deepseek.timeoutMs);
  try {
    const res = await fetch(config.deepseek.baseUrl, {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        authorization: `Bearer ${config.deepseek.apiKey}`,
      },
      body: JSON.stringify({
        model: config.deepseek.model,
        messages,
        temperature: config.deepseek.temperature,
        ...(json ? { response_format: { type: 'json_object' } } : {}),
      }),
      signal: controller.signal,
    });
    if (!res.ok) {
      const detail = await res.text().catch(() => '');
      throw new Error(`DeepSeek ${res.status}: ${detail.slice(0, 200)}`);
    }
    const data = await res.json();
    return data.choices?.[0]?.message?.content ?? '';
  } finally {
    clearTimeout(timer);
  }
}

// 容忍模型偶尔包了代码块或夹带文字
function extractJson(text) {
  const cleaned = text.replace(/```json|```/gi, '').trim();
  const start = cleaned.indexOf('{');
  const end = cleaned.lastIndexOf('}');
  if (start === -1 || end === -1) return null;
  try {
    return JSON.parse(cleaned.slice(start, end + 1));
  } catch {
    return null;
  }
}

function normalizeReply(parsed, raw) {
  const fromParsed = String(parsed.reply ?? '').trim();
  if (fromParsed) return fromParsed;
  // 模型偶尔不按 JSON 给;raw 是纯文本时也能用(排除 raw 本身就是 JSON 的情况)
  const rawTrim = String(raw ?? '').trim();
  if (rawTrim && !rawTrim.startsWith('{')) return rawTrim;
  return '';
}

async function requestPet(messages) {
  const raw = await callDeepSeek(messages, { json: true });
  const parsed = extractJson(raw) || {};
  return {
    reply: normalizeReply(parsed, raw),
    emotion: String(parsed.emotion || 'neutral').trim(),
    remember: String(parsed.remember || '').trim(),
    statChanges:
      parsed.stat_changes && typeof parsed.stat_changes === 'object' ? parsed.stat_changes : {},
  };
}

// 让宠物回话;偶发空回复补一句指示重试一次,仍空则兜底并标记 ok=false
export async function chatAsPet(systemPrompt, history, userText) {
  const messages = [
    { role: 'system', content: systemPrompt },
    ...history,
    { role: 'user', content: userText },
  ];
  let result = await requestPet(messages);
  if (!result.reply) {
    result = await requestPet([
      ...messages,
      { role: 'user', content: '(刚才没收到你的话,用一句不为空的话回我,记得只输出 JSON)' },
    ]);
  }
  if (!result.reply) {
    return { ok: false, reply: '(没太听清,再说一遍?)', emotion: 'neutral', remember: '', statChanges: {} };
  }
  return { ok: true, ...result };
}

// 把溢出的旧对话折叠进已有摘要
export async function summarize(prevSummary, overflowTurns) {
  const convo = overflowTurns
    .map((m) => `${m.role === 'user' ? '主人' : '宠物'}: ${m.content}`)
    .join('\n');
  const prompt = [
    '把下面这只电子宠物与主人的对话,合并进已有的长期记忆摘要里。',
    '保留重要的人物、喜好、约定、情感变化,去掉寒暄。用第三人称,150 字以内。',
    '',
    `【已有摘要】${prevSummary || '(暂无)'}`,
    '',
    `【新增对话】\n${convo}`,
  ].join('\n');
  const raw = await callDeepSeek([{ role: 'user', content: prompt }]);
  return raw.trim().slice(0, 600);
}
