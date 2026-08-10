// 切场景主动: 用户切换场景时, 克劳德根据该场景左上角 hub 卡片的数据,
// 用 LLM 生成一句自然的话(像看到场景里显示的行情/额度随口聊起)。
// 数据源: Dell Hub (localhost:4000), 后端同机可直连。
import { LIMITS } from './config.js';
import { buildSystemPrompt } from './persona.js';
import { chatAsPet } from './llm.js';
import { loadPet } from './store.js';

const HUB = 'http://localhost:4000';

// 拉 Dell Hub JSON。失败返回 null。
async function hubGet(path) {
  try {
    const r = await fetch(`${HUB}${path}`, { signal: AbortSignal.timeout(5000) });
    if (!r.ok) return null;
    return await r.json();
  } catch { return null; }
}

// 场景号 → 数据提示文本(给 LLM 参考)。返回 null 表示无场景数据可聊(切场景静默不聊)。
async function sceneData(idx) {
  switch (idx % 10) {
    case 1: {  // 客厅 → Claude 额度
      const vps = await hubGet('/api/sysinfo/vps');
      const c = vps?.claude;
      if (c && c['7d']) return `Claude 额度: 5小时窗口已用 ${c['5h']?.pct ?? '?'}%, 7天窗口已用 ${c['7d'].pct}%`;
      return null;  // 无额度数据 → 不聊
    }
    case 2: case 3: case 4: case 5: {  // 指数场景
      const indices = await hubGet('/api/indices');
      const names = ['标普500', '纳指100', '上证指数', '恒生指数'];
      const idx2 = { 2: 0, 3: 1, 4: 2, 5: 3 }[idx % 10];
      const it = Array.isArray(indices) ? indices[idx2] : null;
      if (it) return `${names[idx2]} 现价 ${it.price}, 今日 ${it.pct >= 0 ? '+' : ''}${it.pct}%`;
      return null;  // 行情暂无 → 不聊
    }
    default:
      return null;  // 无 hub 数据(景色/家常等) → 切场景静默, 不聊废话
  }
}

// 切场景主动: 生成一句克劳德的话。仅当该场景有真实 hub 数据时聊, 否则静默。
// 返回 { message } 或 { has:false }。
export async function sceneGreet(petId, sceneIdx) {
  try {
    const pet = (await loadPet(petId));
    if (!pet) return { has: false };
    const data = await sceneData(sceneIdx);
    if (!data) return { has: false };   // 无真实数据 → 不聊(避免"景色很好"废话)
    const system = buildSystemPrompt(pet);
    const user = `用户刚切换到了一个新场景(场景号 ${sceneIdx})。\n这个场景对应的数据: ${data}\n克劳德像看到就随口聊一句(温柔自然,别念数据,别堆砌数字)。`;
    const r = await chatAsPet(
      `${system}\n\n你看到主人切换了场景,像朋友一样随口说一句。`,
      [],
      `${user}\n(只说 1 句话,${LIMITS.replyMaxChars} 字内)`
    );
    return { has: true, message: (r.reply || '').trim().slice(0, LIMITS.replyMaxChars), emotion: 'neutral' };
  } catch (e) {
    return { has: false };
  }
}
