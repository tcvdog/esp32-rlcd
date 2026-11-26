# v0.60.1 动作组合与分镜

## 普通连段

### A → A → A

```text
Jab       0.00–0.42s   命中 0.17s   14
Hook      0.00–0.62s   命中 0.24s   28
High Kick 0.00–0.72s   命中 0.28s   32
```

取消窗口：

```text
Jab  t=0.12–0.36  → Hook
Hook t=0.18–0.52  → High Kick
```

### 下段

```text
B            中段踢
↓ + B        低扫
↓ + 后 + B   后滚（0–0.34s 无敌）
```

### 空中

```text
↑             跳
空中 B        Air Kick（0.10s 命中，落地取消）
```

## 防御与反击

```text
后退（对方攻击中）  Guard
防御 + A            Counter（0.26s 内反击）
双击后              Evade（0.16s 无敌）
```

## 投技

```text
近身 C   Grab（0.10s 判定，不可防御）
         → Throw（受击者旋转浮空 0.55s）
```

## 特殊技 / 大招

```text
QCF + C/D         墨旋踢      1 气   突进
↓ + 后 + C/D      墨爆        2 气   范围击倒
冲刺中 C/D        墨影三连    1 气   3 hits
C + D             墨龙破      3 气   全屏大招收尾
```

## 推荐连段路线

```text
压制：Jab → Hook → High Kick
择下：Low Kick → Low Sweep
反击：Guard+A → Counter → Hook
投技：Grab → Throw
资源：普通拳脚攒气 → 墨爆/墨影三连 → 墨龙破
```

## 1bpp 关键帧

```text
Jab：收拳 → 直刺 → 回收
Hook：转肩 → 横摆 → 收势
High Kick：提膝 → 高扫 → 落地
Air Kick：收腿 → 斜下踢 → 落地
Roll：团身 → 旋转 → 起身
Counter：架招 → 弹反 → 反击
Grab/Throw：伸手 → 抓取 → 旋转投出
Ink Burst：蓄墨 → 放射墨环 → 收势
Shadow Rush：低姿 → 三连斜斩 → 收刀
Ink Dragon：蓄墨 → 冲刺 → 墨龙环 → 收势
```
