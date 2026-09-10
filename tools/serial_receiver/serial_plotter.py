#!/usr/bin/env python3
"""Live waveform and spectrum plots for int16 audio samples over UART.

A producer thread reads and decodes UART frames. The main thread consumes the
samples to redraw a decimated waveform and a full-rate spectrum in one window.
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
from serial_spectrum import calculate_spectrum


INT16_MIN = np.iinfo(np.int16).min
INT16_MAX = np.iinfo(np.int16).max


def producer(ser: serial.Serial, out_queue: "queue.Queue[tuple[int, int]]",
             stop_event: threading.Event) -> None:
    """Decode UART frames and forward every sample with its source index."""
    sample_index = 0
    try:
        for values in read_frames(ser):
            if stop_event.is_set():
                return
            for value in values:
                try:
                    out_queue.put_nowait((sample_index, value))
                except queue.Full:
                    pass  # consumer is behind; drop this sample
                sample_index += 1
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
    finally:
        stop_event.set()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial device path")
    parser.add_argument("--baud", type=int, default=1500000, help="Baud rate")
    parser.add_argument("--sample-rate", type=float, default=48000.0, help="Source sample rate in Hz")
    parser.add_argument("--window", type=float, default=10.0, help="Sliding window length in seconds")
    parser.add_argument("--decimate", type=int, default=1, help="Plot every Nth sample")
    parser.add_argument("--fft-size", type=int, default=4096, help="Samples per spectrum")
    parser.add_argument("--fps", type=float, default=20.0, help="Plot redraw rate")
    parser.add_argument("--db-floor", type=float, default=-120.0, help="Lower spectrum limit in dBFS")
    args = parser.parse_args()

    if args.sample_rate <= 0:
        parser.error("--sample-rate must be positive")
    if args.window <= 0:
        parser.error("--window must be positive")
    if args.decimate < 1:
        parser.error("--decimate must be at least 1")
    if args.fft_size < 4:
        parser.error("--fft-size must be at least 4")
    if args.fps <= 0:
        parser.error("--fps must be positive")
    if args.db_floor >= 0:
        parser.error("--db-floor must be negative")

    plot_points = max(2, int(args.window * args.sample_rate / args.decimate))
    dt = args.decimate / args.sample_rate  # time between plotted samples

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Error opening {args.port}: {exc}", file=sys.stderr)
        return 1

    data_queue: "queue.Queue[tuple[int, int]]" = queue.Queue(
        maxsize=max(plot_points * 2, args.fft_size * 4))
    stop_event = threading.Event()
    producer_thread = threading.Thread(
        target=producer, args=(ser, data_queue, stop_event), daemon=True)
    producer_thread.start()

    waveform_samples: deque[int] = deque(maxlen=plot_points)
    spectrum_samples: deque[int] = deque(maxlen=args.fft_size)

    fig, (waveform_ax, spectrum_ax) = plt.subplots(
        2, 1, figsize=(10, 8), constrained_layout=True)
    waveform_line, = waveform_ax.plot([], [], lw=0.75)
    waveform_ax.set_xlim(-args.window, 0)
    waveform_ax.set_ylim(INT16_MIN, INT16_MAX)
    waveform_ax.set_xlabel("time (s)")
    waveform_ax.set_ylabel("sample value")
    waveform_ax.set_title(
        f"Audio samples (every {args.decimate}th, "
        f"{args.window:.0f}s window @ {args.sample_rate:.0f} Hz)")
    waveform_ax.grid(True, alpha=0.3)

    spectrum_line, = spectrum_ax.plot([], [], lw=0.75)
    spectrum_ax.set_xlim(0, args.sample_rate / 2.0)
    spectrum_ax.set_ylim(args.db_floor, 0)
    spectrum_ax.set_xlabel("frequency (Hz)")
    spectrum_ax.set_ylabel("magnitude (dBFS)")
    spectrum_ax.set_title(
        f"Audio spectrum ({args.fft_size} samples, "
        f"{args.sample_rate / args.fft_size:.1f} Hz resolution)")
    spectrum_ax.grid(True, alpha=0.3)

    def on_close(_event):
        stop_event.set()

    fig.canvas.mpl_connect("close_event", on_close)

    def update(_frame):
        try:
            while True:
                sample_index, value = data_queue.get_nowait()
                spectrum_samples.append(value)
                if sample_index % args.decimate == 0:
                    waveform_samples.append(value)
        except queue.Empty:
            pass

        if waveform_samples:
            xs = np.arange(-len(waveform_samples) + 1, 1) * dt
            waveform_line.set_data(xs, waveform_samples)

        if len(spectrum_samples) == args.fft_size:
            frequencies, magnitudes_dbfs = calculate_spectrum(
                np.fromiter(spectrum_samples, dtype=np.int16), args.sample_rate)
            spectrum_line.set_data(frequencies, magnitudes_dbfs)

        return waveform_line, spectrum_line

    animation = FuncAnimation(
        fig, update, interval=1000.0 / args.fps, blit=False, cache_frame_data=False)
    print(
        f"Listening on {args.port} @ {args.baud} baud, plotting every "
        f"{args.decimate}th sample and {args.fft_size}-sample spectra "
        "(close window to stop)")

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
