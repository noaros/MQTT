#!/usr/bin/env bash
# Compile and link the firmware into build/mqtt.elf and build/mqtt.bin.
set -euo pipefail
cd "$(dirname "$0")"

TARGET=mqtt
BUILD=build
LWIP=lib/lwip/src
SRCS=(src/startup.c src/main.c
      $LWIP/core/*.c $LWIP/core/ipv4/*.c $LWIP/netif/ethernet.c
      $LWIP/apps/mqtt/mqtt.c)

CPU=(-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
CFLAGS=("${CPU[@]}" -std=c11 -Wall -Wextra -Os -g -ffreestanding
        -ffunction-sections -fdata-sections -Isrc -I$LWIP/include)
# The C library (newlib-nano) is linked only for memcpy, memset and friends
LDFLAGS=("${CPU[@]}" -T linker.ld -nostdlib -Wl,--gc-sections
         "-Wl,-Map=$BUILD/$TARGET.map")
LIBS=(-lc_nano -lgcc)

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
arm-none-eabi-gcc "${LDFLAGS[@]}" "${OBJS[@]}" "${LIBS[@]}" -o "$BUILD/$TARGET.elf"
arm-none-eabi-size "$BUILD/$TARGET.elf"
arm-none-eabi-objcopy -O binary "$BUILD/$TARGET.elf" "$BUILD/$TARGET.bin"
