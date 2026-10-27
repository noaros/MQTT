#!/usr/bin/env bash
# Compile and link the firmware into build/hello.elf and build/hello.bin.
set -euo pipefail
cd "$(dirname "$0")"

TARGET=hello
BUILD=build
SRCS=(src/startup.c src/main.c)

CPU=(-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
CFLAGS=("${CPU[@]}" -std=c11 -Wall -Wextra -Os -g -ffreestanding
        -ffunction-sections -fdata-sections)
LDFLAGS=("${CPU[@]}" -T linker.ld -nostdlib -Wl,--gc-sections
         "-Wl,-Map=$BUILD/$TARGET.map")

OBJS=()
for src in "${SRCS[@]}"; do
    obj="$BUILD/${src%.c}.o"
    mkdir -p "$(dirname "$obj")"
    set -x
    arm-none-eabi-gcc "${CFLAGS[@]}" -c "$src" -o "$obj"
    { set +x; } 2>/dev/null
    OBJS+=("$obj")
done

set -x
arm-none-eabi-gcc "${LDFLAGS[@]}" "${OBJS[@]}" -o "$BUILD/$TARGET.elf"
arm-none-eabi-size "$BUILD/$TARGET.elf"
arm-none-eabi-objcopy -O binary "$BUILD/$TARGET.elf" "$BUILD/$TARGET.bin"
