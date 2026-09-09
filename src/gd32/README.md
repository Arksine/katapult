# GD32 platform layout

Katapult's GD32 implementation follows the same boundary as the Klipper port:

- `src/gd32` contains common runtime code and explicitly prefixed F30x/E23x
  implementations.
- `lib/gd32f30x/include` and `lib/gd32e23x/include` contain vendor device
  headers only.
- GD32F303 never builds through `lib/stm32f1`; register-level similarities are
  not treated as device compatibility.
- F303 and E230 select their own startup, GPIO, timer, serial and flash paths
  from this directory.  There is no secondary `src/gd32e23x` board tree.

Bootloader/application offsets and the physical communication interface remain
Kconfig choices and must match the target board before flashing.
