# Katapult  (formerly known as CanBoot)
 Bootloader for ARM Cortex-M MCUs

 This bootloader was initially designed for CAN nodes to be used with
 [Klipper](https://github.com/Klipper3d/klipper).  The bootloader
 itself makes use of Klipper's hardware abstraction layer, stripped
 down to keep the footprint minimal. In addition to CAN, Katapult now
 supports USB and UART interfaces.

Currently lpc176x, stm32 and rp2040 MCUs are supported.  CAN support is currently
limited to stm32 F-series and rp2040 devices.

Katapult is licensed under the [GNU GPL v3](/LICENSE).

Note: References to CanBoot remain both in the source of this repo and
in related projects such as Klipper.  Some of these must remain indefinitely
for compatibility.  Documentation and source code references that do not break
compatibility will be updated over time.

## Building

Katapult also uses Klipper's build system.  The build is configured
with menuconfig.  The steps to fetch and build are as follows:
```
git clone https://github.com/Arksine/katapult
cd katapult
make menuconfig
make
```

The menuconfig will present the following options:
- `Microcontroller Architecture`: Choose between lpc176x, stm32 and rp2040
- `Processor model`: Options depend on the chosen architecture
- `SD Card Configuration`: See the [SD Card Configuration](#sd-card-configuration)
  section below.
- `Build Katapult deployment application`: See the [deployer](#katapult-deployer)
   section below.
- `Disable SWD at startup`:  This is an option for GigaDevice STM32F103
  clones.  Note that this is untested for the bootloader, GigaDevice clones
  may not work as expected.
- `Clock Reference`: Choose the appropriate setting for your board's crystal
- `Communication interface`:  Choose between CAN, USB, and UART.  Be sure
  to choose the interface with the appropriate pins for your hardware.
- For CAN Interfaces:
  - `CAN bus speed`: Select the appropriate speed for your canbus.
- For Serial (USART) Interfaces:
  - `Baud rate for serial port`:  Select the appropriate baud rate for your
    serial device.
- For USB Interfaces:
  - `USB ids`:  Allows the user to define the USB Vendor ID, Product ID,
    and/or Serial Number.
- `Support bootloader entry on rapid double click of reset button`:  When
  enabled it is possible to enter the bootloader by pressing the reset button
  twice within a 500ms window.
- `Enable bootloader entry on button (or gpio) state`:  Enable to use a gpio
  to enter the bootloader.
  - `Button GPIO Pin`:  The Pin Name of the GPIO to use as a button.  A hardware
  pull-up can be enabled by prefixing a `^`, and a pull-down can be enabled by
  prefixing a `~`.
- `Enable Status Led`: Enables the option to select a status LED gpio.
  - `Status LED GPIO Pin`:  The pin name for your LED.  The Pin can be inverted
    if the LED is on when the pin is low.  For example, the status LED Pin for a
    "blue pill" is !PC13.

Once configured and built flash with a programmer such as an ST-Link.  If you
don't have a programmer available, it should be possible to flash STM32F103
devices via UART and STM32F042/72 devices over DFU.  ST's STM32CubeProgrammer
software can facilitate all of these methods, however there are also other
tools such as `stm32flash` (UART) and `dfu-util` (USB DFU).

NOTE:  Prior to flashing Katapult it is recommended to do a full chip erase.
Doing so allows Katapult to detect that no application is present and enter
the bootloader.  This is required to enter the bootloader if you have not
configured an alternative method of entry.

NOTE RP2040: To flash rp2040 targets mcu should be rebooted in system boot mode
(usually with _BOOT_ button pressed). After that `make flash` command
could be used. You could also use rp2040 specific mass storage device
drag-and-drop method to flash `katapult.uf2` from `out` folder. Flashing Katapult
will erase main application (i.e. klipper), so it should be uploaded
with Katapult again.

## Uploading Klipper
1) Make sure the `klipper` service stopped.
2) Build Klipper with CAN support and with the a bootloader offset matching that
   of the "application offset" in Katapult.
3) Enter the bootloader.  Katapult automatically enters the bootloader if it
   detects that the application area of flash is empty. When upgrading from a
   currently flashed version of Klipper the `flashtool.py` script will command
   USB and CANBus devices to enter the bootloader.  Note that "USB to CAN
   bridge devices" and "UART" devices cannot be auto-detected.  If the device
   is not in bootloader mode it is necessary to first manually request the
   bootloader with the `-r` option, then rerun the script without `-r` to
   perform the upload.  Devices running software other than Klipper will need
   to request the bootloader with their own method.
