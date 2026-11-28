# UI 子页面同步自检（v0.64.0）

## 修复
- 渲染分发链被底栏键帽代码截断，`MODE_INFO / MODE_SETTINGS_CTRL / MODE_WIFI` 无法进入。
- 设置调节页每帧调用 `settings_save()`，改为只在参数变化时写 NVS。
- 子页面板改为 x=2 外框，正文 x=8 内缩，避免边框压字。
- 移除子页旧 `SW: BACK` / `A: TEST` 等重复提示，统一由底栏键帽显示。

## 已迁移子页
```text
CONFIG // OUTPUT     音量/亮度滑杆
CONFIG // INFO       4 页系统信息
CONFIG // HELP       操作帮助
NETWORK              Wi-Fi 扫描/密码
SURVEY              问卷/报告
VOICE               录音/反馈
```

## 键帽
```text
OUTPUT: JOY ADJ  K3 TEST  K4 RESET  SW BACK
INFO:   JOY PAGE  K4 BACK  SW BACK
WIFI:   JOY SELECT  K3 CONNECT  K4 SCAN  SW BACK
HELP:   JOY SCROLL  SW BACK
```

## 未改
- 全屏 image/video 不加框架。
- 外设/网络/OTA/DSH 未改。

## 未实机验证
- K4 长按 700ms 抖动窗口。
- 所有子页长期残影。
