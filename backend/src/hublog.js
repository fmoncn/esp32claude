// 克劳德聊 Hub 数据: 每天记录一次 Dell Hub 的信息(指数行情 + Claude 额度 +
// 导航页各项目动态: news/yts/vsm 等), 存进克劳德记忆(facts), 聊天时作为谈资。
// 数据源: Dell Hub (localhost:4000), 后端同机可直连, 无鉴权。
import { loadPet, savePet } from './store.js';

const HUB = 'http://localhost:4000';

// 拉取 Dell Hub 的四指数行情。失败返回 null。
async function fetchIndices() {
  try {
    const r = await fetch(`${HUB}/api/indices`, { signal: AbortSignal.timeout(5000) });
    if (!r.ok) return null;
    return await r.json();  // [{name, price, pct}]
  } catch { return null; }
}

// 拉取 Claude 额度(来自 VPS 系统信息)。失败返回 null。
async function fetchQuota() {
  try {
    const r = await fetch(`${HUB}/api/sysinfo/vps`, { signal: AbortSignal.timeout(5000) });
    if (!r.ok) return null;
    const j = await r.json();
    return j.claude || null;  // { 5h:{pct}, 7d:{pct,reset_in_s} }
  } catch { return null; }
}

// 拉取导航页服务列表(项目名/描述)。失败返回 []。
async function fetchServices() {
  try {
    const r = await fetch(`${HUB}/api/services`, { signal: AbortSignal.timeout(5000) });
    if (!r.ok) return [];
    const s = await r.json();
    return Array.isArray(s) ? s : [];
  } catch { return []; }
}

// 拉取各项目最新动态(最近处理/推送/同步了什么)。失败返回 {}。
async function fetchPreviews() {
  try {
    const r = await fetch(`${HUB}/api/previews`, { signal: AbortSignal.timeout(5000) });
    if (!r.ok) return {};
    return await r.json();
  } catch { return {}; }
}

// 生成当天 hub 摘要文本(指数 + 额度 + 项目动态)。
function formatSummary(indices, quota, services, previews) {
  const parts = [];
  if (Array.isArray(indices)) {
    const moves = indices.map((i) => `${i.name}${i.pct >= 0 ? '+' : ''}${i.pct?.toFixed?.(2) ?? i.pct}%`).join(' ');
    parts.push(`四指数:${moves}`);
  }
  if (quota && quota['7d']) {
    parts.push(`Claude 7天额度已用${quota['7d'].pct}%`);
  }
  // 项目动态: 从 previews 提取各项目最近一条活动(如 yts 同步了"龙之家族第三季")
  const nameMap = {};
  if (Array.isArray(services)) {
    for (const s of services) if (s && s.id) nameMap[s.id] = s.name || s.id;
  }
  const projBits = [];
  if (previews && typeof previews === 'object') {
    for (const [id, list] of Object.entries(previews)) {
      if (!Array.isArray(list) || list.length === 0) continue;
      const first = list[0] || {};
      const t = first.t || '';
      if (!t) continue;
      const label = nameMap[id] || id;
      projBits.push(`${label}:${t}`);
    }
  }
  if (projBits.length) parts.push(`项目:${projBits.join('; ')}`);
  if (parts.length === 0) return null;
  const today = new Date();
  const date = `${today.getMonth() + 1}/${today.getDate()}`;
  return `[${date} hub] ${parts.join('; ')}`;
}

// 每天记录一次 hub 数据到克劳德记忆(每天第一条为准, 不重复刷)。
// 返回新增的 hub 摘要文本(今天第一次记录时), 否则 null。
export async function recordDailyHub(petId) {
  const pet = (await loadPet(petId));
  if (!pet) return null;
  const today = `${new Date().getMonth() + 1}/${new Date().getDate()}`;
  const tag = `[${today} hub]`;
  // 今天已记录过(记忆里有今天的 hub 条目) → 不重复
  if (Array.isArray(pet.facts) && pet.facts.some((f) => String(f).includes(tag))) return null;

  const [indices, quota, services, previews] = await Promise.all([
    fetchIndices(), fetchQuota(), fetchServices(), fetchPreviews(),
  ]);
  const summary = formatSummary(indices, quota, services, previews);
  if (!summary) return null;

  pet.facts = Array.isArray(pet.facts) ? pet.facts : [];
  pet.facts.push(summary);
  if (pet.facts.length > 30) pet.facts = pet.facts.slice(-30);
  await savePet(pet);
  return summary;
}

// 取克劳德记忆里最近一条 hub 摘要, 供主动对话聊起。无则返回 null。
export function recentHubFact(pet) {
  if (!Array.isArray(pet.facts)) return null;
  for (let i = pet.facts.length - 1; i >= 0; i--) {
    const f = String(pet.facts[i] || '');
    if (f.includes('hub]')) return f;  // 摘要形如 "[日期 hub] 四指数:..."
  }
  return null;
}