3) Run the flash script:
   For CAN Devices:
   ```
   cd ~/katapult/scripts
   python3 flashtool.py -i can0 -f ~/klipper/out/klipper.bin -u <uuid>
   ```
   Replace <uuid> with the appropriate uuid for your can device.  If
   the device has not been previously flashed with Klipper, it is possible
   to query the bootloader for the UUID:

   ```
   flashtool.py -i can0 -q
   ```

   **NOTE: A query should only be performed when a single can node is on
   the network.  Attempting to query multiple nodes may result in transmission
   errors that can force a node into a "bus off" state.  When a node enters
   "bus off" it becomes unresponsive.  The node must be reset to recover.**

   For USB/UART devices:
   Before flashing, make sure pyserial is installed.  The command required
   for installation depends on the the linux distribution.  Debian and Ubuntu
   based distros can run the following commands:
   ```
   sudo apt update
   sudo apt install python3-serial
   ```
   ```
   python3 flashtool.py -d <serial device> -b <baud_rate>
   ```
   Replace `<serial_device>` the the path to the USB/UART device you wish to
   flash.  The `<baud_rate>` is only necessary for UART devices, and defaults
   to 250000 baud if omitted.

## Flash Tool usage

Run `scripts/flashtool.py -h` to display help:

```
usage: flashtool.py [-h] [-d <serial device>] [-b <baud rate>] [-i <can interface>]
                    [-f <klipper.bin>] [-u <uuid>] [-q] [-v] [-r]

Katapult Flash Tool

options:
  -h, --help            show this help message and exit
  -d <serial device>, --device <serial device>
                        Serial Device
  -b <baud rate>, --baud <baud rate>
                        Serial baud rate
  -i <can interface>, --interface <can interface>
                        Can Interface
  -f <klipper.bin>, --firmware <klipper.bin>
                        Path to Klipper firmware file
  -u <uuid>, --uuid <uuid>
                        Can device uuid
  -q, --query           Query Bootloader Device IDs (CANBus only)
  -v, --verbose         Enable verbose responses
  -r, --request-bootloader
                        Requests the bootloader and exits
  -s, --status          Connect to bootloader and print status
```

### Can Programming

The `-i` option defaults to `can0` if omitted.  The `uuid` option is required
for programming.  The `-q` option will query the CAN interface for unassigned
nodes, returning their UUIDs.

The `-f` option defaults to `~/klipper/out/klipper.bin` when omitted.

### Serial Programming (USB or UART)

The `-d` option is required.  The `-b` option defaults to `250000` if omitted.

If `flashtool` detects that the device is connected via USB, it will check
the USB IDs to determine if its currently running Klipper.  If so, the
`flashtool` will attempt to request the bootloader, waiting until it detects
Katapult.

### Request Bootloader and Exit

When the `-r` option is supplied `flashtool` request that the MCU enter
the bootloader.  Flashtool will then immediately exit, no attempt will be
made to upload a new binary over the canbus.

This is particularly useful for Klipper devices configured in "USB to CAN
bridge mode". These devices upload firmware using DFU and/or Katapult-USB. This
option allows the user to enter the bootloader without physical access to the
board, then use the appropriate tool (`dfu-util` or `flashtool.py -d`) to
upload the new binary.

Additionally, the `-r` option can be used with devices connected to the host
over a UART connection to request Klipper's bootloader.

## SD Card Programming

Katapult offers optional SD Card support.  When configured, an SD Card
may be used to upload firmware in addition to the primary interface
(CANBus, USB, or UART).  This is useful for bench updates or as a
fallback when it isn't possible to flash using the primary interface.

Unlike most stock bootloaders, Katapult does not initialize the SD Card
and check for a new firmware file on every restart. The user must explicitly
enter the bootloader to initiate an SD Card upload.  It is recommended to
either configure the "double reset" or a GPIO Button when enabling SD Card
programming so bootloader entry is possible without using `flashtool.py`.

Upon entering the bootloader, Katapult will look for a new `firmware file`,
and if detected it will begin writing.  If a `status led` is configured it
will blink rapidly during the programming procedure.  After successful completion
Katapult will rename the firmware file's extension to `.cur`, reset the MCU,
and jump to the application.  If Katapult encounters an error during programming
it will attempt to rename the firmware file with a `.err` extension, then drop
to command mode.

If the `firmware file` does not exist or if there is an error initializing the
SD Card, Katapult will enter command mode.  In this mode the `status led`
will blink slowly, and Katapult is read to accept commands over the primary
interface, such as those issued by `flashtool.py`.

Katapult supports SPI and Software SPI SD Card interfaces for all supported
micro-controllers. SDIO is available for STM32F4 series MCUs.

### SD Card Configuration

The following Options are available in the `SD Card Configuration` menu:
- `SD Card Interface`:  The interface used to communicate with the SD Card.
  Choices are `disabled`, hardware SPI modes, Software SPI, and SDIO modes.
