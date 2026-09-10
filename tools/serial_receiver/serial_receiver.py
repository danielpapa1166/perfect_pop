#!/usr/bin/env python3
"""Reads signed integer UART data from an STM32 board and prints it to the console."""

import argparse
import struct
import sys

import serial

INT16_SIZE = 2
INT32_SIZE = 4

# Must match pp_uart.c: [4 sync bytes][1 count byte][integer payload][1 checksum byte].
SYNC_MARKER_INT32 = bytes((0xAA, 0x55, 0xA5, 0x5A))
SYNC_MARKER_INT16 = bytes((0xAA, 0x55, 0xA5, 0x5B))
FRAME_TYPES = {
    SYNC_MARKER_INT32: (INT32_SIZE, "i"),
    SYNC_MARKER_INT16: (INT16_SIZE, "h"),
}


def read_frames(ser: serial.Serial):
    """Yield tuples of signed integer values from validated UART frames."""
    buffer = bytearray()
    while True:
        buffer += ser.read(max(1, ser.in_waiting))

        marker_pos = -1
        frame_type = None
        for marker, candidate_frame_type in FRAME_TYPES.items():
            candidate_pos = buffer.find(marker)
            if candidate_pos >= 0 and (marker_pos < 0 or candidate_pos < marker_pos):
                marker_pos = candidate_pos
                frame_type = candidate_frame_type

        if marker_pos < 0:
            # keep only a tail that could still be a partial marker
            del buffer[: max(0, len(buffer) - (len(SYNC_MARKER_INT32) - 1))]
            continue
        del buffer[:marker_pos]  # drop any junk before the marker

        header_size = len(SYNC_MARKER_INT32) + 1
        if len(buffer) < header_size:
            continue  # need the count byte too

        element_size, format_code = frame_type
        count = buffer[len(SYNC_MARKER_INT32)]
        frame_size = header_size + count * element_size + 1  # +1 checksum byte
        if len(buffer) < frame_size:
            continue  # wait for the rest of the payload

        payload = buffer[len(SYNC_MARKER_INT32):frame_size - 1]  # count byte + integer data
        checksum = buffer[frame_size - 1]
        if sum(payload) & 0xFF != checksum:
            # marker bytes showed up inside earlier payload data by
            # coincidence; it wasn't a real frame start, so keep looking
            del buffer[:1]
            continue

        values = struct.unpack(f"<{count}{format_code}", buffer[header_size:frame_size - 1])
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
