# Additional STM32 build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/stm32 src/generic lib/fast-hash
dirs-$(CONFIG_MACH_STM32F0) += lib/stm32f0
dirs-$(CONFIG_MACH_STM32F1) += lib/stm32f1
dirs-$(CONFIG_MACH_STM32F2) += lib/stm32f2
dirs-$(CONFIG_MACH_STM32F4) += lib/stm32f4
dirs-$(CONFIG_MACH_STM32G0) += lib/stm32g0
dirs-$(CONFIG_MACH_STM32G4) += lib/stm32g4
dirs-$(CONFIG_MACH_STM32H7) += lib/stm32h7

MCU := $(shell echo $(CONFIG_MCU))
MCU_UPPER := $(shell echo $(CONFIG_MCU) | tr a-z A-Z | tr X x)

CFLAGS-$(CONFIG_MACH_STM32F0) += -mcpu=cortex-m0 -Ilib/stm32f0/include
CFLAGS-$(CONFIG_MACH_STM32F1) += -mcpu=cortex-m3 -Ilib/stm32f1/include
CFLAGS-$(CONFIG_MACH_STM32F2) += -mcpu=cortex-m3 -Ilib/stm32f2/include
CFLAGS-$(CONFIG_MACH_STM32F4) += -mcpu=cortex-m4 -Ilib/stm32f4/include
CFLAGS-$(CONFIG_MACH_STM32G0) += -mcpu=cortex-m0plus -Ilib/stm32g0/include
CFLAGS-$(CONFIG_MACH_STM32G4) += -mcpu=cortex-m4 -Ilib/stm32g4/include
CFLAGS-$(CONFIG_MACH_STM32H7) += -mcpu=cortex-m7 -Ilib/stm32h7/include
CFLAGS += $(CFLAGS-y) -D$(MCU_UPPER) -mthumb -Ilib/cmsis-core -Ilib/fast-hash

CFLAGS_katapult.elf += -nostdlib -lgcc -lc_nano
CFLAGS_katapult.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)katapult.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
mcu-y += stm32/flash.c stm32/clockline.c stm32/dfu_reboot.c
mcu-y += generic/armcm_irq.c generic/crc16_ccitt.c
mcu-$(CONFIG_MACH_STM32F0) += stm32/stm32f0.c ../lib/stm32f0/system_stm32f0xx.c
mcu-$(CONFIG_MACH_STM32F1) += stm32/stm32f1.c ../lib/stm32f1/system_stm32f1xx.c
mcu-$(CONFIG_MACH_STM32F2) += stm32/stm32f4.c ../lib/stm32f2/system_stm32f2xx.c
mcu-$(CONFIG_MACH_STM32F4) += stm32/stm32f4.c ../lib/stm32f4/system_stm32f4xx.c
mcu-$(CONFIG_MACH_STM32G0) += stm32/stm32g0.c
mcu-$(CONFIG_MACH_STM32G4) += stm32/stm32g4.c ../lib/stm32g4/system_stm32g4xx.c
mcu-$(CONFIG_MACH_STM32H7) += stm32/stm32h7.c ../lib/stm32h7/system_stm32h7xx.c
timer-src-y := generic/armcm_timer.c
timer-src-$(CONFIG_MACH_STM32F0) := stm32/stm32f0_timer.c
timer-src-$(CONFIG_MACH_STM32G0) := stm32/stm32f0_timer.c
gpio-src-y := stm32/gpio.c stm32/gpioperiph.c
gpio-src-$(CONFIG_MACH_STM32F1) := stm32/gpio.c
mcu-y += $(timer-src-y) $(gpio-src-y)
src-y += generic/armcm_canboot.c $(mcu-y)
serial-src-y := stm32/serial.c
serial-src-$(CONFIG_MACH_STM32F0) := stm32/stm32f0_serial.c
serial-src-$(CONFIG_MACH_STM32G0) := stm32/stm32f0_serial.c
serial-src-$(CONFIG_MACH_STM32G4) := stm32/stm32f0_serial.c
serial-src-$(CONFIG_MACH_STM32H7) := stm32/stm32f0_serial.c
src-$(CONFIG_SERIAL) += $(serial-src-y) generic/serial_irq.c
usb-src-$(CONFIG_HAVE_STM32_USBFS) := stm32/usbfs.c
usb-src-$(CONFIG_HAVE_STM32_USBOTG) := stm32/usbotg.c
src-$(CONFIG_USBSERIAL) += $(usb-src-y) stm32/chipid.c generic/usb_cdc.c
canbus-src-y := generic/canserial.c ../lib/fast-hash/fasthash.c
canbus-src-$(CONFIG_HAVE_STM32_CANBUS) += stm32/can.c
canbus-src-$(CONFIG_HAVE_STM32_FDCANBUS) += stm32/fdcan.c
src-$(CONFIG_CANSERIAL) += $(canbus-src-y) generic/canbus.c stm32/chipid.c

# Flash rules
flash: $(OUT)katapult.bin
	@echo "  Flashing $< to $(FLASH_DEVICE)"
	$(Q) $(if $(NOSUDO),,sudo) dfu-util -d "$(FLASH_DEVICE)" -R -a 0 -s "$(CONFIG_FLASH_START)":leave -D $(OUT)katapult.bin

# Deployer build
deployer-y += generic/armcm_boot.c generic/armcm_reset.c $(mcu-y)
CFLAGS_deployer.elf += -nostdlib -lgcc -lc_nano
CFLAGS_deployer.elf += -T $(OUT)src/generic/armcm_deployer.ld
$(OUT)deployer.elf: $(OUT)src/generic/armcm_deployer.ld
