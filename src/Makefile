# Main code build rules

src-y += sched.c bootentry.c command.c flashcmd.c initial_pins.c
src-$(CONFIG_ENABLE_LED) += led.c
src-$(CONFIG_ENABLE_SDCARD) += ../lib/fatfs/ff.c sdcard.c
src-$(CONFIG_FATFS_ENABLE_LFN) += ../lib/fatfs/ffunicode.c
src-$(CONFIG_SDCARD_SPI) += sdcard_spi.c
src-$(CONFIG_SDCARD_SOFTWARE_SPI) += spi_software.c sdcard_spi.c

dirs-$(CONFIG_ENABLE_SDCARD) += lib/fatfs
CFLAGS.fatfs-$(CONFIG_ENABLE_SDCARD) := -Ilib/fatfs
CFLAGS.fatfs-$(CONFIG_FATFS_ENABLE_LFN) += -DFF_USE_LFN
CFLAGS += $(CFLAGS.fatfs-y)

deployer-y += deployer.c
