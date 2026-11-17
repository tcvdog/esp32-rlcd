# 设计说明

## 屏幕

- 400×300，1bpp，横向
- 顶部状态栏保留
- 主区域：坐标轴 + 轨迹
- 底部：参数、读数、操作提示

## 物理

平抛：

```text
x(t) = v0 * t
y(t) = h - 0.5 * g * t²
vx = v0
vy = -g * t
```

球落地后停在 `y=0`，保留轨迹。

## 任务隔离

- `physics_lab` 不直接调用游戏 Fighter
- 不占用音频 codec
- 不创建高优先级任务
- 每帧由 gameTask 调用 `physicsUpdate(dt)` / `physicsDraw()`
- 物理模式退出时停表、释放临时轨迹
