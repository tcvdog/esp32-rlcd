# 背诵行为日志与不扎实点分析

## 日志
- 文件：`/sdcard/cloze_events.jsonl`
- 电脑：`http://<板子IP>/cloze_events.jsonl`
- 每行一个事件，JSONL 可直接给 AI/分析脚本

字段：
```text
session, seq, clock, uptime_ms, event, button,
group, title, level, page, tokens, original, masked
```

事件：
```text
session / enter / prev / next / level_up / level_down
page_prev / page_next / reveal_on / reveal_off
reveal_early / reveal_denied / exit
```

## 提示预算
- 每篇进入时 3 次显示原文额度
- 进入/换页后至少 3 秒才能显示
- 额度用尽显示“提示0”，K3 不再显示
- 降级也会被记录为 hint，不会被当成独立回忆

## 不扎实点候选
- reveal_on 次数高
- reveal_early 次数高
- level_down 后立刻 reveal_on
- 同一篇多次进入但没到 L5
- 停留时间过短
- 页面跳读、反复切篇
- 高 reveal 但完成图仍为空心

## AI 分析提示
```text
你是学习行为分析师。输入是 cloze_events.jsonl。
不要评价人格，只分析学习行为风险。
逐篇输出：
- 是否依赖看原文
- 是否通过降级/提前显示获得提示
- 是否存在跳页、快速退出、虚假完成
- 最可能不扎实的篇目/段落
- 建议复习级别与下次复习时间
```
