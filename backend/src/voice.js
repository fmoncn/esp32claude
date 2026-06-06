import { config } from './config.js';

const DS = config.dashscope;

function headers(extra = {}) {
  return { 'content-type': 'application/json', authorization: `Bearer ${DS.apiKey}`, ...extra };
}

// 语音 → 文字(Qwen3-ASR-Flash,同步,base64 内联音频)
export async function transcribe(audioBuffer, mime = 'audio/wav') {
  if (!DS.apiKey) throw new Error('DASHSCOPE_API_KEY 未配置,请在 backend/.env 里填上');
  const b64 = audioBuffer.toString('base64');
  const res = await fetch(DS.endpoint, {
    method: 'POST',
    headers: headers(),
    body: JSON.stringify({
      model: DS.asrModel,
      input: { messages: [{ role: 'user', content: [{ audio: `data:${mime};base64,${b64}` }] }] },
      parameters: { asr_options: { language: 'zh', enable_itn: true } },
    }),
  });
  if (!res.ok) throw new Error(`ASR ${res.status}: ${(await res.text().catch(() => '')).slice(0, 200)}`);
  const data = await res.json();
  const text = data?.output?.choices?.[0]?.message?.content?.[0]?.text ?? '';
  return String(text).trim();
}

// 文字 → 语音(Qwen-TTS,SSE 流式;拼成一整段 24kHz/mono/int16 裸 PCM)
export async function synthesize(text, voice = DS.ttsVoice) {
  if (!DS.apiKey) throw new Error('DASHSCOPE_API_KEY 未配置,请在 backend/.env 里填上');
  const res = await fetch(DS.endpoint, {
    method: 'POST',
    headers: headers({ 'x-dashscope-sse': 'enable' }),
    body: JSON.stringify({ model: DS.ttsModel, input: { text, voice } }),
  });
  if (!res.ok || !res.body) {
    throw new Error(`TTS ${res.status}: ${(await res.text().catch(() => '')).slice(0, 200)}`);
  }

  const reader = res.body.getReader();
  const dec = new TextDecoder();
  const chunks = [];
  let buf = '';
  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    buf += dec.decode(value, { stream: true });
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if (!line.startsWith('data:')) continue;
      const payload = line.slice(5).trim();
      if (!payload || payload === '[DONE]') continue;
      let evt;
      try {
        evt = JSON.parse(payload);
      } catch {
        continue;
      }
      const b64 = evt?.output?.audio?.data;
      if (b64) chunks.push(Buffer.from(b64, 'base64'));
    }
  }
  return Buffer.concat(chunks); // 裸 int16 PCM @ 24kHz mono
}
