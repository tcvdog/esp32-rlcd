# X1K4 joystick test — Waveshare ESP32-S3-RLCD-4.2

## 1. Board header P1 (from the schematic)

The board's 2x8 header is:

| Pin | Signal | Pin | Signal |
|---:|---|---:|---|
| 1 | 3V3 | 2 | VBUS (5V) |
| 3 | GND | 4 | GND |
| 5 | GPIO0 / BOOT | 6 | USB_N |
| 7 | GPIO1 / ADC1_CH0 | 8 | USB_P |
| 9 | GPIO2 / ADC1_CH1 | 10 | U0TXD |
| 11 | GPIO3 | 12 | U0RXD |
| 13 | GPIO17 | 14 | ESP32_SDA / GPIO13 |
| 15 | GPIO18 / KEY | 16 | ESP32_SCL / GPIO14 |

**Do not connect anything to the USB_N/USB_P pins (6 and 8).**

## 2. Direct wiring (no extra chip)

The X1K4 has 9 pins. The usual order is:

`GND  5V  VRx  VRy  SW  K1  K2  K3  K4`

Verify the labels on your module before powering it.

| X1K4 | Board P1 header | ESP32-S3 | Notes |
|---|---|---|---|
| GND | pin 3 or 4 | GND | common ground |
| 5V/VCC | pin 1 | 3V3 | **3.3 V only — never 5 V** |
| VRx | pin 7 | GPIO1 / ADC1_CH0 | analog X |
| VRy | pin 9 | GPIO2 / ADC1_CH1 | analog Y |
| SW | pin 5 | GPIO0 | active-low; also BOOT |
| K1 | pin 14 | GPIO13 | active-low; SDA pad repurposed |
| K2 | pin 16 | GPIO14 | active-low; SCL pad repurposed |
| K3 | pin 13 | GPIO17 | active-low; also SD card CS |
| K4 | pin 15 | GPIO18 | active-low; also onboard KEY |

### Important tradeoffs

- This game-only pinout **repurposes the RTC/SHTC3 I2C pins** (GPIO13/14) and the **microSD CS** pin (GPIO17). Do not use those peripherals while this firmware runs.
- GPIO0 is the **BOOT strapping pin**. Do not hold the joystick switch during reset/flashing.
- GPIO13, GPIO14 and GPIO18 have onboard pull-ups. Add **10 kΩ pull-ups to 3.3 V** on GPIO0, GPIO17 and the switch lines if the module does not already have them.
- The X1K4 may switch its buttons to GND (active-low) or VCC (active-high). Measure one button with a multimeter first. This code assumes active-low.
- Power the joystick from **3.3 V**. The ESP32-S3 ADC is not 5 V tolerant.

## 3. Cleaner alternative: PCF8574

If you want to keep the RTC, SHTC3 and SD card, use a **PCF8574** I2C expander at address 0x20:

| X1K4 | PCF8574 | Board |
|---|---|---|
| VRx | — | GPIO1 |
| VRy | — | GPIO2 |
| VCC | VCC | 3V3 |
| GND | GND | GND |
| SW | P0 | — |
| K1 | P1 | — |
| K2 | P2 | — |
| K3 | P3 | — |
| K4 | P4 | — |
| — | SDA | GPIO13 / pin 14 |
| — | SCL | GPIO14 / pin 16 |

The PCF8574 uses the existing I2C bus. Set P0–P7 high before reading; a pressed active-low button reads 0.

## 4. Build and flash

```bash
source ~/esp/esp-idf/export.sh
cd esp32-rlcd/joystick_test
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The `sdkconfig.defaults` selects:

- ESP32-S3
- 16 MB flash
- 8 MB octal PSRAM
- USB Serial/JTAG console

The board's Type-C port should enumerate as `/dev/ttyACM0` when connected with a **data** cable. If it enumerates but permission is denied:

```bash
sudo usermod -aG dialout "$USER"
# log out and back in, then reconnect the board
```

## 5. Expected serial output

```text
X:2048(CENTER) Y:2049(MID) SW:0 K1:0 K2:0 K3:0 K4:0
```

- Center X/Y should be around 1800–2300 at 12 dB attenuation.
- Adjust the 1200/2800 thresholds in `main.c`.
- If the directions are reversed, swap the X/Y wires or change the threshold order.

## 6. Pin conflict summary

| GPIO | Board function | Game use |
|---|---|---|
| 0 | BOOT button | joystick SW (do not hold at reset) |
| 1 | header ADC | joystick X |
| 2 | header ADC | joystick Y |
| 13 | RTC/SHTC3 SDA | K1 |
| 14 | RTC/SHTC3 SCL | K2 |
| 17 | microSD CS | K3 |
| 18 | onboard KEY | K4 |

For the full game, a PCF8574 or a dedicated button board is recommended if you want to keep the sensors and SD card.
