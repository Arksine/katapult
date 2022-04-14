# CanBoot
 Can Bootloader for STM32F103 MCUs

 This bootloader is designed for CAN nodes to be used with
 [Klipper](https://github.com/KevinOConnor/klipper).  The bootloader
 itself makes use of Klipper's hardware abstraction layer, stripped
 down to keep the footprint minimal.  Currently the bootloader
 reserves 8 KiB of space.


## Building

CanBoot also uses Klipper's build system.  The build is configured
with menuconfig.  The steps to fetch and build are as follows:
```
git clone https://github.com/Arksine/CanBoot
cd CanBoot
make menuconfig
make
```

The menuconfig will present the following options:
- `Processor model`: Currently STM32F042, STMF072, and STM32F103 models are
 supported.
- `Disable SWD at startup`:  This is an option for GigaDevice STM32F103
  clones.  Note that these untested on this bootloader. This option only
  present for "STM32F103" models,
- `Clock Reference`: Choose the appropriate setting for your board's crystal
- `CAN pins`: Allows the user to choose which pins are wired to CAN.
- `Flash Page Size`: The STM32F103 comes in multiple variants.  The low/medium
  density models have a 1 KiB page size, others a 2KiB page size.  Choose
  the appropriate selection for your model. This option only present for
  "STM32F103" models,
- `CAN bus speed`: Select the appropriate speed for your canbus.
- `Enable Status Led`: Enables the option to select a status LED gpio.
- `Status LED GPIO Pin`:  The pin name for your LED.  The Pin can be inverted
  if the LED is on when the pin is low.  For example, the status LED Pin for a
  "blue pill" is !PC13.

Once configured and built flash with a programmer such as an ST-Link.  If you
don't have a programmer available, it should be possible to flash STM32F103
devices via UART and STM32F042/72 devices over DFU.  ST's STM32CubeProgrammer
software can facilitate all of these methods, however there are also other
tools such as `stm32flash` (UART) and `dfu-util` (USB DFU).

## Uploading a Program
1) Build Klipper with CAN support and with the "8KiB" bootloader setting enabled.
2) Enter the bootloader.  This may be accomplished through "double tapping" the
   Reset button.  The second reset should be within 1.5 seconds of the first.
   If the Status LED is set it will blink every second when the device has
   entered the bootloader.  A future patch to Klipper should make it possible
   for `flash_can.py` to direct the device to enter the bootloader.
3) Run the flash script:
   ```
   cd ~/CanBoot
   python3 flash_can.py -i can0 -f ~/klipper/out/klipper.bin -u <uuid>
   ```
   Replace <uuid> with the appropriate uuid for your can device.  If
   the device has not been previouisly flashed with Klipper, it is possible
   to query the bootloader for the UUID:

   ```
   flash_can.py -i can0 -q
   ```

## FlashCan usage

Running `flash_can.py -h` to display help:

```
usage: flash_can.py [-h] [-i <can interface>] [-f <klipper.bin>] [-u <uuid>]
                    [-q]

Can Bootloader Flash Utility

optional arguments:
  -h, --help            show this help message and exit
  -i <can interface>, --interface <can interface>
                        Can Interface
  -f <klipper.bin>, --firmware <klipper.bin>
                        Path to Klipper firmware file
  -u <uuid>, --uuid <uuid>
                        Can device uuid
  -q, --query           Query Bootloader Device IDs
```

The `interface` option defaults to `can0` if omitted.  The `firmware` option
defaults to `~/klipper/out/klipper.bin`.  The `uuid` must be specified unless
the user is running a query with `-q`.

## Notes
- If using a MCP2515 Can Device it is likely that packets will be dropped when
  reading flash back from the node during the verification process.  Reducing
  SPI speed may help with this, in my testing the process could not complete
  at 10MHz.  If possible use a USB can device flashed with
  [candlelight](https://github.com/candle-usb/candleLight_fw).

