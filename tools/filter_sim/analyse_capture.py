#!/usr/bin/env python3
"""Plot signed 16-bit audio samples saved by serial_plotter.py."""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


DEFAULT_SAMPLE_RATE_HZ = 48_000.0
DEFAULT_MAX_PLOT_POINTS = 100_000


def read_capture(path: Path) -> np.ndarray:
	"""Load whitespace-delimited signed 16-bit audio samples from a text file."""
	try:
		samples = np.loadtxt(path, dtype=np.int16, ndmin=1)
	except (OSError, ValueError) as exc:
		raise ValueError(f"Could not load samples from {path}: {exc}") from exc

	if samples.size == 0:
		raise ValueError(f"No samples found in {path}")
	return samples


def plot_capture(samples: np.ndarray, sample_rate_hz: float,
				 max_plot_points: int) -> None:
	"""Display the capture waveform without overloading Matplotlib for long logs."""
	displayed_count = min(samples.size, max_plot_points)
	sample_indices = np.linspace(
		0, samples.size - 1, num=displayed_count, dtype=np.intp)
	time_seconds = sample_indices / sample_rate_hz

	figure, axis = plt.subplots(figsize=(12, 5), constrained_layout=True)
	axis.plot(time_seconds, samples[sample_indices], linewidth=0.5)
	axis.set_title(
		f"Audio Capture: {samples.size:,} Samples "
		f"({displayed_count:,} Displayed)")
	axis.set_xlabel("Time (s)")
	axis.set_ylabel("Sample Value (int16)")
	axis.set_ylim(np.iinfo(np.int16).min, np.iinfo(np.int16).max)
	axis.grid(True, alpha=0.3)
	plt.show()


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument(
		"capture_file", type=Path, nargs="?",
		help="Text file containing whitespace-delimited int16 audio samples")
	parser.add_argument(
		"--capture-file", "--capture_file", dest="capture_file_option",
		type=Path, help="Text file containing whitespace-delimited int16 audio samples")
	parser.add_argument(
		"--sample-rate", type=float, default=DEFAULT_SAMPLE_RATE_HZ,
		help=f"Sample rate in Hz (default: {DEFAULT_SAMPLE_RATE_HZ:g})")
	parser.add_argument(
		"--max-plot-points", type=int, default=DEFAULT_MAX_PLOT_POINTS,
		help=f"Maximum points drawn (default: {DEFAULT_MAX_PLOT_POINTS:,})")
	args = parser.parse_args()

	if args.sample_rate <= 0:
		parser.error("--sample-rate must be positive")
	if args.max_plot_points < 1:
		parser.error("--max-plot-points must be at least 1")
	if args.capture_file is not None and args.capture_file_option is not None:
		parser.error("provide the capture file either positionally or with --capture-file")

	capture_file = args.capture_file_option or args.capture_file
	if capture_file is None:
		parser.error("a capture file is required")

	try:
		samples = read_capture(capture_file)
	except ValueError as exc:
		parser.error(str(exc))

	print(
		f"Loaded {samples.size:,} int16 samples from {capture_file} "
		f"at {args.sample_rate:g} Hz")
	plot_capture(samples, args.sample_rate, args.max_plot_points)
	return 0


if __name__ == "__main__":
	sys.exit(main())
