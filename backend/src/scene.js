// 切场景主动: 每切换一次场景, 克劳德随机聊一条今天 paseo News 的关注者推文(不重复),
// 不再聊 hub 数据。数据源: paseo News 文件(关注推文 following.md)。
import { LIMITS } from './config.js';
import { buildSystemPrompt } from './persona.js';
import { chatAsPet } from './llm.js';
import { loadPet, savePet } from './store.js';
import { readNews } from './newslog.js';

const SEEN_KEY = 'sceneNewsSeen';   // pet 里记录已聊过的推文(不重复)

// 挑一条今天关注者推文, 排除已聊过的。返回 { author, text } 或 null。
async function pickFreshNews(pet) {
  const { following } = await readNews();
  if (!following.length) return null;
  // 已聊过(按 author:前10字指纹去重, 避免重复聊同一条)
  const seen = new Set(Array.isArray(pet[SEEN_KEY]) ? pet[SEEN_KEY] : []);
  const fresh = following.filter((t) => !seen.has(`${t.author}:${t.text.slice(0, 12)}`));
  const pool = fresh.length ? fresh : following;   // 全聊过则重头
  if (!pool.length) return null;
  const pick = pool[Math.floor(Math.random() * pool.length)];
  return { author: pick.author, text: pick.text };
}

// 切场景主动: 生成一句克劳德的话, 聊一条今天的关注者新闻(随机不重复)。
// 返回 { message } 或 { has:false }。
export async function sceneGreet(petId, sceneIdx) {
  try {
    const pet = (await loadPet(petId));
    if (!pet) return { has: false };
    const news = await pickFreshNews(pet);
    if (!news) return { has: false };   // 无新闻 → 不聊
    const system = buildSystemPrompt(pet);
    const user = `用户刚切换了一个场景(场景号 ${sceneIdx})。\n你刷到关注里 @${news.author} 说: ${news.text}\n像朋友随口聊聊这条新闻,给点你的看法或好奇心。`;
    const r = await chatAsPet(
      `${system}\n\n你看到主人切换了场景,像朋友一样随口聊一条刚刷到的关注新闻。`,
      [],
      `${user}\n(只说 1 句话,${LIMITS.replyMaxChars} 字内)`
    );
    // 记录这条已聊过(不重复)
    const key = `${news.author}:${news.text.slice(0, 12)}`;
    pet[SEEN_KEY] = Array.isArray(pet[SEEN_KEY]) ? pet[SEEN_KEY] : [];
    if (!pet[SEEN_KEY].includes(key)) {
      pet[SEEN_KEY].push(key);
      if (pet[SEEN_KEY].length > 50) pet[SEEN_KEY] = pet[SEEN_KEY].slice(-50);
      await savePet(pet);
    }
    return { has: true, message: (r.reply || '').trim().slice(0, LIMITS.replyMaxChars), emotion: 'neutral' };
  } catch (e) {
    return { has: false };
  }
}
