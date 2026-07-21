# Fork Changes

This file tracks changes made in this fork on top of upstream
[Katapult](https://github.com/Arksine/katapult), for reference and as a
starting point if/when a change is upstreamed via PR.

## Recover from an interrupted firmware flash

Two related, backward-compatible additions so a failed or interrupted flash
update can't leave the device stuck running (or unable to boot) a corrupted
application.

### 1. Configurable auto-jump timeout

- New Kconfig option `CONFIG_AUTO_JUMP_TIMEOUT` (default: 10s).
- Previously, `bootentry_check()` jumped straight to a valid application at
  boot with no window to intervene. It now stays in the bootloader for
  `CONFIG_AUTO_JUMP_TIMEOUT` seconds instead, so the device is reachable for
  flashing without a manual bootloader-entry step, then auto-jumps if
  nothing happens (`auto_jump_task`, `bootentry.c`).
- If a flash session actually starts within that window, the timeout is
  dropped entirely in favor of the existing `CMD_COMPLETE` handshake
  (`complete_task` in `flashcmd.c`) — the bootloader waits for the host's
  explicit completion signal instead of guessing from elapsed time, so
  upload + verification can take as long as needed.

### 2. Flash-completion flag (STM32 only)

- New Kconfig option `CONFIG_STM32_RESERVE_FLASH_FLAG` (default: off), which
  reserves one flash erase-page (`CONFIG_FLASH_FLAG_SIZE`) immediately
  before the application start (`armcm_link.lds.S`). The whole page has to
  be reserved because flash can only be erased a full page at a time (2KB
  here); only the first 4 bytes are ever used for the magic value, the rest
  sits unused.
- `flashcmd.c` erases this flag at the start of a flash session and writes
  a magic value (`stm32/flash.c: flash_flag_*`) only after a fully
  successful transfer.
- `bootentry_check()` now also requires this flag before treating the
  application as bootable — if a flash session gets interrupted (power
  loss, dropped connection), the flag is absent, so the bootloader stays
  resident instead of jumping into a partially-written application.
- Fully opt-in: `CONFIG_FLASH_FLAG_SIZE` defaults to `0`, which compiles the
  feature out completely (no layout or behavior change) unless a board
  enables `STM32_RESERVE_FLASH_FLAG`.

### Files changed
- `src/Kconfig`, `src/stm32/Kconfig` — new options.
- `src/bootentry.c` — auto-jump task, flag-check gating.
- `src/flashcmd.c` — erase/write the flag around a flash session.
- `src/stm32/flash.c`, `src/stm32/flash.h` — flag erase/write/check.
- `src/generic/armcm_link.lds.S` — reserved memory region + linker symbols.
