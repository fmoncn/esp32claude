import dotenv from 'dotenv';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

// 从 backend/.env 读取,无论从哪个目录启动 node 都能找到
dotenv.config({ path: join(dirname(fileURLToPath(import.meta.url)), '..', '.env') });

export const config = {
  port: Number(process.env.PORT) || 8787,
  petToken: process.env.PET_TOKEN || '',
  deepseek: {
    apiKey: process.env.DEEPSEEK_API_KEY || '',
    baseUrl: 'https://api.deepseek.com/chat/completions',
    model: 'deepseek-chat',
    temperature: 0.8,
    timeoutMs: 30000,
  },
  // 语音:STT + TTS 都走 DashScope(阿里云百炼),同一 key/endpoint
  dashscope: {
    apiKey: process.env.DASHSCOPE_API_KEY || '',
    endpoint: 'https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation',
    asrModel: 'qwen3-asr-flash',
    ttsModel: 'qwen3-tts-flash',
    ttsVoice: process.env.PET_VOICE || 'Ethan',
    ttsSampleRate: 24000,
  },
};

// 宠物身份与主人称呼(persona 用;均可在 .env 覆盖)
export const PET = {
  name: process.env.PET_NAME || '小豆丁',
  species: process.env.PET_SPECIES || '掌上 AI 伙伴',
  ownerName: process.env.PET_OWNER_NAME || '主人',
  ownerTitle: process.env.PET_OWNER_TITLE || '主人',
};

// 记忆 / 状态相关的可调旋钮(避免魔法数字散落在逻辑里)
export const LIMITS = {
  shortTermMessages: 12, // 保留最近 N 条原始消息(≈6 轮)
  summarizeWhenOver: 16, // 超过这个长度就把旧对话折叠进长期摘要
  maxFacts: 30, // 长期事实条数上限
  replyMaxChars: 320, // 回复长度上限,适配小屏
  userTextMaxChars: 500, // 单次输入上限
};

// 每"被冷落"一小时的状态衰减(0..100 标度)
export const DECAY_PER_HOUR = {
  hunger: +6, // 越来越饿
  energy: -4, // 越来越困
  mood: -3, // 心情慢慢变差
  affection: -1, // 久不见会生疏
};

export const STAT_BOUNDS = { min: 0, max: 100 };
