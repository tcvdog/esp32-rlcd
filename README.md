# ESP32-S3-RLCD-4.2 game port workspace

Board: Waveshare ESP32-S3-RLCD-4.2 (300x400 ST7306 reflective LCD, 16 MB flash, 8 MB PSRAM).

## Current build

- Firmware: **v0.45.0**
- 400x300 1bpp retro stickman game
- Survey: 前额叶修复 5-question self-rating, saved to `/sdcard/survey_prefrontal.csv`
- KET/PET sentence memory: 100 + 100 words with English/Chinese sentences
- D-key sentence TTS: `/sdcard/tts/{ket,pet}_NNN.mp3` (generate with `tools/gen_sentence_tts.py`)
- 6-item main menu: word, physics, SD, music, survey, settings; game entry hidden
- Word cards: 100 KET/PET words + 100 example sentences (6 basic review cards kept), Chinese meaning and sentence translation behind A
- SD browser: TXT paging, WAV playback, MP3 playback, GIF/PNG/JPG/BMP first-frame viewing
- Voice command C+D: WAV upload to PC, SenseVoice + Whisper dual ASR
- PC bridge to the current DSH Web session
- Settings submenu: 2-column card grid — sound, Wi-Fi, system info, help, voice notes, command feedback
- PCF85063A RTC + NVS time fallback
- FreeRTOS task monitor as System Info page 4 (state/priority/stack)
- 3-page system information: software, memory/storage, network/power
- Settings: speaker volume, ST7305 contrast, Wi-Fi scan and on-screen keyboard

The repository contains the application binary in `firmware/` and the full source in `game_proto/`.

> Default Wi-Fi credentials are placeholders. Set the SSID/password through
> Settings → Wi-Fi, or edit the fallback in `game_proto/main/main.cpp` locally.
> Do not commit real credentials.

## Environment

- ESP-IDF v5.4.4: `~/esp/esp-idf`
- Target toolchain: `esp32s3`
- Source environment:

```bash
source esp32-rlcd/setup_idf.sh
idf.py --version
```

## Current status

- The Waveshare U8G2 display test was flashed successfully and shows the frame counter on the 300x400 RLCD.
- The 400x300 landscape stickman prototype is flashed and running on the board.
- The ST7306/ST7305-compatible display path, 1bpp output, power and USB flashing are verified.
- KOF-style A/B/C/D controls, low sweep, rising knee, QCF ink, back guard, auto-facing, AI and 1.3x ink slices were verified on hardware.
- Wi-Fi OTA was tested twice: v0.6.0 and v0.9.3 uploaded to `http://192.168.3.103/update`, reboot, `/version` and the page still reachable.
- v0.9.3: HP 1000, gas pip 60% requirement, reset = yellow+green, 3 gas pips, gas gain 1/4 speed, ink consumes 1 pip.
- The RLCD top-right shows the firmware version; the OTA page shows the same version and build time.
- Jump/air-hit gravity, KO and reset were verified.
- `/dev/ttyACM0` permissions reset when the board re-enumerates; run `sudo chmod 666 /dev/ttyACM0` after bootloader/power cycles.

## Game prototype

The playable prototype is in `game_proto/`. See `game_proto/README.md` for the final wiring and combo controls.

## Previous joystick test project

`joystick_test/` reads the X1K4 joystick:

- X/Y: ADC1 channels 0/1 on GPIO1/GPIO2
- SW + four buttons: GPIO0/13/14/17/18

See `joystick_test/README.md` for wiring and pin-conflict warnings.

## Build

```bash
source esp32-rlcd/setup_idf.sh
cd esp32-rlcd/game_proto
idf.py set-target esp32s3
idf.py build
```

Prebuilt application image: `firmware/esp32-rlcd-v0.45.0.bin`.

## OTA (preferred)

```bash
cd esp32-rlcd
./ota_upload.sh 192.168.3.103
```

The board screen shows its current OTA IP. AP fallback: `STICKMAN-RLCD` / `stickman123` at `192.168.4.1`.

## USB flash (first install only)

The board's native USB should appear as `/dev/ttyACM0`:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

If permission is denied:

```bash
sudo usermod -aG dialout "$USER"
# log out/in, then reconnect
```
