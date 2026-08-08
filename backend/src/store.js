import { readFile, writeFile, mkdir, rename } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DATA_DIR = join(dirname(fileURLToPath(import.meta.url)), '..', 'data');

// 只允许安全字符,避免路径穿越
function petPath(petId) {
  const safe = String(petId)
    .replace(/[^a-zA-Z0-9_-]/g, '')
    .slice(0, 64);
  if (!safe) throw new Error('invalid petId');
  return join(DATA_DIR, `${safe}.json`);
}

export async function loadPet(petId) {
  const path = petPath(petId);
  if (!existsSync(path)) return null;
  const raw = await readFile(path, 'utf8');
  return JSON.parse(raw);
}

export async function savePet(pet) {
  await mkdir(DATA_DIR, { recursive: true });
  const path = petPath(pet.id);
  const tmp = `${path}.tmp`;
  // 原子写:先写临时文件再重命名,避免写一半崩了把档案写坏
  await writeFile(tmp, JSON.stringify(pet, null, 2), 'utf8');
  await rename(tmp, path);
  return pet;
}

// 同一 petId 的 load→改→save 必须串行, 否则两个并发请求(比如 /chat 正在等
// LLM 回复时 /proactive 轮询插进来)会互相用旧数据覆盖对方的写入, 丢对话历史。
// 按 petId 分链: 不同宠物互不阻塞, 同一宠物排队执行。
const chains = new Map();

export function withPetLock(petId, fn) {
  const prev = chains.get(petId) || Promise.resolve();
  const next = prev.then(fn, fn); // 前一个失败也不能卡住后续请求
  const tail = next.catch(() => {}); // 占位在链上的是这个,不会因 fn 报错而中断链条
  chains.set(petId, tail);
  tail.finally(() => {
    if (chains.get(petId) === tail) chains.delete(petId); // 期间没有新请求排队时才清理,避免 Map 无限增长
  });
  return next; // 调用方仍能拿到 fn 的真实结果/异常
}
