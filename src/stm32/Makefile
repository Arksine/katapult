# Additional STM32 build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/stm32 src/generic
dirs-$(CONFIG_MACH_STM32F0) += lib/stm32f0
dirs-$(CONFIG_MACH_STM32F1) += lib/stm32f1
dirs-$(CONFIG_MACH_STM32F2) += lib/stm32f2
dirs-$(CONFIG_MACH_STM32F4) += lib/stm32f4

MCU := $(shell echo $(CONFIG_MCU))
MCU_UPPER := $(shell echo $(CONFIG_MCU) | tr a-z A-Z | tr X x)

CFLAGS-$(CONFIG_MACH_STM32F0) += -mcpu=cortex-m0 -Ilib/stm32f0/include
CFLAGS-$(CONFIG_MACH_STM32F1) += -mcpu=cortex-m3 -Ilib/stm32f1/include
CFLAGS-$(CONFIG_MACH_STM32F2) += -mcpu=cortex-m3 -Ilib/stm32f2/include
CFLAGS-$(CONFIG_MACH_STM32F4) += -mcpu=cortex-m4 -Ilib/stm32f4/include
CFLAGS-$(CONFIG_MACH_STM32F4) += -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += $(CFLAGS-y) -D$(MCU_UPPER) -mthumb -Ilib/cmsis-core -Ilib/fast-hash

CFLAGS_canboot.elf += --specs=nano.specs --specs=nosys.specs
CFLAGS_canboot.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)canboot.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
src-y += stm32/gpio.c generic/armcm_timer.c
src-y += generic/armcm_boot.c generic/armcm_irq.c
src-$(CONFIG_MACH_STM32F0) += ../lib/stm32f0/system_stm32f0xx.c
src-$(CONFIG_MACH_STM32F0) += stm32/stm32f0.c

src-$(CONFIG_MACH_STM32F1) += ../lib/stm32f1/system_stm32f1xx.c
src-$(CONFIG_MACH_STM32F1) += stm32/stm32f1.c stm32/flash.c

src-$(CONFIG_MACH_STM32F2) += ../lib/stm32f2/system_stm32f2xx.c
src-$(CONFIG_MACH_STM32F2) += stm32/stm32f4.c

src-$(CONFIG_MACH_STM32F4) += ../lib/stm32f4/system_stm32f4xx.c
src-$(CONFIG_MACH_STM32F4) += stm32/stm32f4.c

can-src-$(CONFIG_CANSERIAL) += stm32/can.c ../lib/fast-hash/fasthash.c
src-$(CONFIG_CANSERIAL) += $(can-src-y) generic/canbus.c
dirs-$(CONFIG_CANSERIAL) += lib/fast-hash

# Binary output file rules
target-y += $(OUT)canboot.bin

$(OUT)canboot.bin: $(OUT)canboot.elf
	@echo "  Creating hex file $@"
	$(Q)$(OBJCOPY) -O binary $< $@
