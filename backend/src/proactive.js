import { LIMITS, PET } from './config.js';
import { loadPet, savePet } from './store.js';
import { createPet, withDecay } from './pet.js';
import { buildSystemPrompt } from './persona.js';
import { chatAsPet } from './llm.js';

// 主动对话: 克劳德根据空闲时长 + 聊天记忆主动找主人说话。
// 频率控制: 距上次交互 > IDLE_MIN 分钟 + 距上次主动 > COOLDOWN_MIN 分钟。
// 智能逻辑全在后端, 固件只轮询拉取 → 不占设备内存。
const IDLE_MIN = 15;        // 主人空闲多久才考虑主动
const COOLDOWN_MIN = 30;    // 每次主动后冷却多久,避免频繁打扰
const MAX_FACTS = 5;        // 挑几个记忆话题给模型参考

// 时段问候模板(LLM 失败时兜底,省一次调用)
function timeGreeting(hour) {
  if (hour < 5) return '夜深了还没睡吗?想不想聊聊白天的事?';
  if (hour < 11) return '早上好~今天有什么打算?';
  if (hour < 14) return '中午啦,吃饭没?趁空聊两句?';
  if (hour < 18) return '下午好,在忙什么?需要我搭把手吗?';
  return '晚上好~今天过得怎么样?';
}

// 判断是否该主动 + 生成一句主动话。返回 { has:false } 表示此刻不打扰。
export async function proactiveMessage(petId) {
  const now = Date.now();
  let pet = (await loadPet(petId)) || createPet(petId);
  pet = withDecay(pet, now);

  // 频率控制: 空闲时长 + 冷却
  const idleMin = (now - (pet.lastInteraction || now)) / 60000;
  if (idleMin < IDLE_MIN) return { has: false };          // 主人刚互动过,不打扰
  if (now - (pet.lastProactive || 0) < COOLDOWN_MIN * 60000) return { has: false };  // 冷却中

  // 尝试用 LLM 从记忆挑话题(克劳德更贴合);失败回退时段问候
  let text = '';
  const topics = (pet.facts || []).slice(-MAX_FACTS);
  try {
    const system = buildSystemPrompt(pet);
    const hint = topics.length
      ? `【你记得主人这些事,挑一件自然提起】\n${topics.map((f) => `- ${f}`).join('\n')}`
      : '主人今天还没怎么聊,自然地打个招呼、起个轻松话题。';
    const r = await chatAsPet(
      `${system}\n\n你正在主动找主人说话(主人空闲中)。请像关心朋友一样主动开口。`,
      pet.shortTerm.slice(-4),
      `${hint}\n(主动发起一句 1 句话,温柔自然地关心/挑个话题,不要问句堆砌,${LIMITS.replyMaxChars} 字内)`
    );
    text = r.reply;
  } catch (e) {
    text = '';
  }
  if (!text.trim()) text = timeGreeting(new Date().getHours());

  // 记录主动时间,控制下次频率
  pet.lastProactive = now;
  await savePet(pet);

  return { has: true, message: text.trim().slice(0, LIMITS.replyMaxChars), emotion: 'thinking' };
}
