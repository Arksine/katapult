# Additional RPxxxx Raspberry Pi MCU build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/rp2040 src/generic lib/elf2uf2 lib/fast-hash lib/can2040
dirs-y += lib/pico-sdk/hardware

MCU := $(shell echo $(CONFIG_MCU))
MCU_UPPER := $(shell echo $(CONFIG_MCU) | tr a-z A-Z | tr X x)

CFLAGS-$(CONFIG_MACH_RP2040) += -mcpu=cortex-m0plus
CFLAGS-$(CONFIG_MACH_RP2350) += -mcpu=cortex-m33
CFLAGS += $(CFLAGS-y) -DPICO_$(MCU_UPPER) -mthumb -Ilib/cmsis-core
CFLAGS += -Ilib/pico-sdk/$(MCU) -Ilib/pico-sdk
CFLAGS += -Ilib/pico-sdk/$(MCU)/cmsis_include -Ilib/fast-hash -Ilib/can2040

# Add source files
mcu-y += rp2040/main.c rp2040/gpio.c rp2040/flash.c
mcu-y += generic/armcm_irq.c generic/crc16_ccitt.c
mcu-y += ../lib/pico-sdk/hardware/flash.c
mcu-$(CONFIG_MACH_RP2040) += rp2040/timer.c rp2040/bootrom.c
mcu-$(CONFIG_MACH_RP2350) += generic/armcm_timer.c rp2040/rp2350_bootrom.c
mcu-$(CONFIG_MACH_RP2350) += rp2040/rp2350_dblreset.c

src-y += generic/armcm_canboot.c $(mcu-y)
src-$(CONFIG_USBSERIAL) += rp2040/usbserial.c generic/usb_cdc.c rp2040/chipid.c
src-$(CONFIG_SERIAL) += rp2040/serial.c generic/serial_irq.c
src-$(CONFIG_CANSERIAL) += rp2040/can.c rp2040/chipid.c ../lib/can2040/can2040.c
src-$(CONFIG_CANSERIAL) += generic/canserial.c generic/canbus.c
src-$(CONFIG_CANSERIAL) += ../lib/fast-hash/fasthash.c

# rp2040 stage2 building
STAGE2_FILE := $(shell echo $(CONFIG_RP2040_STAGE2_FILE))
$(OUT)stage2.o: lib/pico-sdk/$(MCU)/boot_stage2/$(STAGE2_FILE) $(OUT)autoconf.h
	@echo "  Building rp2040 stage2 $@"
	$(Q)$(CC) $(CFLAGS) -Ilib/pico-sdk/$(MCU)/boot_stage2 -Ilib/pico-sdk/$(MCU)/boot_stage2/asminclude -DPICO_FLASH_SPI_CLKDIV=$(CONFIG_RP2040_STAGE2_CLKDIV) -c $< -o $(OUT)stage2raw1.o
	$(Q)$(LD) $(OUT)stage2raw1.o --script=lib/pico-sdk/$(MCU)/boot_stage2/boot_stage2.ld -o $(OUT)stage2raw.o
	$(Q)$(OBJCOPY) -O binary $(OUT)stage2raw.o $(OUT)stage2raw.bin
	$(Q)lib/pico-sdk/$(MCU)/boot_stage2/pad_checksum -s 0xffffffff $(OUT)stage2raw.bin $(OUT)stage2.S
	$(Q)$(CC) $(CFLAGS) -c $(OUT)stage2.S -o $(OUT)stage2.o

stage2-$(CONFIG_RP2040_HAVE_STAGE2) := $(OUT)stage2.o

# Set katapult.elf linker rules
target-y += $(OUT)katapult.uf2
CFLAGS_katapult.elf += -nostdlib -lgcc -lc_nano
CFLAGS_katapult.elf += -T $(OUT)src/rp2040/rpxxxx_link.ld
OBJS_katapult.elf += $(stage2-y)
$(OUT)katapult.elf: $(stage2-y) $(OUT)src/rp2040/rpxxxx_link.ld

# Binary output file rules
$(OUT)lib/elf2uf2/elf2uf2: lib/elf2uf2/main.cpp
	@echo "  Building $@"
	$(Q)g++ -g -O -Ilib/pico-sdk $< -o $@

$(OUT)katapult.uf2: $(OUT)katapult.elf $(OUT)lib/elf2uf2/elf2uf2
	@echo "  Creating uf2 file $@"
	$(Q)$(OUT)lib/elf2uf2/elf2uf2 $(OUT)katapult.elf $(OUT)katapult.uf2
	@echo "  Creating uf2 file $(OUT)katapult.withclear.uf2"
	$(Q)$(PYTHON) ./scripts/uf2_append_boot_signature.py --address $(CONFIG_LAUNCH_APP_ADDRESS) --input $(OUT)katapult.uf2 --output $(OUT)katapult.withclear.uf2
	@echo "  Creating legacy uf2 file $(OUT)canboot.uf2"
	$(Q)cp $@ $(OUT)canboot.uf2

# Flash rules
lib/rp2040_flash/rp2040_flash:
	@echo "  Building rp2040_flash"
	$(Q)make -C lib/rp2040_flash rp2040_flash

flash: $(OUT)katapult.uf2 lib/rp2040_flash/rp2040_flash
	@echo "  Flashing $< "
	$(Q) $(if $(NOSUDO),,sudo) ./lib/rp2040_flash/rp2040_flash $(OUT)katapult.uf2

# Deployer build
deployer-y += generic/armcm_boot.c generic/armcm_reset.c $(mcu-y)
CFLAGS_deployer.elf += -nostdlib -lgcc -lc_nano
CFLAGS_deployer.elf += -T $(OUT)src/rp2040/rpxxxx_deployer.ld
$(OUT)deployer.elf: $(OUT)src/rp2040/rpxxxx_deployer.ld
