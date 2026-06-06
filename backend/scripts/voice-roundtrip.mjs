// 语音自测:TTS 合成一句话 → 存 WAV → 再喂给 STT 回识。无需录音即可验证两头。
import { writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { synthesize, transcribe } from '../src/voice.js';

const here = dirname(fileURLToPath(import.meta.url));

function wavFromPcm(pcm, rate = 24000, ch = 1, bits = 16) {
  const blockAlign = (ch * bits) / 8;
  const h = Buffer.alloc(44);
  h.write('RIFF', 0);
  h.writeUInt32LE(36 + pcm.length, 4);
  h.write('WAVE', 8);
  h.write('fmt ', 12);
  h.writeUInt32LE(16, 16);
  h.writeUInt16LE(1, 20);
  h.writeUInt16LE(ch, 22);
  h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(rate * blockAlign, 28);
  h.writeUInt16LE(blockAlign, 32);
  h.writeUInt16LE(bits, 34);
  h.write('data', 36);
  h.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([h, pcm]);
}

const phrase = '你好呀,今天有点累,陪我聊会儿吧';
try {
  console.log('1) TTS 合成 →', phrase);
  const pcm = await synthesize(phrase);
  console.log(`   PCM ${pcm.length} 字节 (~${(pcm.length / 2 / 24000).toFixed(1)}s @24kHz)`);
  const wav = wavFromPcm(pcm);
  const wavPath = join(here, '_tts_out.wav');
  await writeFile(wavPath, wav);
  console.log('   已存', wavPath);

  console.log('2) STT 回识 ...');
  const heard = await transcribe(wav, 'audio/wav');
  console.log('   听到 →', heard);
  console.log(pcm.length > 0 && heard ? '\n✅ 语音链路通(TTS + STT 都好)' : '\n⚠️ 有空结果,看上面');
} catch (e) {
  console.error('❌ 失败:', e.message);
  process.exit(1);
}
