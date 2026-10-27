TARGET  := hello
BUILD   := build
SRCS    := src/startup.c src/main.c
PORT    := /dev/ttyACM0
BAUD    := 115200

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPU     := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS  := $(CPU) -std=c11 -Wall -Wextra -Os -g -ffreestanding \
           -ffunction-sections -fdata-sections
LDFLAGS := $(CPU) -T linker.ld -nostdlib -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map

OBJS    := $(SRCS:%.c=$(BUILD)/%.o)

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD)/$(TARGET).elf
	STM32_Programmer_CLI -c port=SWD -w $< -v -rst

monitor:
	(stty $(BAUD) raw -echo; cat) < $(PORT)

clean:
	rm -rf $(BUILD)

.PHONY: all flash monitor clean
