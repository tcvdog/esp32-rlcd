# 三用户进度隔离（v0.70.5）

## 用户
- 测试（user 0，保留旧进度）
- 淘淘（user 1，首次升级清空）
- 乐乐（user 2，首次升级清空）

## 切换
主界面：K4 USER → 用户选择
用户选择：K4 CANCEL → 主界面
选择：摇杆上下，K3 ENTER

## 顶部
主界面标题行显示当前用户名。
用户选择页显示“当前测试/淘淘/乐乐”。

## 隔离数据
- NVS：cloze_p（user0）/ cloze_p1 / cloze_p2
- 统计：cloze / cloze1 / cloze2
- CSV：cloze_stats.csv / _u1 / _u2
- 事件：cloze_events.jsonl / _u1 / _u2
