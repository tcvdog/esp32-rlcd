# UI 全局骨架自检（v0.63.0）

## 已覆盖
- 主菜单、闪卡分类/卡片、设置、音乐、SD、物理、格斗、调查、语音、信息/帮助页。
- 顶栏 24px：路径 + Wi-Fi/SD/电池。
- 底栏 24px：键帽 + 当前页面核心操作。
- 1bpp 极性：普通页与 WULI 反相页共用 `ui_theme`，通过 `begin(g, g_no_invert)` 切换黑白映射。
- 旧标题/旧提示：已移除会被顶/底栏压住的重复项。
- 格斗 HUD：P1/AI/HP/GAS 下移到顶栏之后。

## 文件
```text
ui_theme.h/.cpp
ui_icons.h
main.cpp
CMakeLists.txt
fw_version.h
```

## 内存/性能
- 图标与主题常量静态存放，无新增 PSRAM 动态分配。
- 每帧只多绘制顶/底栏与少量图标，没有全屏复制。
- 主循环仍为目标 25fps。

## 未改
- GPIO/摇杆/按键映射
- Wi-Fi/OTA/AP fallback
- 音频/语音/DSH 链路
- 分区表

## 未实机验证
- K4 长按 700ms 的机械抖动窗口。
- 所有页面在 RLCD 上的长期残影表现。
- 逐页实际帧时间统计。
