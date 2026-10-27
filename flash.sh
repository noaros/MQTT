#!/usr/bin/env bash
# Build, then program the board over SWD and reset it.
set -euo pipefail
cd "$(dirname "$0")"

./build.sh
STM32_Programmer_CLI -c port=SWD -w build/hello.elf -v -rst
