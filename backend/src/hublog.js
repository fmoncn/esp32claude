// 克劳德聊 Hub 数据: 每天记录一次右上角 hub 数据(指数行情 + Claude 额度),
// 存进克劳德记忆(facts), 主动对话时偶尔聊起行情/额度变化。
// 数据源: Dell Hub (localhost:4000), 后端同机可直连, 无鉴权。
import { loadPet, savePet } from './store.js';

const HUB = 'http://localhost:4000';
const DATA_KEYS = ['5h', '7d'];  // Claude 额度窗口

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

// 生成当天 hub 摘要文本(供克劳德记忆/聊天)。
function formatSummary(indices, quota) {
  const parts = [];
  if (Array.isArray(indices)) {
    const moves = indices.map((i) => `${i.name}${i.pct >= 0 ? '+' : ''}${i.pct?.toFixed?.(2) ?? i.pct}%`).join(' ');
    parts.push(`四指数:${moves}`);
  }
  if (quota && quota['7d']) {
    parts.push(`Claude 7天额度剩${quota['7d'].pct}%`);
  }
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

  const [indices, quota] = await Promise.all([fetchIndices(), fetchQuota()]);
  const summary = formatSummary(indices, quota);
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
