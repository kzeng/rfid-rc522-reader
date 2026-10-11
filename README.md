# STM32F103C8T6 + RC522

This project provides an SPI1 RC522 reader with a USB CDC command console. Scanning supports ISO14443A cards with 4-, 7-, and 10-byte UIDs. Data-block operations are limited to MIFARE Classic 1K/4K cards using the factory default Key A.

## Wiring

| RC522 | Blue Pill |
|---|---|
| SDA / SS | PA4 |
| SCK | PA5 |
| MISO | PA6 |
| MOSI | PA7 |
| RST | PB0 |
| 3.3V | 3.3V |
| GND | GND |
| IRQ | Not connected |

For the three-pin passive buzzer module, connect `S` to PB1/TIM3_CH4, `-` to GND, and `+` to the voltage specified for the module. Its signal input must accept 3.3V logic. PB1 generates an approximately 2 kHz, 50% duty tone. A bare two-pin buzzer may need a transistor driver; do not assume it can be driven directly from a GPIO. Keep the RC522 at 3.3V; do not power it from 5V.

The Blue Pill micro-USB port provides the USB CDC command console. Connect ST-Link to the board's labeled `SWDIO`, `SWCLK`, and `GND` pins for flashing/debugging; `NRST` is optional. These SWD connections are separate from the RC522 SPI wiring. See [the detailed wiring guide](docs/rfid-basics-and-usage.md#本项目硬件与连接).

## Build and flash

```sh
pio run
pio run -t upload
```

Open the new USB serial device in a terminal at 115200 baud, 8 data bits, no parity, 1 stop bit. USB CDC ignores the selected baud rate, but terminal applications commonly require one.

## Console commands

Type `help` for the on-device menu. Common commands:

```text
status
scan
read 4
write 4 00112233445566778899AABBCCDDEEFF
erase 4 YES
copy 4 5
```

Blocks are decimal. Read, write, erase, and copy require a MIFARE Classic 1K/4K card and its factory Key A (`FF FF FF FF FF FF`). Writes and erase alter the presented card immediately. Erase fills the selected block with zeros. Copy reads and writes between two data blocks in the same sector on the presented card (for example, `copy 4 5`).

Successful scan/read gives one short beep, write/copy gives two, erase gives two short and one longer beep, and an operation error gives one long beep.

The firmware refuses block 0 (UID/manufacturer data) and sector trailers (keys/access bits). It does not write sector keys, copy UIDs, handle non-Classic card memory, or copy an entire card. Do not use these commands on cards containing data you need.

To read all accessible data blocks from a host computer, install `pyserial` and run:

```sh
python -m pip install pyserial
python tools/read_all_blocks.py /dev/ttyACM0 --output card-dump.txt
```

On Windows, pass the serial port name such as `COM4`. The script scans the card to identify Classic 1K/4K, then reads ordinary data blocks using the firmware console. It skips block 0 and every sector trailer; each block must authenticate with the factory default Key A. Keep dump files private if the card contains sensitive data.

To copy and verify all ordinary data blocks between two same-size test cards:

```sh
python tools/card_transfer_verify.py /dev/ttyACM0 --dump source-card.json
```

Follow the prompts to present the source, target, then target again for verification. The script requires the target's explicit `COPY` confirmation before writing. Both cards must be MIFARE Classic 1K or both 4K, and every copied sector must use the factory default Key A. This overwrites ordinary data blocks on the target; block 0 and sector trailers are skipped. Use only cards you own or are authorized to modify, and keep dump files private.

See [the development log](docs/development-log.md) for implementation steps and hardware/build notes.

For a Chinese introduction to RFID/RC522 concepts, card compatibility, the command workflow, and firmware interfaces, see [RFID basics and usage](docs/rfid-basics-and-usage.md).
