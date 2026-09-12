#!/usr/bin/env python3
"""Plot Bode diagrams for the simple filters in Core/Src/pp_dsp.c."""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


DEFAULT_SAMPLE_RATE_HZ = 48_000.0
DEFAULT_ALPHA = 0.5


def all_pass_response(frequency_hz):
    """Return the response of all_pass_filter(), which copies its input."""
    return np.ones_like(frequency_hz, dtype=np.complex128)


def low_pass_response(frequency_hz, sample_rate_hz, alpha):
    """Return the response of y[n] = alpha*x[n] + (1-alpha)*y[n-1]."""
    angular_frequency = 2.0 * np.pi * frequency_hz / sample_rate_hz
    return alpha / (1.0 - (1.0 - alpha) * np.exp(-1j * angular_frequency))


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Plot Bode diagrams for the pp_dsp.c example filters."
    )
    parser.add_argument(
        "--sample-rate",
        type=float,
        default=DEFAULT_SAMPLE_RATE_HZ,
        help="sample rate in Hz (default: %(default).0f)",
    )
    parser.add_argument(
        "--alpha",
        type=float,
        default=DEFAULT_ALPHA,
        help="low-pass alpha coefficient from pp_dsp.c (default: %(default)s)",
    )
    parser.add_argument(
        "--min-frequency",
        type=float,
        default=1.0,
        help="lowest plotted frequency in Hz (default: %(default)s)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="save the plot to this file",
    )
    parser.add_argument(
        "--no-show",
        action="store_true",
        help="do not open an interactive plot window",
    )
    arguments = parser.parse_args()

    if arguments.sample_rate <= 0.0:
        parser.error("--sample-rate must be positive")
    if not 0.0 < arguments.alpha <= 1.0:
        parser.error("--alpha must be greater than 0 and no more than 1")
    if not 0.0 < arguments.min_frequency < arguments.sample_rate / 2.0:
        parser.error("--min-frequency must be between 0 and the Nyquist frequency")

    return arguments


def plot_bode(frequency_hz, responses):
    figure, (magnitude_axis, phase_axis) = plt.subplots(
        2, 1, figsize=(9, 7), sharex=True
    )

    for label, response in responses.items():
        magnitude_db = 20.0 * np.log10(np.maximum(np.abs(response), np.finfo(float).tiny))
        phase_degrees = np.degrees(np.unwrap(np.angle(response)))
        magnitude_axis.semilogx(frequency_hz, magnitude_db, label=label)
        phase_axis.semilogx(frequency_hz, phase_degrees, label=label)

    magnitude_axis.set_title("Bode Diagram: pp_dsp.c Example Filters")
    magnitude_axis.set_ylabel("Magnitude (dB)")
    phase_axis.set_xlabel("Frequency (Hz)")
    phase_axis.set_ylabel("Phase (degrees)")

    for axis in (magnitude_axis, phase_axis):
        axis.grid(True, which="both", linestyle=":")
        axis.legend()

    figure.tight_layout()
    return figure


def main():
    arguments = parse_arguments()
    frequency_hz = np.geomspace(
        arguments.min_frequency, arguments.sample_rate / 2.0, num=2_000
    )
    responses = {
        "All-pass (copy input)": all_pass_response(frequency_hz),
        f"Low-pass (alpha = {arguments.alpha:g})": low_pass_response(
            frequency_hz, arguments.sample_rate, arguments.alpha
        ),
    }
    figure = plot_bode(frequency_hz, responses)

    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(arguments.output, dpi=160)
        print(f"Saved Bode diagram to {arguments.output}")

    if not arguments.no_show:
        plt.show()


if __name__ == "__main__":
    main()