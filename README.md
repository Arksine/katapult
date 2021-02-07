# CanBoot
 Can Bootloader for STM32F103 MCUs

 This bootloader is designed for CAN nodes to be used with
 [Klipper](https://github.com/KevinOConnor/klipper).  The bootloader
 itself makes use of Klipper's hardware abstraction layer, stripped
 down to keep the footprint minimal.  Currently the bootloader
 reserves 8 KiB of space, however it occupies less than 4 KiB.


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
- `Processor model`: Currently only STM32F203 is supported
- `Clock Reference`: Choose the appropriate setting for your board's crystal
- `Flash Page Size`: The STM32F103 comes in multiple variants.  The low
  density models have a 1 KiB page size, others a 2KiB page size.  Choose
  the appropriate selection for your model
- `CAN bus speed`: Select the appropriate speed for your setup
- `Enable Status Led`: If enabled, the option to set the Status LED Pin is
  made available
- `Status LED GPIO Pin`:  The pin name for your LED.  The Pin can be inverted
  if the LED is on when the pin is low.  For example, the status LED Pin for a
  "blue pill" is !PC13.

Once configured and built flash with a programmer such as an ST-Link

## Uploading a Program

1) Make sure [CanSerial](https://github.com/bondus/CanSerial) is installed and
   enabled. Alternatively one can use the `pycanserial.py` script in this repo,
   however it requries Python 3.7+.  It also lacks the "reset" feature of
   CanSerial, so for the moment I recommend using C version linked.
2) Enter the bootloader.  This may be accomplished through "double tapping" the
   Reset button.  The second reset should be within 1.5 seconds of the first.
   If the Status LED is set it will blink every second when the device has
   entered the bootloader.  The bootloader should connect to CanSerial, and
   CanSerial should create a PTY for it in the `/tmp` directory.
3) Build Klipper with CAN support and with the "8KiB" bootloader setting enabled.
4) Run the flash script:
   ```
   cd ~/CanBoot
   ~/klippy-env/bin/python flash_can.py /tmp/ttyCANxxxxxxxx ~/klipper/out/klipper.bin
   ```
   Replace `/tmp/CANxxxxxxxx` with the appropriate symlink for your device.

## Notes
- This is preview code and will almost certainly change.  Specifically, it is
  anticipated that the flash_can script will not use CanSerial in the future,
  but instead connect directly to the CAN socket and issue commands to the
  bootloader.  Likewise it is expected that it will be possible to enter the
  booltoader via command from Klipper.  The IDs used to communicate with the
  bootloader may also change (0x321, 0x322, 0x323).
- If using a MCP2515 Can Device it is likely that packets will be dropped when
  reading flash back from the node during the verification process.  Reducing
  SPI speed may help with this, in my testing the process could not complete
  at 10MHz.  If possible use a USB can device flashed with
  [candlelight](https://github.com/candle-usb/candleLight_fw).