- `SD Card Software SPI Pins`:  Only available when `Software SPI` is selected
   as the `SD Card Interface`.  Must be three comma separated pins; MISO, MOSI,
   and SCLK.
- `SD Card SPI CS Pin`:  The Chip Select pin when one of the SPI modes (including
  Software SPI) is selected.
- `Firmware file name`:  The name of the firmware file that will trigger an upload.
  Defaults to `firmware.bin`. **NOTE:**  Avoid using a `.cur` or `.err` extensions
  when customizing the file name.  Doing so will result in Katapult deleting the
  firmware file after an upload rather than renaming it.
- `Enable Long File Name Support`:  When enabled, the firmware file name supports
  FAT long file names.  This allows the `Firmware file name` to have a base name
  longer than 8 characters and extensions longer than 3 characters.  This option
  increases the size of the binary by roughly 2.5 KiB.

For most configurations the total size of Katapult's binary should be under 16KiB
when SD Card support is configured, thus a 16 KB `Application start offset` should be sufficient.  One exception to this is the `RP2040` when the primary interface
is `CANBus` and `Long File Name Support` is enabled.  This will result in a binary
larger than 16 KiB.  It should be noted that `Klipper` currently only supports a
16 KiB bootloader offset for the `RP2040`.

### Troubleshooting

As mentioned above, Katapult will enter command mode if it encounters an error
during initialization or programming.  When in command mode it is possible
to use the flashtool's  `-s` option to query Katapult's status which includes
SD Card status:

```
./scripts/flashtool.py -s -u aabbccddeeff
Connecting to CAN UUID aabbccddeeff on interface can0
Resetting all bootloader node IDs...
Detected Klipper binary version v0.12.0-302-g87ac69363, MCU: stm32f103xe
Attempting to connect to bootloader
Katapult Connected
Software Version: v0.0.1-95-g2d7bd0c
Protocol Version: 1.1.0
Block Size: 64 bytes
Application Start: 0x8004000
MCU type: stm32f103xe
Verifying canbus connection

*** SD Card Status ***
Detected SD Card Interface: HARDWARE_SPI
Last SD Flash State: NO_DISK
SD Flags: DEINITIALIZED
Last Error: NO_IDLE_STATE
Status Request Complete
```


## Katapult Deployer

**WARNING**: Make absolutely sure your Katapult build configuration is
correct before uploading the deployer.  Overwriting your existing
bootloader with an incorrectly configured build will brick your device
and require a programmer to recover.

The Katapult deployer allows a user to overwrite their existing bootloader
with Katapult, allowing modification and updates without a programmer.  It
is *strongly* recommended that an alternate recovery (programmer, DFU, etc)
method is available in the event that something goes wrong during deployment.
If coming from a stock bootloader it is also recommended that the user create
a backup before proceeding.

To build the deployer set the `Build Katapult deployment application` option
in the menuconfig to your existing bootloader offset.  The additional settings
apply to the Katapult binary, configure them just as you would without the
deployer.  Save your settings and build with `make`.

This will result in an additional binary in the `out` folder, `deployer.bin`.
Flash `deployer.bin` with your existing bootloader (SD Card, HID, an older
version of Katapult, etc).  Once complete, the deployer should reset the
device and enter Katapult.  Now you are ready to use Katapult to flash an
application, such as Klipper.

## Contributing

Katapult is effectively a fork of Klipper's MCU source.  As such, it is appropriate
to retain similar contributing guidelines as Klipper.  Commits should be formatted
as follows:

```
filename: brief description of commit

More detailed explanation of the change if required.

Signed-off-by: Your Name <your email address>
```

All commits must be signed off with a real name and email address indicating
acceptance of the
[developer certificate of origin](/developer-certificate-of-origin).

## Notes
- It is recommended to use a USB-CAN device flashed with
  [candlelight](https://github.com/candle-usb/candleLight_fw), such as a
  [Canable](https://canable.io/). Alternatively, a device that supports Klipper's
  USB-CAN bridge mode works well.
- The BTT U2C v2.1 CAN device requires the latest firmware.  The binary can be found
  [in the U2C repo](https://github.com/bigtreetech/U2C/tree/master/firmware) and the
  source can be found at [BTT's candlelight fork](https://github.com/bigtreetech/candleLight_fw/commits/stm32g0_support).
- If using a MCP2515 CAN device (ie: Waveshare RS485 CAN HAT) it is possible
  that packets will be dropped when reading flash back from the node during
  the verification process.  That said, I have successfully tested the 12 MHz
  Crystal variant with the
  [recommended settings](https://www.waveshare.com/wiki/RS485_CAN_HAT).
- Details on the protocol used to communicate with the bootloader may
  be found in [protocol.md](protocol.md).
