#!/usr/bin/env python3
"""Reads UART data from an STM32 board over /dev/ttyACM0 and prints it to the console."""

import argparse
import struct
import sys

import serial

INT32_SIZE = 4
INT32_FORMAT = "<i"  # little-endian signed 32-bit, matches Cortex-M7 byte order

# Must match pp_uart.c: [4 sync bytes][1 count byte][count * int32][1 checksum byte].
SYNC_MARKER = bytes((0xAA, 0x55, 0xA5, 0x5A))


def read_frames(ser: serial.Serial):
    """Yield tuples of int32 values, one tuple per validated UART frame."""
    buffer = bytearray()
    while True:
        buffer += ser.read(max(1, ser.in_waiting))

        marker_pos = buffer.find(SYNC_MARKER)
        if marker_pos < 0:
            # keep only a tail that could still be a partial marker
            del buffer[: max(0, len(buffer) - (len(SYNC_MARKER) - 1))]
            continue
        del buffer[:marker_pos]  # drop any junk before the marker

        header_size = len(SYNC_MARKER) + 1
        if len(buffer) < header_size:
            continue  # need the count byte too

        count = buffer[len(SYNC_MARKER)]
        frame_size = header_size + count * INT32_SIZE + 1  # +1 checksum byte
        if len(buffer) < frame_size:
            continue  # wait for the rest of the payload

        payload = buffer[len(SYNC_MARKER):frame_size - 1]  # count byte + int32 data
        checksum = buffer[frame_size - 1]
        if sum(payload) & 0xFF != checksum:
            # marker bytes showed up inside earlier payload data by
            # coincidence; it wasn't a real frame start, so keep looking
            del buffer[:1]
            continue

        values = struct.unpack(f"<{count}i", buffer[header_size:frame_size - 1])
        del buffer[:frame_size]
        yield values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial device path")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            print(f"Listening on {args.port} @ {args.baud} baud (Ctrl+C to stop)")
            for values in read_frames(ser):
                for value in values:
                    print(value, flush=True)

    except serial.SerialException as exc:
        print(f"Error opening {args.port}: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
