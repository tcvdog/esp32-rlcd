#!/usr/bin/env bash
echo "== USB devices =="
lsusb
echo
echo "== Espressif / serial candidates =="
lsusb | grep -Ei '303a|espressif|1a86|10c4|cp210|ch34' || echo "no ESP32/CH34x/CP210x USB device found"
echo
echo "== tty devices =="
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "no /dev/ttyACM* or /dev/ttyUSB* found"
echo
echo "== user groups =="
id -nG
