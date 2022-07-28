# lpc176x build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/lpc176x src/generic lib/lpc176x/device

CFLAGS += -mthumb -mcpu=cortex-m3 -Ilib/lpc176x/device -Ilib/cmsis-core
CFLAGS += -Wno-nonnull

CFLAGS_canboot.elf += --specs=nano.specs --specs=nosys.specs
CFLAGS_canboot.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)canboot.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
mcu-y = lpc176x/main.c lpc176x/gpio.c lpc176x/flash.c
mcu-y += generic/armcm_irq.c generic/armcm_timer.c generic/crc16_ccitt.c
mcu-y += ../lib/lpc176x/device/system_LPC17xx.c

src-y += generic/armcm_canboot.c $(mcu-y)
src-$(CONFIG_USBSERIAL) += lpc176x/usbserial.c lpc176x/chipid.c
src-$(CONFIG_USBSERIAL) += generic/usb_cdc.c
src-$(CONFIG_SERIAL) += lpc176x/serial.c generic/serial_irq.c

BUILDBINARY_FLAGS = -l

# Deployer build
deployer-y += generic/armcm_boot.c generic/armcm_reset.c $(mcu-y)
CFLAGS_deployer.elf += --specs=nano.specs --specs=nosys.specs
CFLAGS_deployer.elf += -T $(OUT)src/generic/armcm_deployer.ld
$(OUT)deployer.elf: $(OUT)src/generic/armcm_deployer.ld
