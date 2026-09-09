# Katapult GD32 platform build rules
#
# Keep all GD32 runtime code in src/gd32 and all vendor device headers in
# lib/gd32*/include.  In particular, do not route GD32F303 through lib/stm32f1:
# the peripherals are similar, but the clock, FMC, USB and device definitions
# are not interchangeable.

CROSS_PREFIX=arm-none-eabi-

dirs-y += src/gd32 src/generic lib/fast-hash
dirs-$(CONFIG_MACH_GD32F30X) += lib/gd32f30x/include
dirs-$(CONFIG_MACH_GD32E23X) += lib/gd32e23x/include

CFLAGS-$(CONFIG_MACH_GD32F30X) += -mcpu=cortex-m4 -DUSE_STDPERIPH_DRIVER
CFLAGS-$(CONFIG_MACH_GD32F30X) += -Ilib/gd32f30x/include
CFLAGS-$(CONFIG_MACH_GD32F303XB) += -DGD32F30X_HD
CFLAGS-$(CONFIG_MACH_GD32F303XC) += -DGD32F30X_HD
CFLAGS-$(CONFIG_MACH_GD32F303XE) += -DGD32F30X_XD
CFLAGS-$(CONFIG_MACH_GD32E23X) += -mcpu=cortex-m23 -DGD32E230
CFLAGS-$(CONFIG_MACH_GD32E23X) += -Ilib/gd32e23x/include
CFLAGS += $(CFLAGS-y) -mthumb -Ilib/cmsis-core -Ilib/fast-hash

CFLAGS_katapult.elf += -nostdlib -lgcc -lc_nano
CFLAGS_katapult.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)katapult.elf: $(OUT)src/generic/armcm_link.ld

f30x-mcu-y := gd32/gd32f30x_flash.c
f30x-mcu-y += gd32/gd32f30x.c gd32/gpio.c
f30x-mcu-y += generic/armcm_irq.c generic/armcm_timer.c generic/crc16_ccitt.c
e23x-mcu-y := gd32/gd32e23x.c gd32/gd32e23x_flash.c gd32/gd32e23x_timer.c
e23x-mcu-y += gd32/gd32e23x_gpio.c generic/armcm_irq.c generic/crc16_ccitt.c

mcu-$(CONFIG_MACH_GD32F30X) += $(f30x-mcu-y)
mcu-$(CONFIG_MACH_GD32E23X) += $(e23x-mcu-y)
src-y += generic/armcm_canboot.c $(mcu-y)

f30x-src-$(CONFIG_SERIAL) += gd32/serial.c generic/serial_irq.c
f30x-src-$(CONFIG_USBSERIAL) += gd32/usbfs.c gd32/chipid.c generic/usb_cdc.c
e23x-src-$(CONFIG_SERIAL) += gd32/gd32e23x_serial.c generic/serial_irq.c
src-$(CONFIG_MACH_GD32F30X) += $(f30x-src-y)
src-$(CONFIG_MACH_GD32E23X) += $(e23x-src-y)

canbus-src-y := gd32/can.c gd32/chipid.c generic/canserial.c
canbus-src-y += generic/canbus.c ../lib/fast-hash/fasthash.c
f30x-src-$(CONFIG_CANSERIAL) += $(canbus-src-y)

CFLAGS_deployer.elf += -nostdlib -lgcc -lc_nano
CFLAGS_deployer.elf += -T $(OUT)src/generic/armcm_deployer.ld
$(OUT)deployer.elf: $(OUT)src/generic/armcm_deployer.ld
deployer-y += generic/armcm_boot.c generic/armcm_reset.c $(mcu-y)
