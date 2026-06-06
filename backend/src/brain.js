import { LIMITS } from './config.js';
import { loadPet, savePet } from './store.js';
import { createPet, withDecay, withStatChanges } from './pet.js';
import { buildSystemPrompt } from './persona.js';
import { chatAsPet } from './llm.js';
import { withTurn, withFact, maybeSummarize } from './memory.js';

// 文字进、宠物回复出 —— /chat 和 /voice 共用这一套大脑 + 记忆
export async function respondToPet(petId, userText) {
  const trimmed = String(userText).trim().slice(0, LIMITS.userTextMaxChars);

  let pet = (await loadPet(petId)) || createPet(petId);
  pet = withDecay(pet);

  const system = buildSystemPrompt(pet);
  const result = await chatAsPet(system, pet.shortTerm, trimmed);
  const reply = result.reply.slice(0, LIMITS.replyMaxChars);

  // 只把"成功的真回复"写进记忆,避免空回复污染历史
  if (result.ok) {
    pet = withStatChanges(pet, result.statChanges);
    pet = withTurn(pet, trimmed, reply);
    if (result.remember) pet = withFact(pet, result.remember);
    pet = await maybeSummarize(pet);
    await savePet(pet);
  }

  return { reply, emotion: result.emotion, name: pet.name, stats: pet.stats, ok: result.ok };
}
