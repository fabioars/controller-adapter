#!/usr/bin/env bash
# Builds and runs the host-side unit tests (no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")/.."
CFLAGS=(-std=c11 -Wall -Wextra -Werror -Imain)
cc "${CFLAGS[@]}" -o test/run_ds3 test/test_ds3_report.c main/ds3_report.c
cc "${CFLAGS[@]}" -o test/run_x360 test/test_x360_report.c main/x360_report.c
./test/run_ds3
./test/run_x360
