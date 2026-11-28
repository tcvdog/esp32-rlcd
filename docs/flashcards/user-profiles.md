# 三用户进度隔离（v0.70.4）

## 用户
- 测试（user 0，继承旧进度）
- 淘淘（user 1，从零开始）
- 乐乐（user 2，从零开始）

开机进入 `SYS // USER` 选择页，摇杆上下选择，K3 进入。

## 隔离数据
- cloze_progress：
  - user 0: `cloze_p`
  - user 1: `cloze_p1`
  - user 2: `cloze_p2`
- cloze_stats：
  - user 0: `cloze`
  - user 1: `cloze1`
  - user 2: `cloze2`
- CSSV：
  - user 0: `/sdcard/cloze_stats.csv`
  - user 1: `/sdcard/cloze_stats_u1.csv`
  - user 2: `/sdcard/cloze_stats_u2.csv`
- 事件：
  - user 0: `/sdcard/cloze_events.jsonl`
  - user 1: `/sdcard/cloze_events_u1.jsonl`
  - user 2: `/sdcard/cloze_events_u2.jsonl`
