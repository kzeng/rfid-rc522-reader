# Repository Guidelines

## Project Structure

- `src/` contains STM32 firmware, including the RC522 SPI driver, USB CDC console, and command handling.
- `include/` contains project headers and STM32 HAL configuration; `USB_DEVICE/` contains USB CDC support.
- `docs/` contains the Chinese RFID/RC522 introduction and the development log. `README.md` covers wiring, build, and quick command examples.
- `platformio.ini` configures the STM32F103C8T6 Blue Pill target. `.pio/` is generated build output and must not be committed.

## Build and Hardware Workflow

- `pio run` builds the firmware for `bluepill_f103c8`.
- `pio run -t upload` requests a PlatformIO ST-Link upload. If that uploader is unavailable, use OpenOCD with the ST-Link and STM32F1 configs as described in `docs/development-log.md`.
- Hardware validation requires an STM32F103C8T6, RC522 wired to SPI1, USB CDC terminal, and authorized test cards. Verify `status` and `scan` before attempting data-block operations.

## Code Style and Boundaries

Use four spaces in C, descriptive `snake_case` names, and uppercase names for compile-time constants. Keep low-level register/SPI work separate from card commands where practical. Validate card type, block range, and authentication before writes. Never weaken the safeguards protecting manufacturer blocks and sector trailers without documenting the reason and updating the usage guide.

## Testing and Review

There is no automated test suite yet. Every firmware change should at least build with `pio run`; changes to card communication should also be checked on authorized lab cards and documented with the observed serial output. Keep pull requests focused, describe hardware and card types used, list build/verification results, and include relevant terminal output or wiring changes.

## Commits and Configuration

Use short imperative commit subjects, such as `Add UID cascade scanning`. Do not commit `.pio/`, local editor state, credentials, or private card data. Record new pins, commands, assumptions, and known card limitations in the README or `docs/`.
