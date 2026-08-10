// 克劳德聊 paseo News: 读取 Dell 上 paseo 生成的 News 文件(关注推文 + 书签文章),
// 挑一条有料的话题, 让克劳德在主动对话/开机时聊起(替换原来的 hub 数据)。
// 数据源: /mnt/extdata/paseo/News/ 下的 <date>-following.md 和 <date>-bookmarks.md
import { readdir, readFile } from 'node:fs/promises';

const NEWS_DIR = '/mnt/extdata/paseo/News';

// 找目录里最新的匹配文件(如 2026-08-11-following.md)。无则 null。
async function latestFile(kind) {
  try {
    const files = await readdir(NEWS_DIR);
    const m = files
      .filter((f) => f.endsWith(`-${kind}.md`))
      .sort()
      .pop();   // 名字按日期排序, 取最新的
    return m ? `${NEWS_DIR}/${m}` : null;
  } catch { return null; }
}

// 从 following.md 解析关注推文 → [{author, text, hot}]
function parseFollowing(md) {
  const out = [];
  const blocks = md.split(/\n(?=\*\*@)/);   // 每条推文从 "**@作者" 开始
  for (const b of blocks) {
    const am = b.match(/^\*\*(?:🔥 )?@([\w]+)\*\*/);   // 作者
    if (!am) continue;
    const lines = b.split('\n');
    const text = lines.slice(1).map((l) => l.trim()).find((l) => l && !l.startsWith('_'));
    if (!text) continue;
    // 热度: 从 "_【X 评论 / Y 浏览】_" 提取
    const hm = b.match(/(\d+)\s*评论/);
    out.push({ author: am[1], text: text.slice(0, 120), hot: hm ? Number(hm[1]) : 0 });
  }
  return out;
}

// 从 bookmarks.md 解析书签 → [{author, title}]。只在 "逐条分析" 之后解析, 跳过总览。
function parseBookmarks(md) {
  const out = [];
  const analysisIdx = md.indexOf('## 逐条分析');
  if (analysisIdx < 0) return out;
  const body = md.slice(analysisIdx);
  const blocks = body.split(/\n(?=### )/);   // 每条书签从 "### N."
  for (const b of blocks) {
    const tm = b.match(/@([\w]+)\)/);       // 作者 handle
    const lines = b.split('\n');
    // 标题 = 互动行之后的第一个非空内容行
    let title = null;
    for (let i = 0; i < lines.length; i++) {
      if (lines[i].startsWith('💬') || lines[i].startsWith('🔗')) continue;
      const t = lines[i].trim();
      if (t && !t.startsWith('**') && !t.startsWith('#') && !t.startsWith('>') && !t.startsWith('|') && !t.startsWith('---') && !t.startsWith('*')) {
        title = t; break;
      }
    }
    if (!title) continue;
    out.push({ author: tm ? tm[1] : '', title: title.slice(0, 100) });
  }
  return out;
}

// 读取最新 News 并解析。返回 { following:[], bookmarks:[] }。
export async function readNews() {
  const [fp, bp] = await Promise.all([latestFile('following'), latestFile('bookmarks')]);
  const [f, b] = await Promise.all([fp ? readFile(fp, 'utf8') : '', bp ? readFile(bp, 'utf8') : '']);
  return { following: f ? parseFollowing(f) : [], bookmarks: b ? parseBookmarks(b) : [] };
}

// 挑一条适合克劳德主动聊的 news 话题(优先热度高的推文, 或书签标题)。
// 返回可读的一句话, 或 null(无内容)。
export async function pickNewsTopic() {
  const { following, bookmarks } = await readNews();
  // 优先: 关注推文里热度高且有实质内容的
  const interesting = following.filter((t) => t.text.length > 10 && (t.hot >= 3 || following.length <= 5));
  let pick = null;
  if (interesting.length) {
    pick = interesting[Math.floor(Math.random() * interesting.length)];
    return `关注里 @${pick.author} 说: ${pick.text}`;
  }
  if (bookmarks.length) {
    const bm = bookmarks[Math.floor(Math.random() * bookmarks.length)];
    return `你收藏了一篇文章「${bm.title}」${bm.author ? `(by @${bm.author})` : ''}`;
  }
  return null;
}
