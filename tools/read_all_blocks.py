#!/usr/bin/env python3
"""Read all accessible MIFARE Classic data blocks through the USB CDC console."""

import argparse
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
    """Read console output through its next prompt, preserving all received bytes."""
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(max(1, min(256, int((deadline - time.monotonic()) * 1000))))
        if chunk:
            data.extend(chunk)
            if data.endswith(PROMPT):
                return bytes(data)
    raise TimeoutError("Timed out waiting for the device prompt")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB CDC serial port, e.g. /dev/ttyACM0 or COM4")
    parser.add_argument("-o", "--output", help="also save results to this text file")
    parser.add_argument("--baud", type=int, default=115200, help="serial baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=4.0, help="seconds to wait for each command (default: 4)")
    args = parser.parse_args()

    output = open(args.output, "w", encoding="ascii") if args.output else None

    def report(line):
        print(line)
        if output:
            output.write(line + "\n")
            output.flush()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.1) as port:
            # USB CDC may reset/re-enumerate when opened; wait for its startup prompt.
            time.sleep(1.0)
            port.reset_input_buffer()
            port.write(b"scan\r")
            scan = read_until_prompt(port, args.timeout)
            match = SAK_LINE.search(scan)
            if not match:
                raise RuntimeError("Scan failed; present a MIFARE Classic 1K/4K card and retry")
            sak = int(match.group(1), 16)
            if sak == 0x08:
                max_block = 64
            elif sak == 0x18:
                max_block = 256
            else:
                raise RuntimeError(f"Unsupported SAK 0x{sak:02X}; only Classic 1K/4K are supported")

            report(f"# SAK 0x{sak:02X}; reading accessible data blocks (default Key A required)")
            for block in range(1, max_block):
                # Classic 1K: 4 blocks per sector. Classic 4K: sectors 0-31
                # have 4 blocks, sectors 32-39 have 16 blocks.
                trailer = (block % 4 == 3) if block < 128 else ((block - 128) % 16 == 15)
                if trailer:
                    continue
                port.write(f"read {block}\r".encode("ascii"))
                response = read_until_prompt(port, args.timeout)
                data_match = DATA_LINE.search(response)
                if data_match:
                    report(f"{block:03d}: {data_match.group(1).decode('ascii')}")
                else:
                    detail = response.decode("ascii", errors="replace").replace("\r", " ").replace("\n", " ").strip()
                    report(f"{block:03d}: ERROR {detail}")
    except (serial.SerialException, OSError, TimeoutError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    finally:
        if output:
            output.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
