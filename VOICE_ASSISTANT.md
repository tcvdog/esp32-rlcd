# 语音记录 / 人生指导（局域网版）

## 结论

ESP32-S3 不适合直接跑通用中文语音识别：

- Vosk 中文小模型约 66MB，超过内部 SRAM，8MB PSRAM 也很紧
- Whisper 模型和计算量更大，无法在 S3 上实时运行
- ESP-SR（Espressif 开源）适合唤醒词/命令词，不适合自由说话转写

可行架构：

```text
ESP32-S3 麦克风 (ES7210)
  -> 录 6 秒 WAV
  -> HTTP POST 到电脑 192.168.3.39:8765
  -> Vosk 中文小模型转写
  -> 本机 Ollama qwen2.5:7b 生成记录/建议
  -> 电脑网页显示，JSONL 存档
```

全部在局域网内，不需要云端。

## 已安装

- Vosk 0.3.45
- 中文小模型 `vosk-model-small-cn-0.22`（66MB）
- Ollama `qwen2.5:7b`
- 服务脚本：`voice_server.py`

## 启动服务

```bash
cd '/mnt/documents/桌面/web游戏/esp32-rlcd'
nohup /home/tcvdog/voice-venv/bin/python voice_server.py \
  > /tmp/voice_server.log 2>&1 &
```

网页：

```text
http://192.168.3.39:8765/
```

接口：

```text
POST /voice   body = audio/wav
GET  /latest.json
GET  /voice_last.wav
```

## ESP32 端

固件 v0.12.0 增加：

- 目录第 4 项“语音记录”
- A 键录 6 秒，自动上传
- 上传地址 `http://192.168.3.39:8765/voice`
- A+C 按住 0.6 秒返回目录

注意：当前板子离线，v0.12.0 还没有 OTA 上去。板子开机并连上 WiFi 后：

```bash
cd '/mnt/documents/桌面/web游戏/esp32-rlcd'
./ota_upload.sh <板子屏幕上的IP>
```

## 使用

1. 电脑运行 voice_server.py
2. 板子进入 目录 -> 语音记录
3. 按 A，靠近麦克风说 6 秒
4. 板子显示 `SENT OK - SEE PC`
5. 电脑打开 `http://192.168.3.39:8765/` 看转写和 Qwen 建议

## 限制

- 电脑 IP 目前写死为 `192.168.3.39`；如果 PC IP 变了，需要改 `voice_server.py` 的监听和 ESP32 的上传 URL
- Vosk 小模型对远场/噪声敏感，尽量靠近板载麦克风
- 6 秒录音固定，后续可改成 VAD 自动截断
- 记录保存在 `voice_notes.jsonl`
