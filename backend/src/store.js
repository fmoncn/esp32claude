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
