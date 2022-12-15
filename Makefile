# CanBoot build system
#
# Copyright (C) 2016-2020  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

# Output directory
OUT=out/

# Kconfig includes
export KCONFIG_CONFIG     := $(CURDIR)/.config
-include $(KCONFIG_CONFIG)

# Common command definitions
CC=$(CROSS_PREFIX)gcc
AS=$(CROSS_PREFIX)as
LD=$(CROSS_PREFIX)ld
OBJCOPY=$(CROSS_PREFIX)objcopy
OBJDUMP=$(CROSS_PREFIX)objdump
STRIP=$(CROSS_PREFIX)strip
CPP=cpp
PYTHON=python3

# Source files
src-y =
deployer-y =
dirs-y = src

# Default compiler flags
cc-option=$(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`" \
    ; then echo "$(2)"; else echo "$(3)"; fi ;)

CFLAGS := -I$(OUT) -Isrc -I$(OUT)board-generic/ -std=gnu11 -Os -MD \
    -Wall -Wold-style-definition $(call cc-option,$(CC),-Wtype-limits,) \
    -ffunction-sections -fdata-sections -fno-delete-null-pointer-checks
CFLAGS += -flto -fwhole-program -fno-use-linker-plugin -ggdb3

OBJS_canboot.elf = $(patsubst %.c, $(OUT)src/%.o,$(src-y))
OBJS_canboot.elf += $(OUT)compile_time_request.o
CFLAGS_canboot.elf = $(CFLAGS) -Wl,--gc-sections

OBJS_deployer.elf = $(patsubst %.c, $(OUT)src/%.o,$(deployer-y))
OBJS_deployer.elf += $(OUT)deployer_ctr.o $(OUT)canboot_payload.o
CFLAGS_deployer.elf = $(CFLAGS) -Wl,--gc-sections

BUILDBINARY_FLAGS =

CPPFLAGS = -I$(OUT) -P -MD -MT $@

# Default targets
target-y := $(OUT)canboot.elf $(OUT)canboot.bin

all:

# Run with "make V=1" to see the actual compile commands
ifdef V
Q=
else
Q=@
MAKEFLAGS += --no-print-directory
endif

# Include board specific makefile
include src/Makefile
-include src/$(patsubst "%",%,$(CONFIG_BOARD_DIRECTORY))/Makefile

################ Main build rules

$(OUT)%.o: %.c $(OUT)autoconf.h
	@echo "  Compiling $@"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(OUT)%.ld: %.lds.S $(OUT)autoconf.h
	@echo "  Preprocessing $@"
	$(Q)$(CPP) -I$(OUT) -P -MD -MT $@ $< -o $@

$(OUT)canboot.elf: $(OBJS_canboot.elf)
	@echo "  Linking $@"
	$(Q)$(CC) $(OBJS_canboot.elf) $(CFLAGS_canboot.elf) -o $@
	$(Q)scripts/check-gcc.sh $@ $(OUT)compile_time_request.o

$(OUT)canboot.bin: $(OUT)canboot.elf ./scripts/buildbinary.py
	@echo "  Creating bin file $@"
	$(Q)$(OBJCOPY) -O binary $< $(OUT)canboot.work
	$(Q)$(PYTHON) ./scripts/buildbinary.py -b $(CONFIG_FLASH_START) -s $(CONFIG_LAUNCH_APP_ADDRESS) $(BUILDBINARY_FLAGS) $(OUT)canboot.work -c $(OUT)canboot_payload.c $@

$(OUT)canboot_payload.o: $(OUT)canboot.bin
	@echo "  Compiling $@"
	$(Q)$(CC) $(CFLAGS) -c $(OUT)canboot_payload.c -o $@

################ CanBoot "deployer" build rules

target-$(CONFIG_BUILD_DEPLOYER) += $(OUT)deployer.elf $(OUT)deployer.bin

$(OUT)deployer.elf: $(OBJS_deployer.elf) $(OUT)canboot.bin
	@echo "  Linking $@"
	$(Q)$(CC) $(OBJS_deployer.elf) $(CFLAGS_deployer.elf) -o $@

$(OUT)deployer.bin: $(OUT)deployer.elf
	@echo "  Creating hex file $@"
	$(Q)$(OBJCOPY) -O binary $< $@

################ Compile time requests

$(OUT)%.o.ctr: $(OUT)%.o
	$(Q)$(OBJCOPY) -j '.compile_time_request' -O binary $^ $@

$(OUT)compile_time_request.o: $(patsubst %.c, $(OUT)src/%.o.ctr,$(src-y)) ./scripts/buildcommands.py
	@echo "  Building $@"
	$(Q)cat $(patsubst %.c, $(OUT)src/%.o.ctr,$(src-y)) | tr -s '\0' '\n' > $(OUT)compile_time_request.txt
	$(Q)$(PYTHON) ./scripts/buildcommands.py $(OUT)compile_time_request.txt $(OUT)compile_time_request.c
	$(Q)$(CC) $(CFLAGS) -c $(OUT)compile_time_request.c -o $@

$(OUT)deployer_ctr.o: $(patsubst %.c, $(OUT)src/%.o.ctr,$(deployer-y)) ./scripts/buildcommands.py
	@echo "  Building $@"
	$(Q)cat $(patsubst %.c, $(OUT)src/%.o.ctr,$(deployer-y)) | tr -s '\0' '\n' > $(OUT)deployer_ctr.txt
	$(Q)$(PYTHON) ./scripts/buildcommands.py $(OUT)deployer_ctr.txt $(OUT)deployer_ctr.c
	$(Q)$(CC) $(CFLAGS) -c $(OUT)deployer_ctr.c -o $@

################ Auto generation of "board/" include file link

create-board-link:
	@echo "  Creating symbolic link $(OUT)board"
	$(Q)mkdir -p $(addprefix $(OUT), $(dirs-y))
	$(Q)rm -f $(OUT)*.d $(patsubst %,$(OUT)%/*.d,$(dirs-y))
	$(Q)rm -f $(OUT)board
	$(Q)ln -sf $(CURDIR)/src/$(CONFIG_BOARD_DIRECTORY) $(OUT)board
	$(Q)mkdir -p $(OUT)board-generic
	$(Q)rm -f $(OUT)board-generic/board
	$(Q)ln -sf $(CURDIR)/src/generic $(OUT)board-generic/board

# Hack to rebuild OUT directory and reload make dependencies on Kconfig change
$(OUT)board-link: $(KCONFIG_CONFIG)
	$(Q)mkdir -p $(OUT)
	$(Q)echo "# Makefile board-link rule" > $@
	$(Q)$(MAKE) create-board-link
include $(OUT)board-link

################ Kconfig rules

$(OUT)autoconf.h: $(KCONFIG_CONFIG)
	@echo "  Building $@"
	$(Q)mkdir -p $(OUT)
	$(Q) KCONFIG_AUTOHEADER=$@ $(PYTHON) lib/kconfiglib/genconfig.py src/Kconfig

$(KCONFIG_CONFIG) olddefconfig: src/Kconfig
	$(Q)$(PYTHON) lib/kconfiglib/olddefconfig.py src/Kconfig

menuconfig:
	$(Q)$(PYTHON) lib/kconfiglib/menuconfig.py src/Kconfig

################ Generic rules

# Make definitions
.PHONY : all clean distclean olddefconfig menuconfig create-board-link FORCE
.DELETE_ON_ERROR:

all: $(target-y)

clean:
	$(Q)rm -rf $(OUT)

distclean: clean
	$(Q)rm -f .config .config.old

-include $(OUT)*.d $(patsubst %,$(OUT)%/*.d,$(dirs-y))
