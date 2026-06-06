import { DECAY_PER_HOUR, STAT_BOUNDS, PET } from './config.js';

const HOUR_MS = 60 * 60 * 1000;
const DAY_MS = 24 * HOUR_MS;
const DEFAULT_STATS = { mood: 70, hunger: 30, energy: 80, affection: 50 };

export function createPet(id, { name = PET.name, species = PET.species } = {}, now = Date.now()) {
  return {
    id,
    name,
    species,
    birthday: now,
    stats: { ...DEFAULT_STATS },
    lastInteraction: now,
    shortTerm: [], // [{ role, content }]
    summary: '', // 滚动长期摘要
    facts: [], // 关于主人的长期事实
  };
}

function clamp(n) {
  return Math.max(STAT_BOUNDS.min, Math.min(STAT_BOUNDS.max, Math.round(n)));
}

// 按流逝时间衰减状态,返回新对象(不可变)
export function withDecay(pet, now = Date.now()) {
  const hours = (now - pet.lastInteraction) / HOUR_MS;
  if (hours <= 0) return pet;
  const stats = { ...pet.stats };
  for (const [key, perHour] of Object.entries(DECAY_PER_HOUR)) {
    stats[key] = clamp((stats[key] ?? 50) + perHour * hours);
  }
  return { ...pet, stats };
}

// 应用模型自报的状态变化,返回新对象(不可变)
export function withStatChanges(pet, changes = {}) {
  const stats = { ...pet.stats };
  for (const [key, delta] of Object.entries(changes)) {
    if (key in stats && Number.isFinite(delta)) stats[key] = clamp(stats[key] + delta);
  }
  return { ...pet, stats };
}

// 给 prompt 用的人话状态描述
export function describeState(pet) {
  const { mood, hunger, energy, affection } = pet.stats;
  const ageDays = Math.floor((Date.now() - pet.birthday) / DAY_MS);
  return [
    `心情 ${mood}/100`,
    `饥饿 ${hunger}/100`,
    `精力 ${energy}/100`,
    `亲密度 ${affection}/100`,
    `已陪伴 ${ageDays} 天`,
  ].join('、');
}
