#!/usr/bin/env python3
"""Live waveform and sliding spectrogram plots for int16 audio samples over UART.

A producer thread reads and decodes UART frames. The main thread consumes the
samples to redraw a decimated waveform and a scrolling full-rate spectrogram in
one window.
"""

import argparse
import queue
import sys
import threading
from collections import deque
from dataclasses import dataclass

import matplotlib.pyplot as plt
import numpy as np
import serial
from matplotlib.animation import FuncAnimation

from serial_receiver import read_frames
from serial_spectrum import calculate_spectrum


INT16_MIN = np.iinfo(np.int16).min
INT16_MAX = np.iinfo(np.int16).max
MAX_CAPTURE_BYTES = 64 * 1024 * 1024


@dataclass
class SampleCapture:
    """Fixed-size int16 capture buffer for the initial audio samples."""

    samples: np.ndarray
    count: int = 0
    saved: bool = False

    def append(self, value: int) -> bool:
        if self.count < self.samples.size:
            self.samples[self.count] = value
            self.count += 1
            return self.count == self.samples.size
        return False


def save_capture(capture: SampleCapture, capture_file: str) -> None:
    try:
        np.savetxt(capture_file, capture.samples[:capture.count], fmt="%d")
        capture.saved = True
        print(
            f"Saved {capture.count} of {capture.samples.size} captured "
            f"samples to {capture_file}")
    except OSError as exc:
        print(f"Error saving capture to {capture_file}: {exc}", file=sys.stderr)


