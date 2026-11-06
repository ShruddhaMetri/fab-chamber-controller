# firmware/Makefile
# Command-line build of BOTH programs with the GNU Arm toolchain, using the
# same source files, startup code and linker scripts as STM32CubeIDE.
# CubeIDE is still what you use day to day; this file exists so that a
# machine with no IDE (the GitHub Actions server) can build the firmware too.
#
#   make            build bootloader + application into build/
#   make clean

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := $(MCU) -std=gnu11 -O0 -g3 -DDEBUG -DSTM32 -DSTM32F4 -DSTM32F407VGTx \
           -Wall -ffunction-sections -fdata-sections
LDFLAGS := $(MCU) --specs=nosys.specs --specs=nano.specs -static -Wl,--gc-sections \
           -Wl,--start-group -lc -lm -Wl,--end-group

APP_DIR  := chamber_ctrl
BOOT_DIR := chamber_boot

# Everything the CubeIDE projects compile: Src (and Src/freertos), Startup
APP_SRC  := $(wildcard $(APP_DIR)/Src/*.c $(APP_DIR)/Src/freertos/*.c) $(wildcard $(APP_DIR)/Startup/*.s)
BOOT_SRC := $(wildcard $(BOOT_DIR)/Src/*.c) $(wildcard $(BOOT_DIR)/Startup/*.s)
APP_LD   := $(wildcard $(APP_DIR)/*_FLASH.ld)
BOOT_LD  := $(wildcard $(BOOT_DIR)/*_FLASH.ld)

all: build/$(APP_DIR).bin build/$(BOOT_DIR).bin check-layout
	$(SIZE) build/$(BOOT_DIR).elf build/$(APP_DIR).elf

build:
	mkdir -p build

build/$(APP_DIR).elf: $(APP_SRC) $(APP_LD) | build
	$(CC) $(CFLAGS) -I$(APP_DIR)/Inc -x assembler-with-cpp $(filter %.s,$(APP_SRC)) -x c $(filter %.c,$(APP_SRC)) \
	      -T$(APP_LD) -Wl,-Map=build/$(APP_DIR).map $(LDFLAGS) -o $@

build/$(BOOT_DIR).elf: $(BOOT_SRC) $(BOOT_LD) | build
	$(CC) $(CFLAGS) -I$(BOOT_DIR)/Inc -x assembler-with-cpp $(filter %.s,$(BOOT_SRC)) -x c $(filter %.c,$(BOOT_SRC)) \
	      -T$(BOOT_LD) -Wl,-Map=build/$(BOOT_DIR).map $(LDFLAGS) -o $@

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@

# The two programs must sit where fw_image.h says, or the bootloader would
# jump into nothing. Check the start address of each vector table.
check-layout: build/$(APP_DIR).elf build/$(BOOT_DIR).elf
	@app=$$($(PREFIX)objdump -h build/$(APP_DIR).elf  | awk '$$2==".isr_vector"{print $$4}'); \
	 boot=$$($(PREFIX)objdump -h build/$(BOOT_DIR).elf | awk '$$2==".isr_vector"{print $$4}'); \
	 echo "bootloader at 0x$$boot, application at 0x$$app"; \
	 test "$$boot" = "08000000" || { echo "ERROR: bootloader must start at 0x08000000"; exit 1; }; \
	 test "$$app"  = "08020000" || { echo "ERROR: application must start at 0x08020000 (edit its _FLASH.ld)"; exit 1; }

clean:
	rm -rf build

.PHONY: all clean check-layout
