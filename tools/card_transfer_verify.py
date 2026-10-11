#!/usr/bin/env python3
"""Copy ordinary MIFARE Classic data blocks between cards and verify them."""

import argparse
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    print("Missing dependency: install pyserial with `python -m pip install pyserial`", file=sys.stderr)
    raise SystemExit(2)


PROMPT = b"> "
DATA_LINE = re.compile(rb"(?m)^((?:[0-9A-Fa-f]{2} ){15}[0-9A-Fa-f]{2})\r?$")
SAK_LINE = re.compile(rb"(?m)^.*SAK: 0x([0-9A-Fa-f]{2}).*$")


def read_until_prompt(port, timeout):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        remaining_ms = max(1, int((deadline - time.monotonic()) * 1000))
        chunk = port.read(min(256, remaining_ms))
        if chunk:
            data.extend(chunk)
            if data.endswith(PROMPT):
                return bytes(data)
    raise TimeoutError("Timed out waiting for the device prompt")


def command(port, text, timeout):
    port.write((text + "\r").encode("ascii"))
    return read_until_prompt(port, timeout)


def card_info(port, timeout):
    response = command(port, "scan", timeout)
    match = SAK_LINE.search(response)
    if not match:
        raise RuntimeError("Card scan failed; present a MIFARE Classic 1K/4K card")
    sak = int(match.group(1), 16)
    if sak == 0x08:
        return sak, 64
    if sak == 0x18:
        return sak, 256
    raise RuntimeError(f"Unsupported card SAK 0x{sak:02X}; only Classic 1K/4K are supported")


def data_blocks(max_blocks):
    for block in range(max_blocks):
        if block != 0 and not is_trailer(block):
            yield block


def is_trailer(block):
    if block < 128:
        return block % 4 == 3
    return (block - 128) % 16 == 15


def read_card(port, max_blocks, timeout, label):
    values = {}
    blocks = list(data_blocks(max_blocks))
    print(f"Reading {label}: {len(blocks)} ordinary blocks")
    skipped = "-- " * 15 + "--"
    for block in range(max_blocks):
        if block == 0 or is_trailer(block):
            print(f"  block {block:03d}: {skipped}")
            continue
        response = command(port, f"read {block}", timeout)
        match = DATA_LINE.search(response)
        if not match:
            detail = response.decode("ascii", errors="replace").replace("\r", " ").replace("\n", " ").strip()
            raise RuntimeError(f"Could not read {label} block {block}: {detail}")
        values[block] = bytes.fromhex(match.group(1).decode("ascii").replace(" ", ""))
        print(f"  block {block:03d}: {values[block].hex(' ').upper()}")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, e.g. /dev/ttyACM0 or COM4")
    parser.add_argument("--baud", type=int, default=115200, help="serial baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=5.0, help="seconds per command (default: 5)")
    parser.add_argument("--dump", help="save the source card's readable blocks as JSON before writing")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1) as port:
            time.sleep(1.0)
            port.reset_input_buffer()

            input("Present the SOURCE card, then press Enter...")
            source_sak, source_size = card_info(port, args.timeout)
            print(f"Source detected: MIFARE Classic {'1K' if source_size == 64 else '4K'} (SAK 0x{source_sak:02X})")
            source = read_card(port, source_size, args.timeout, "source card")
            if args.dump:
                with open(args.dump, "w", encoding="ascii") as dump_file:
                    json.dump({"sak": source_sak, "blocks": {str(k): v.hex().upper() for k, v in source.items()}}, dump_file, indent=2)
                    dump_file.write("\n")
                print(f"Source dump saved to {args.dump}")

            input("Remove the source card, present the TARGET card, then press Enter...")
            target_sak, target_size = card_info(port, args.timeout)
            print(f"Target detected: MIFARE Classic {'1K' if target_size == 64 else '4K'} (SAK 0x{target_sak:02X})")
            if target_size != source_size:
                raise RuntimeError("Source and target capacities differ; refusing a partial or ambiguous copy")

            confirmation = input(f"This will overwrite {len(source)} ordinary data blocks on the target. Type COPY to continue: ")
            if confirmation != "COPY":
                print("Cancelled; target card was not written.")
                return 1

            blocks = list(source)
            print(f"Writing {len(blocks)} ordinary data blocks to target")
            for index, block in enumerate(blocks, 1):
                response = command(port, f"write {block} {source[block].hex().upper()}", args.timeout)
                if b"Block written." not in response:
                    detail = response.decode("ascii", errors="replace").replace("\r", " ").replace("\n", " ").strip()
                    raise RuntimeError(f"Could not write target block {block}: {detail}")
                if index % 16 == 0 or index == len(blocks):
                    print(f"  {index}/{len(blocks)} blocks written")

            input("Remove the target card, present it again, then press Enter to verify...")
            verify_sak, verify_size = card_info(port, args.timeout)
            if verify_size != source_size:
                raise RuntimeError("Verification card capacity does not match the source")
            actual = read_card(port, verify_size, args.timeout, "target verification")

            mismatches = [(block, source[block], actual.get(block)) for block in source if source[block] != actual.get(block)]
            if mismatches:
                print(f"FAIL: {len(mismatches)} block(s) differ:", file=sys.stderr)
                for block, expected, found in mismatches:
                    print(f"  block {block}: expected {expected.hex().upper()} got {found.hex().upper() if found else 'UNREADABLE'}", file=sys.stderr)
                return 2
            print(f"PASS: all {len(source)} ordinary data blocks match (SAK 0x{verify_sak:02X}).")
            print("Block 0 and sector trailer blocks were skipped and were not copied.")
            return 0
    except (serial.SerialException, OSError, TimeoutError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