def producer(ser: serial.Serial, out_queue: "queue.Queue[tuple[int, int]]",
             stop_event: threading.Event, capture: SampleCapture | None,
             capture_file: str) -> None:
    """Decode UART frames and forward every sample with its source index."""
    sample_index = 0
    try:
        for values in read_frames(ser, stop_event):
            if stop_event.is_set():
                return
            for value in values:
                if stop_event.is_set():
                    return
                if capture is not None and capture.append(value):
                    save_capture(capture, capture_file)
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
    parser.add_argument("--fft-size", type=int, default=4096, help="Samples per spectrogram column")
    parser.add_argument(
        "--hop", type=int, default=None,
        help="Samples between spectrogram columns (default: fft-size / 2)")
    parser.add_argument("--fps", type=float, default=20.0, help="Plot redraw rate")
    parser.add_argument("--db-floor", type=float, default=-120.0, help="Lower spectrogram limit in dBFS")
    parser.add_argument("--db-ceiling", type=float, default=0.0, help="Upper spectrogram limit in dBFS")
    parser.add_argument(
        "--capture-seconds", type=float, default=0.0,
        help="Store the first N seconds of int16 samples; 0 disables capture")
    parser.add_argument(
        "--capture-file", default="audio_capture.txt",
        help="Text file for captured samples")
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
    if args.db_ceiling <= args.db_floor:
        parser.error("--db-ceiling must be greater than --db-floor")
    hop = args.fft_size // 2 if args.hop is None else args.hop
    if hop < 1:
        parser.error("--hop must be at least 1")
    if not np.isfinite(args.capture_seconds) or args.capture_seconds < 0:
        parser.error("--capture-seconds must be zero or positive")

    plot_points = max(2, int(args.window * args.sample_rate / args.decimate))
    dt = args.decimate / args.sample_rate  # time between plotted samples
    capture_sample_count = int(np.ceil(args.capture_seconds * args.sample_rate))
    capture_bytes = capture_sample_count * np.dtype(np.int16).itemsize
    if capture_bytes > MAX_CAPTURE_BYTES:
        parser.error(
            f"--capture-seconds requires {capture_bytes / (1024 * 1024):.1f} MiB; "
            f"the limit is {MAX_CAPTURE_BYTES / (1024 * 1024):.0f} MiB")
    capture = (SampleCapture(np.empty(capture_sample_count, dtype=np.int16))
               if capture_sample_count else None)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Error opening {args.port}: {exc}", file=sys.stderr)
        return 1

    data_queue: "queue.Queue[tuple[int, int]]" = queue.Queue(
        maxsize=max(plot_points * 2, args.fft_size * 4))
    stop_event = threading.Event()
    producer_thread = threading.Thread(
        target=producer,
        args=(ser, data_queue, stop_event, capture, args.capture_file),
        daemon=True)
    producer_thread.start()

    waveform_samples: deque[int] = deque(maxlen=plot_points)
    spectrum_samples: deque[int] = deque(maxlen=args.fft_size)
    column_count = max(1, round(args.window * args.sample_rate / hop))
    spectrogram = np.full(
        (args.fft_size // 2 + 1, column_count), args.db_floor, dtype=np.float64)
    samples_since_column = 0

    fig, (waveform_ax, spectrum_ax) = plt.subplots(
        2, 1, figsize=(10, 8), constrained_layout=True)
    waveform_line, = waveform_ax.plot([], [], lw=0.75)
    waveform_ax.set_xlim(-args.window, 0)
    waveform_ax.set_ylim(-1000, 1000)  # INT16_MIN, INT16_MAX)
    waveform_ax.set_xlabel("time (s)")
    waveform_ax.set_ylabel("sample value")
    waveform_ax.set_title(
        f"Audio samples (every {args.decimate}th, "
        f"{args.window:.0f}s window @ {args.sample_rate:.0f} Hz)")
    waveform_ax.grid(True, alpha=0.3)

    spectrogram_image = spectrum_ax.imshow(
        spectrogram, origin="lower", aspect="auto", cmap="viridis",
        vmin=args.db_floor, vmax=args.db_ceiling, interpolation="nearest",
        extent=(-args.window, 0, 0, args.sample_rate / 2.0))
    fig.colorbar(spectrogram_image, ax=spectrum_ax, label="magnitude (dBFS)")
    spectrum_ax.set_xlabel("time (s)")
    spectrum_ax.set_ylabel("frequency (Hz)")
    spectrum_ax.set_title(
        f"Spectrogram ({args.fft_size}-sample FFT every {hop} samples, "
        f"{args.sample_rate / args.fft_size:.1f} Hz resolution)")

    def on_close(_event):
        stop_event.set()

    fig.canvas.mpl_connect("close_event", on_close)

    def update(_frame):
        nonlocal spectrogram, samples_since_column
        new_columns = []
        try:
            while True:
                sample_index, value = data_queue.get_nowait()
                spectrum_samples.append(value)
                if sample_index % args.decimate == 0:
                    waveform_samples.append(value)
                samples_since_column += 1
                if samples_since_column >= hop and len(spectrum_samples) == args.fft_size:
                    samples_since_column = 0
                    _, magnitudes_dbfs = calculate_spectrum(
                        np.fromiter(spectrum_samples, dtype=np.int16), args.sample_rate)
                    new_columns.append(magnitudes_dbfs)
        except queue.Empty:
            pass

        if waveform_samples:
            xs = np.arange(-len(waveform_samples) + 1, 1) * dt
            waveform_line.set_data(xs, waveform_samples)

        if new_columns:
            shift = min(len(new_columns), column_count)
            spectrogram = np.roll(spectrogram, -shift, axis=1)
            spectrogram[:, -shift:] = np.array(new_columns[-shift:]).T
            spectrogram_image.set_data(spectrogram)

        return waveform_line, spectrogram_image

    animation = FuncAnimation(
        fig, update, interval=1000.0 / args.fps, blit=False, cache_frame_data=False)
    print(
        f"Listening on {args.port} @ {args.baud} baud, plotting every "
        f"{args.decimate}th sample and {args.fft_size}-sample spectrogram columns "
        "(close window to stop)")
    if capture is not None:
        print(
            f"Capturing the first {capture.samples.size} samples "
            f"({args.capture_seconds:g} s) to {args.capture_file}")

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        producer_thread.join()
        ser.close()
        if capture is not None and not capture.saved:
            save_capture(capture, args.capture_file)
        del animation

    return 0


if __name__ == "__main__":
    sys.exit(main())
