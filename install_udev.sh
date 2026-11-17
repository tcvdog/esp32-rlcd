#!/usr/bin/env bash
set -euo pipefail
if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo: sudo bash $0" >&2
  exit 1
fi
cat > /etc/udev/rules.d/99-espressif.rules <<'RULE'
# Espressif ESP32-S3 USB JTAG/serial (Waveshare ESP32-S3-RLCD-4.2)
SUBSYSTEM=="usb", ATTR{idVendor}=="303a", MODE="0666", GROUP="plugdev", TAG+="uaccess"
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666", GROUP="plugdev", TAG+="uaccess"
RULE
udevadm control --reload-rules
udevadm trigger
echo "Installed /etc/udev/rules.d/99-espressif.rules"
ls -l /dev/ttyACM0 2>/dev/null || true
