import { LIMITS } from './config.js';
import { loadPet, savePet, withPetLock } from './store.js';
import { createPet, withDecay, withStatChanges, publicStats } from './pet.js';
import { buildSystemPrompt } from './persona.js';
import { chatAsPet } from './llm.js';
import { withTurn, withFact, maybeSummarize } from './memory.js';

// 文字进、宠物回复出 —— /chat 和 /voice 共用这一套大脑 + 记忆
// 整个 load→改→save 包在 withPetLock 里: LLM 调用可能长达 60s, 期间 /proactive
// 轮询若插进来读写同一份档案, 不加锁的话谁后写谁赢, 会把这轮对话记忆覆盖掉。
export async function respondToPet(petId, userText) {
  const trimmed = String(userText).trim().slice(0, LIMITS.userTextMaxChars);

  return withPetLock(petId, async () => {
    let pet = (await loadPet(petId)) || createPet(petId);
    pet = withDecay(pet);

    const system = buildSystemPrompt(pet);
    const result = await chatAsPet(system, pet.shortTerm, trimmed);
    const reply = result.reply.slice(0, LIMITS.replyMaxChars)
      .replace(/\s*\n\s*/g, ' ').trim();  // 紧凑成一段:换行/分段替换成空格

    // 只把"成功的真回复"写进记忆,避免空回复污染历史
    if (result.ok) {
      pet = withStatChanges(pet, result.statChanges);
      pet = withTurn(pet, trimmed, reply);
      if (result.remember) pet = withFact(pet, result.remember);
      pet = await maybeSummarize(pet);
      await savePet(pet);
    }

    return { reply, emotion: result.emotion, name: pet.name, stats: publicStats(pet), ok: result.ok, suggestions: result.suggestions || [] };
  });
}
