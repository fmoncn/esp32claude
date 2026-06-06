# Cardpet 后端(宠物大脑 + 记忆)

Cardputer AI 电子宠物的"大脑":接收设备发来的消息 → 载入记忆 → 调 DeepSeek → 更新记忆 → 返回带情绪的回复。

## 跑起来

```bash
cd cardpet/backend
npm install
cp .env.example .env        # 然后把 DEEPSEEK_API_KEY 填进去
npm start                   # 或 npm run dev (改代码自动重启)
```

要求 Node ≥ 18(用到全局 fetch)。DeepSeek key 在 https://platform.deepseek.com 申请。

## 自测(设备还没到也能玩)

```bash
# 健康检查
curl http://localhost:8787/health

# 跟宠物说话(同一个 petId 会持续积累记忆)
curl -s http://localhost:8787/chat \
  -H 'content-type: application/json' \
  -d '{"petId":"dev","message":"你好呀,我叫华军"}'

# 看看它记住了什么
curl http://localhost:8787/pet/dev
```

## API 约定(固件按这个发)

| 方法 | 路径        | 入参                          | 返回                                              |
|------|-------------|-------------------------------|---------------------------------------------------|
| GET  | `/health`   | —                             | `{ ok, model, hasKey }`                           |
| POST | `/chat`     | `{ petId, message }`          | `{ reply, emotion, name, stats }`                 |
| GET  | `/pet/:id`  | —                             | `{ name, stats, state, facts }`                   |

- `emotion` ∈ `happy / sad / hungry / sleepy / excited / angry / love / neutral` —— 固件据此选脸。
- `stats` = `{ mood, hunger, energy, affection }`,各 0~100。
- 若设了 `PET_TOKEN`,固件需带请求头 `x-pet-token: <值>`。

## 记忆怎么存的

每只宠物一份 `data/<petId>.json`,含三层记忆:

1. **短期** `shortTerm` —— 最近 ~6 轮原始对话
2. **长期** `summary` + `facts` —— 滚动摘要 + 关于主人的关键事实(模型自己决定记什么)
3. **状态** `stats` —— 心情/饥饿/精力/亲密度,随时间衰减,反过来影响语气

调参都在 `src/config.js`(`LIMITS` / `DECAY_PER_HOUR`)。
