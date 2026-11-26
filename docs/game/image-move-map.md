# 参考图 → 游戏动作映射

来源：用户提供的火柴人动作总览图。只参考动作结构，不复制图像素材。

| 图中分类 | 图中动作 | 游戏状态 | 输入 |
|---|---:|---:|---|
| Basic Actions | Walking | 1 | 方向键 |
| Basic Actions | Running | 22 | 持续前方向 |
| Basic Actions | Jumping Kick | 13 | ↑ 后 B |
| Top Row / Defense | Blocking | 5 | 后退防御 |
| Basic Attacks | Jab | 3 | A |
| Basic Attacks | Hook | 7 | C |
| Basic Attacks | Low Kick | 4 | B |
| Combo Attacks | Punch | 3 | A |
| Combo Attacks | Punch | 23 Cross | A（第一拳取消窗口） |
| Combo Attacks | High Kick | 8 | A（第二拳取消窗口） |
| Dodging | Rolling | 14 | ↓ + 后 + B |
| Dodging | Evading | 12 | 双击后 |
| Counter-Attack | Counter-Attack | 15 | 防御 + A |
| Counter-Attack | Grab | 16 | 近身 C |
| Counter-Attack | Throw | 17 / 21 | Grab 命中后自动 |
| Ultimate & Special | 墨旋踢 | 6 | QCF + C/D |
| Ultimate & Special | 墨爆 | 18 | ↓ + 后 + C/D |
| Ultimate & Special | 墨影三连 | 19 | 冲刺中 C/D |
| Ultimate & Special | 墨龙破 | 20 | C + D（3 气） |

## 与上一版的修正

- 三连击第二拳从 `Hook(7)` 改为独立的 `Cross(23)`，对应图中的 Punch → Punch。
- 新增 Running(22)，不再只有 Walking。
- 新增 Jumping Kick 的落地取消。
- 新增 Evading 后撤无敌帧。
- Grab 命中后受击者进入独立的 Throw(21) 旋转受身，而不是普通硬直。
- 新增命中文字：`KA-POW!`、`SLASH!`、`BURST!`、`INK DRAGON`、`HIT!`。
- 墨影三连与墨龙破改为多段命中：各 3 hits。
