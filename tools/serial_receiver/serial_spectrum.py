#!/usr/bin/env python3
"""Live spectrum analyzer for int16 audio samples streamed from the STM32 over UART.

A producer thread receives decoded samples, while the main thread transforms
the newest FFT window and redraws its magnitude spectrum.
"""

import argparse
import queue
import sys
import threading
from collections import deque

import matplotlib.pyplot as plt
import numpy as np
import serial
from matplotlib.animation import FuncAnimation

from serial_receiver import read_frames


INT16_FULL_SCALE = float(-np.iinfo(np.int16).min)


def receive_samples(
        ser: serial.Serial,
        out_queue: "queue.Queue[int]",
        stop_event: threading.Event) -> None:
    """Decode UART frames and place their samples into a queue."""
    try:
        for values in read_frames(ser):
            if stop_event.is_set():
                return
            for value in values:
                try:
                    out_queue.put_nowait(value)
                except queue.Full:
                    pass
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
    finally:
        stop_event.set()


def calculate_spectrum(
        samples: np.ndarray, sample_rate: float) -> tuple[np.ndarray, np.ndarray]:
    """Return positive-frequency bins and their magnitudes in dBFS."""
    if samples.ndim != 1 or samples.size < 4:
        raise ValueError("At least four one-dimensional samples are required")

    window = np.hanning(samples.size)
    centered_samples = samples.astype(np.float64) - np.mean(samples)
    magnitudes = np.abs(np.fft.rfft(centered_samples * window))
    magnitudes *= 2.0 / window.sum()
    magnitudes[0] /= 2.0
    if samples.size % 2 == 0:
        magnitudes[-1] /= 2.0

    frequencies = np.fft.rfftfreq(samples.size, d=1.0 / sample_rate)
    magnitudes_dbfs = 20.0 * np.log10(
        np.maximum(magnitudes / INT16_FULL_SCALE, np.finfo(np.float64).tiny))
    return frequencies, magnitudes_dbfs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial device path")
    parser.add_argument("--baud", type=int, default=1500000, help="Baud rate")
    parser.add_argument("--sample-rate", type=float, default=48000.0, help="Source sample rate in Hz")
    parser.add_argument("--fft-size", type=int, default=4096, help="Samples per spectrum")
    parser.add_argument("--fps", type=float, default=20.0, help="Plot redraw rate")
    parser.add_argument("--db-floor", type=float, default=-120.0, help="Lower y-axis limit in dBFS")
    args = parser.parse_args()

    if args.sample_rate <= 0:
        parser.error("--sample-rate must be positive")
    if args.fft_size < 4:
        parser.error("--fft-size must be at least 4")
    if args.fps <= 0:
        parser.error("--fps must be positive")
    if args.db_floor >= 0:
        parser.error("--db-floor must be negative")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Error opening {args.port}: {exc}", file=sys.stderr)
        return 1

    data_queue: "queue.Queue[int]" = queue.Queue(maxsize=args.fft_size * 4)
    stop_event = threading.Event()
    receiver_thread = threading.Thread(
        target=receive_samples, args=(ser, data_queue, stop_event), daemon=True)
    receiver_thread.start()

    samples: deque[int] = deque(maxlen=args.fft_size)
    frequencies = np.fft.rfftfreq(args.fft_size, d=1.0 / args.sample_rate)

    fig, ax = plt.subplots()
    line, = ax.plot([], [], lw=0.75)
    ax.set_xlim(0, args.sample_rate / 2.0)
    ax.set_ylim(args.db_floor, 0)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("magnitude (dBFS)")
    ax.set_title(
        f"Audio spectrum ({args.fft_size} samples, "
        f"{args.sample_rate / args.fft_size:.1f} Hz resolution)")
    ax.grid(True, alpha=0.3)

    def on_close(_event):
        stop_event.set()

    fig.canvas.mpl_connect("close_event", on_close)

    def update(_frame):
        try:
            while True:
                samples.append(data_queue.get_nowait())
        except queue.Empty:
            pass

        if len(samples) < args.fft_size:
            return (line,)

        spectrum_frequencies, magnitudes_dbfs = calculate_spectrum(
            np.asarray(samples), args.sample_rate)
        line.set_data(spectrum_frequencies, magnitudes_dbfs)
        return (line,)

    animation = FuncAnimation(
        fig, update, interval=1000.0 / args.fps, blit=False, cache_frame_data=False)
    print(
        f"Listening on {args.port} @ {args.baud} baud, "
        f"analyzing {args.fft_size}-sample windows (close window to stop)")

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        ser.close()
        del animation

    return 0


if __name__ == "__main__":
    sys.exit(main())