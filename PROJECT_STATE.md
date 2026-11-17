# ESP32-S3 RLCD 项目交接（精简版）

更新时间：2026-11-17  
用途：后续会话优先读本文件，避免重复上传大段日志、全量源码和无关截图。

## 硬件 / 网络

- 板子：Waveshare ESP32-S3-RLCD-4.2，400×300 1bpp 反射屏
- 固件版本：v0.50.0
- 板子 IP：192.168.3.103
- PC / 语音服务：192.168.3.39:8765
- DSH Web：127.0.0.1:3080，当前目标会话：esp32RLCD
- RTC：PCF85063A，`rtc_ok=1`；NVS 时间回退
- 真实 Wi-Fi 凭据只在板子 NVS；源码/仓库使用占位符

## 当前菜单

主目录 5 项：

1. 单词记忆
2. 物理实验
3. SD 卡
4. 音乐
5. 设置

设置子菜单 6 卡片：

- 声音（音量/亮度）
- Wi-Fi
- 系统信息（4 页）
- 使用帮助
- 语音记录
- 命令反馈

隐藏游戏：主目录输入 `↓ → B`，1.2 秒内完成；游戏退出回主目录。

## 关键功能

- 单词：KET 100 + PET 100 句；上下分类、左右换句、A 释义、D 朗读
- TTS：`/sdcard/tts/{ket,pet}_NNN.mp3`，DeepSeek TTS 生成，`tools/gen_sentence_tts.py` + `tools/upload_tts.py`
- SD：TXT 分页、WAV/MP3 播放、GIF/PNG/JPG/BMP 首帧
- 语音：C+D → WAV → SenseVoice + Whisper（DeepSeek ASR 暂时关闭）→ DSH
- 反馈：`/feedback`、`esp_feedback.py`
- 任务查看：系统信息第 4 页，显示 FreeRTOS 名称/状态/优先级/剩余栈
- OTA：`./ota_upload.sh 192.168.3.103`

## 已知限制

- GIF 只显示第一帧，未做动画
- MP3 通过 minimp3 播放并重采样到 16 kHz
- 反射屏无背光；“亮度”是 ST7305 电子对比度
- RTC 跨彻底断电需要 ML1220 可充电备用电池
- 语音桥接依赖 DSH Web；当前按 `esp32RLCD` 会话标题锁定

## 后续会话 token 规则

1. 先读本文件，不重复贴全量 `main.cpp`。
2. 只读相关函数/行段，不扫整个仓库日志。
3. 不上传 build/、录音 WAV、200 个 TTS、截图合集。
4. 每次只报告：改了什么、验证结果、剩余风险。
5. 大任务先给 3–5 行计划；不把思考过程写进回复。
6. 需要历史细节时按需查 Git 提交，不把提交全文塞入上下文。

## 仓库

https://github.com/tcvdog/esp32-rlcd  
最新交接前提交：aaabfed（v0.50.0）
