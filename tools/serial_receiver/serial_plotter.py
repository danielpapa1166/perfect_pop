#!/usr/bin/env python3
"""Live sliding-window plot of int32 audio samples streamed from the STM32 over UART.

A producer thread reads/decodes UART frames and pushes (sample_index, value)
pairs into a queue; the main thread consumes the queue and redraws the plot.
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


def producer(ser: serial.Serial, out_queue: "queue.Queue[tuple[int, int]]",
             decimate: int, stop_event: threading.Event) -> None:
    """Decode UART frames and forward every `decimate`-th sample to the plot."""
    sample_index = 0
    try:
        for values in read_frames(ser):
            if stop_event.is_set():
                return
            for value in values:
                if sample_index % decimate == 0:
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
    parser.add_argument("--window", type=float, default=1.0, help="Sliding window length in seconds")
    parser.add_argument("--decimate", type=int, default=1, help="Plot every Nth sample")
    parser.add_argument("--fps", type=float, default=20.0, help="Plot redraw rate")
    args = parser.parse_args()

    plot_points = max(2, int(args.window * args.sample_rate / args.decimate))
    dt = args.decimate / args.sample_rate  # time between plotted samples

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Error opening {args.port}: {exc}", file=sys.stderr)
        return 1

    data_queue: "queue.Queue[tuple[int, int]]" = queue.Queue(maxsize=plot_points * 2)
    stop_event = threading.Event()
    producer_thread = threading.Thread(
        target=producer, args=(ser, data_queue, args.decimate, stop_event), daemon=True)
    producer_thread.start()

    ys: deque = deque(maxlen=plot_points)

    fig, ax = plt.subplots()
    line, = ax.plot([], [], lw=0.75)
    ax.set_xlim(-args.window, 0)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("sample value")
    ax.set_title(f"Audio samples (every {args.decimate}th, {args.window:.0f}s window @ {args.sample_rate:.0f} Hz)")

    def on_close(_event):
        stop_event.set()

    fig.canvas.mpl_connect("close_event", on_close)

    def update(_frame):
        try:
            while True:
                _idx, value = data_queue.get_nowait()
                ys.append(value)
        except queue.Empty:
            pass

        if not ys:
            return (line,)

        xs = np.arange(-len(ys) + 1, 1) * dt
        line.set_data(xs, ys)
        ax.relim()
        ax.autoscale_view(scalex=False, scaley=True)
        return (line,)

    ani = FuncAnimation(fig, update, interval=1000.0 / args.fps, blit=False, cache_frame_data=False)
    print(f"Listening on {args.port} @ {args.baud} baud, plotting every {args.decimate}th sample (close window to stop)")

    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        ser.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
