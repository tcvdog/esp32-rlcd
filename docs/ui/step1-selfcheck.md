# UI Step 1 自检

- 视觉：24px 黑色顶栏、2×3 白底卡片、纯黑选中反色、2px 硬阴影、24px 键帽底栏。
- 文件：`ui_theme.h/cpp`、`ui_icons.h`、`main.cpp`、`CMakeLists.txt`、`fw_version.h`。
- 内存：图标为静态 const，无新增 PSRAM 动态分配；UI 不新增堆缓冲。
- 帧率：主循环仍为 25fps 目标；新增绘制为卡片/文字/图标，无逐帧大图。
- 外设：引脚、Wi-Fi、SD、音频、OTA、DSH 链路未改。
- 返回：K4 短按保持上下文原功能；K4 长按 700ms 调用 goBack；SW 仍可返回。
- 未实机验证：K4 长按的机械抖动窗口、长期按压是否误触、实际帧时间统计。
