# lpc176x build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/lpc176x src/generic lib/lpc176x/device

CFLAGS += -mthumb -mcpu=cortex-m3 -Ilib/lpc176x/device -Ilib/cmsis-core

CFLAGS_canboot.elf += --specs=nano.specs --specs=nosys.specs
CFLAGS_canboot.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)canboot.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
src-y += lpc176x/main.c lpc176x/gpio.c lpc176x/flash.c
src-y += generic/armcm_canboot.c generic/armcm_irq.c
src-y += generic/armcm_timer.c generic/crc16_ccitt.c
src-y += ../lib/lpc176x/device/system_LPC17xx.c

src-$(CONFIG_USBSERIAL) += lpc176x/usbserial.c lpc176x/chipid.c
src-$(CONFIG_USBSERIAL) += generic/usb_cdc.c
src-$(CONFIG_SERIAL) += lpc176x/serial.c generic/serial_irq.c


# Build the additional bin output file
target-y += $(OUT)canboot.bin

$(OUT)canboot.bin: $(OUT)canboot.elf
	@echo "  Creating hex file $@"
	$(Q)$(OBJCOPY) -O binary $< $(OUT)canboot.work
	$(Q)$(PYTHON) ./scripts/buildbinary.py -b $(CONFIG_FLASH_START) -s $(CONFIG_APPLICATION_START) $(OUT)canboot.work $@
