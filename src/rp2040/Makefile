# Additional RP2040 build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/rp2040 src/generic lib/rp2040/elf2uf2 lib/fast-hash lib/can2040 lib/rp2040/pico/flash/

CFLAGS += -mcpu=cortex-m0plus -mthumb -Ilib/cmsis-core
CFLAGS += -Ilib/rp2040 -Ilib/rp2040/cmsis_include -Ilib/fast-hash -Ilib/can2040 -Ilib/rp2040/pico/flash/ -Ilib/rp2040/pico/bootrom/

CFLAGS_katapult.elf += -nostdlib -lgcc -lc_nano
CFLAGS_katapult.elf += -T $(OUT)src/rp2040/rp2040_link.ld

# Add source files
mcu-y  = rp2040/main.c rp2040/gpio.c rp2040/timer.c rp2040/flash.c  ../lib/rp2040/pico/flash/hw_flash.c
mcu-y += generic/armcm_irq.c generic/crc16_ccitt.c

src-y += rp2040/armcm_canboot.c $(mcu-y)
src-$(CONFIG_USBSERIAL) += rp2040/usbserial.c generic/usb_cdc.c
src-$(CONFIG_USBSERIAL) += rp2040/chipid.c
src-$(CONFIG_SERIAL) += rp2040/serial.c generic/serial_irq.c
src-$(CONFIG_CANSERIAL) += rp2040/can.c rp2040/chipid.c ../lib/can2040/can2040.c
src-$(CONFIG_CANSERIAL) += generic/canserial.c generic/canbus.c
src-$(CONFIG_CANSERIAL) += ../lib/fast-hash/fasthash.c

$(OUT)katapult.elf: $(OUT)stage2.o $(OUT)src/rp2040/rp2040_link.ld
# rp2040 stage2 building
STAGE2_FILE := $(shell echo $(CONFIG_RP2040_STAGE2_FILE))
$(OUT)stage2.o: lib/rp2040/boot_stage2/$(STAGE2_FILE) $(OUT)autoconf.h
	@echo "  Building rp2040 stage2 $@"
	$(Q)$(CC) $(CFLAGS) -Ilib/rp2040/boot_stage2 -Ilib/rp2040/boot_stage2/asminclude -DPICO_FLASH_SPI_CLKDIV=$(CONFIG_RP2040_STAGE2_CLKDIV) -c $< -o $(OUT)stage2raw1.o
	$(Q)$(LD) $(OUT)stage2raw1.o --script=lib/rp2040/boot_stage2/boot_stage2.ld -o $(OUT)stage2raw.o
	$(Q)$(OBJCOPY) -O binary $(OUT)stage2raw.o $(OUT)stage2raw.bin
	$(Q)lib/rp2040/boot_stage2/pad_checksum -s 0xffffffff $(OUT)stage2raw.bin $(OUT)stage2.S
	$(Q)$(CC) $(CFLAGS) -c $(OUT)stage2.S -o $(OUT)stage2.o
OBJS_katapult.elf += $(OUT)stage2.o

# Binary output file rules
target-y += $(OUT)katapult.uf2

$(OUT)lib/rp2040/elf2uf2/elf2uf2: lib/rp2040/elf2uf2/main.cpp
	@echo "  Building $@"
	$(Q)g++ -g -O -Ilib/rp2040 $< -o $@

$(OUT)katapult.uf2: $(OUT)katapult.elf $(OUT)lib/rp2040/elf2uf2/elf2uf2
	@echo "  Creating uf2 file $@"
	$(Q)$(OUT)lib/rp2040/elf2uf2/elf2uf2 $(OUT)katapult.elf $(OUT)katapult.uf2
ifeq ($(CONFIG_RP2040_ADD_BOOT_SIGNATURE), y)
	$(Q)$(PYTHON) ./scripts/uf2_append_boot_signature.py --address $(CONFIG_LAUNCH_APP_ADDRESS) --input $(OUT)katapult.uf2 --output $(OUT)katapult.uf2
endif
	@echo "  Creating legacy uf2 file $(OUT)canboot.uf2"
	$(Q)cp $@ $(OUT)canboot.uf2

lib/rp2040_flash/rp2040_flash:
	@echo "  Building rp2040_flash"
	$(Q)make -C lib/rp2040_flash rp2040_flash

# Flash rules
flash: $(OUT)katapult.uf2 lib/rp2040_flash/rp2040_flash
	@echo "  Flashing $< "
	$(Q) $(if $(NOSUDO),,sudo) ./lib/rp2040_flash/rp2040_flash $(OUT)katapult.uf2

# Deployer build
deployer-y += generic/armcm_boot.c generic/armcm_reset.c $(mcu-y)
CFLAGS_deployer.elf += -nostdlib -lgcc -lc_nano
CFLAGS_deployer.elf += -T $(OUT)src/generic/armcm_deployer.ld
$(OUT)deployer.elf: $(OUT)src/generic/armcm_deployer.ld
