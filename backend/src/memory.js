import { LIMITS } from './config.js';
import { summarize } from './llm.js';

// 追加一轮对话,返回新 pet(摘要折叠在 maybeSummarize 里单独处理)
export function withTurn(pet, userText, petReply) {
  const shortTerm = [
    ...pet.shortTerm,
    { role: 'user', content: userText },
    { role: 'assistant', content: petReply },
  ];
  return { ...pet, shortTerm, lastInteraction: Date.now() };
}

// 记下一条关于主人的长期事实(去重 + 限量)
export function withFact(pet, fact) {
  const clean = String(fact || '').trim();
  if (!clean || pet.facts.includes(clean)) return pet;
  const facts = [...pet.facts, clean].slice(-LIMITS.maxFacts);
  return { ...pet, facts };
}

// 短期对话太长时,把最旧的几轮折叠进滚动摘要里
export async function maybeSummarize(pet) {
  if (pet.shortTerm.length <= LIMITS.summarizeWhenOver) return pet;
  const keep = LIMITS.shortTermMessages;
  const overflow = pet.shortTerm.slice(0, pet.shortTerm.length - keep);
  const recent = pet.shortTerm.slice(-keep);
  const summary = await summarize(pet.summary, overflow);
  return { ...pet, summary, shortTerm: recent };
}
