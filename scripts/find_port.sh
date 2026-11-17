#!/usr/bin/env bash
# Finds the serial port of a connected ESP chip by asking each USB-serial port which chip it has.
#
#   [CHIP=ESP8266] scripts/find_port.sh [PORT]        (CHIP defaults to ESP32-S3)
#   CHIP="ESP32-S3|ESP8266" scripts/find_port.sh       (accept any of these)
#
# Prints "<port>" on stdout, or "<chip> <port>" when PRINT_CHIP=1; progress goes to stderr.
# With PORT given, only that port is checked.
# Needs esptool on the path (run inside the ESP-IDF environment).
set -uo pipefail

WANT="${CHIP:-ESP32-S3}"

if [ -n "${1:-}" ]; then
    candidates=("$1")
else
    shopt -s nullglob
    candidates=(/dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*)
fi

if [ ${#candidates[@]} -eq 0 ]; then
    echo "No USB serial ports found." >&2
    echo "- Two-port boards: plug the port labelled COM/UART into the computer." >&2
    echo "- One-port boards running the adapter firmware: that port is now the DualShock 3." >&2
    echo "  Hold BOOT (IO0) while plugging it in, then run the command again." >&2
    exit 1
fi

found=()
for port in "${candidates[@]}"; do
    printf "  %-28s " "$port" >&2
    chip=$(python -m esptool --port "$port" --connect-attempts 2 chip_id 2>&1 |
        sed -n 's/^Detecting chip type[. ]*//p' | head -1)
    if [ -z "$chip" ]; then
        echo "no ESP chip responding" >&2
        continue
    fi
    echo "$chip" >&2
    if [[ "$chip" =~ ^($WANT) ]]; then
        if [ "${PRINT_CHIP:-0}" = 1 ]; then
            echo "${BASH_REMATCH[1]} $port"
        else
            echo "$port"
        fi
        exit 0
    fi
    found+=("$chip on $port")
done

echo >&2
if [ "$WANT" = "ESP32-S3" ]; then
    echo "No ESP32-S3 found. This firmware needs an ESP32-S3 (native USB for the DualShock 3 + Bluetooth LE)." >&2
else
    echo "No ${WANT//|/ or } found." >&2
fi
if [ ${#found[@]} -gt 0 ]; then
    printf "Found instead: %s\n" "${found[@]}" >&2
fi
echo "Connect the board's UART port. If it still isn't found, put it in download mode:" >&2
echo "hold IO0 (BOOT), press and release RST (EN), then release IO0." >&2
exit 1
