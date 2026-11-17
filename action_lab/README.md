# Action Lab（动作设计台）

独立于 ESP32 固件的 2D 动作设计/演示/评估工具。

## 打开

```bash
cd /mnt/documents/桌面/web游戏/esp32-rlcd/action_lab
python3 -m http.server 8770
```

浏览器打开 <http://127.0.0.1:8770/>。

## 功能

- 骨架动画播放，沿用固件 `Pose` 角度约定
- 时间轴、速度、镜像、关节、残影
- 自动切片：按角速度峰值 + 旋转相位拆成预备/主动作/回收
- 评估：时长、根位移、最大角速度、角加速度、首尾连续性
- 动作 JSON 数据在 `actions/ink_cartwheel.json`

## 重要说明

“漂亮”不是单一指标。评估器只能过滤明显不合理项；最终需要参考 GIF/视频和人工挑选。

## 下一步

- 从 GIF 逐帧提取 2D 关键点（MediaPipe/OpenPose）→ 角度 → 重定向到骨架
- 自动生成候选关键帧，按平滑度、支撑、脚滑、可读性筛查
- 人工挑选后再写入固件 `inkPose()`
