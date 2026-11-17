# Stickman RLCD game prototype

Waveshare ESP32-S3-RLCD-4.2, **400×300 landscape**, **1bpp**, U8G2 ST7305 driver.

## Verified on hardware

- Screen: landscape 400×300, black on white
- Stickman and dummy, HP bars
- Joystick move / jump / guard
- Light punch / light kick
- Heavy punch / heavy kick
- GIF ink cartwheel: 10 × 1bpp slices, 1.3× scale, same character height as the vector rig
- Reset

## Wiring used

| Signal | Board header | GPIO |
|---|---|---|
| sy | GP1 | GPIO1 / ADC1_CH0 |
| sx | GP2 | GPIO2 / ADC1_CH1 |
| sk | GP3 | GPIO3 |
| red | GP17 | GPIO17 |
| yellow | GP18 | GPIO18 |
| blue / C | TXD | GPIO43 |
| green / D | RXD | GPIO44 |
| power | 3V3 | 3.3 V only |
| ground | GND | GND |

GPIO19/20 are the native USB D-/D+ lines and are **not** used for buttons.

GPIO13/14 are now reserved for the ES8311 codec I2C bus. Blue/green moved to
TXD/RXD (GPIO43/44); UART0 console output is therefore not used for buttons.

## Controls (KOF-style)

| Input | Action |
|---|---|
| joystick left/right | walk; hold in the air to jump forward/back |
| double tap toward / away | fast forward dash / backdash |
| joystick up | jump |
| joystick down | crouch / low-sweep modifier |
| hold back while attacked | guard |
| A / red | light punch |
| B / yellow | light kick |
| C / blue | heavy punch |
| D / green | heavy kick |
| ↓ + B/D | low sweep (visible crouch) |
| → + C/D | rising knee (jumps up, launches) |
| ↓ ↘ → + C or D | ink cartwheel |
| yellow + green, held 0.25 s | reset |

The AI uses the same light/heavy moves, guard and occasional ink cartwheel,
and both fighters automatically turn to face each other when free to act.

### Implemented AI

- approach / retreat based on distance
- random light punch, light kick, heavy punch, heavy kick
- low sweep and rising knee (launcher)
- short guard reaction to incoming attacks
- ink cartwheel with a 2.8 s cooldown
- both sides take damage, hit-stun, KO and reset

### HP and gas

- Max HP: **1000** per fighter, shown as a long bar plus a numeric value.
- Gas stocks: **3 pips** per fighter, starting at 1.
- Gain gas by dealing damage, taking damage or blocking; the gain is scaled to **1/4** of the old rate, and each pip needs **60%** of the old 100-point charge (60 points).
- Ink cartwheel costs **1 gas pip**.
- Without gas the QCF+C/D input does not produce ink.
- The AI follows the same gas rule.

### Status bar and audio

- Top row: time (NTP), Wi-Fi bars + **current SSID**, firmware version, battery %, charging bolt.
- Battery: GPIO4 / ADC1_CH3, 3:1 divider.
- Audio: **ES8311 speaker** via codec_board (I2C 13/14, I2S 16/9/45/10/8,
  PA 46), synthesized combat PCM (no external wav files).
- GP0 passive-buzzer tones remain as fallback if codec init fails.
- Hit/ink/knee/block/jump/dash/KO each have a distinct sound.

### Character proportions (v0.10.0)

- head: 18 px disc (no eyes)
- torso: 36 px, 8 px round brush
- upper arm / forearm: 21 / 21
- thigh / shin: 32 / 33
- shoulder width 20, hip width 12
- front limbs 6 px round, back limbs 4 px dashed round
- static stance: lead hand guard, rear hand at ribs, bent lead leg

The 10 ink frames were measured (height ~130 px, width 68-196 px depending on
the cartwheel phase; head roughly 1/7 of height, legs about half). The redesign
uses those proportions instead of copying the distorted spin silhouette.

### Combat effects

- hit-stop, screen shake, radial ink sparks, expanding shockwave rings
- speed streaks during heavy/ink moves
- local `preview-fx.gif` / `assets/fx` were used as the effect language and
  translated to 1-bpp procedural rings/streaks rather than colored textures.

### Wi-Fi OTA

The firmware starts a Wi-Fi station + AP fallback and an HTTP OTA page.

- Home Wi-Fi: `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD`
- Board IP shown on the top of the screen as `OTA 192.168.x.x`
- Browser: `http://<board-ip>/` or, when on the fallback AP, `http://192.168.4.1/`
- OTA upload was tested successfully from `192.168.3.103` (v0.6.0 and v0.10.0; the script allows 300 s for slow Wi-Fi).
- Fallback AP: `STICKMAN-RLCD` / `stickman123`
- Upload `build/game_proto.bin` from the page.

From the PC:

```bash
cd esp32-rlcd
./ota_upload.sh 192.168.3.103
```

The first installation needs USB once to write the OTA partition table.
After that, firmware updates are browser uploads; no BOOT/PWR/chmod cycle.

### Current scope

This is a playable 1P-vs-AI hardware prototype. Remaining work: two-player
mode, sound, camera effects and the remaining web-demo hit-effect polish.

## Firmware version

Single source of truth: `main/fw_version.h`

```c
#define FW_VERSION "v0.6.0"
#define FW_NAME    "KOF-AI-OTA"
#define FW_BUILD   __DATE__ " " __TIME__
```

- The RLCD shows `v0.6.0` at the top right.
- The OTA page shows the same version, name and build time.
- `GET /version` returns JSON:
  `{"version":"v0.10.0","name":"KOF-AI-OTA","build":"..."}`
- `ota_upload.sh` reads the header and prints the version before uploading.

Bump `FW_VERSION` before each OTA build so the screen and PC page can be compared.

## Build

```bash
source ../setup_idf.sh
cd game_proto
idf.py set-target esp32s3
idf.py build
```

## Flash

The board must be in ROM download mode:

1. Long-press middle PWR to power off.
2. Hold right BOOT.
3. Click middle PWR.
4. Release BOOT after ~2 s.
5. `sudo chmod 666 /dev/ttyACM0`

Then:

```bash
cd game_proto/build
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before no_reset --after no_reset write_flash @flash_args
esptool.py --chip esp32s3 -p /dev/ttyACM0 \
  --before no_reset --after watchdog_reset chip_id
```

## Current scope

This is a first playable prototype, not the full web demo yet. It does not yet
include AI, two-player mode, sound, camera shake/zoom, low sweep, rising knee,
or the full hit-effect stack.
