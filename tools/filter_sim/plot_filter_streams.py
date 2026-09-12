#!/usr/bin/env python3
"""Plot the raw PCM input stream alongside the filter simulator output."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


SAMPLE_RATE_HZ = 48_000
DATA_DIRECTORY = Path(__file__).resolve().parent / "data"
INPUT_PATH = DATA_DIRECTORY / "chirp_noise_48khz_s16le.pcm"
OUTPUT_PATH = DATA_DIRECTORY / "chirp_noise_filtered_48khz_s16le.pcm"
PLOT_PATH = DATA_DIRECTORY / "chirp_noise_filter_comparison.png"


def read_pcm(path):
    """Read mono signed 16-bit little-endian PCM as normalized float samples."""
    samples = np.fromfile(path, dtype="<i2")
    if samples.size == 0:
        raise ValueError(f"No samples found in {path}")
    return samples.astype(np.float64) / np.iinfo(np.int16).max


def plot_spectrogram(axis, signal, title):
    """Plot a spectrogram, or indicate that a stream is silent."""
    axis.set_title(title)
    if np.any(signal):
        axis.specgram(signal, NFFT=1024, Fs=SAMPLE_RATE_HZ, noverlap=768)
    else:
        axis.text(
            0.5,
            0.5,
            "No non-zero output samples",
            ha="center",
            va="center",
            transform=axis.transAxes,
        )


def plot_streams(input_signal, output_signal):
    """Save overview, chirp-detail, and spectrogram comparisons."""
    time_seconds = np.arange(input_signal.size) / SAMPLE_RATE_HZ
    detail_start_seconds = 4.975
    detail_end_seconds = 5.125
    detail_mask = (time_seconds >= detail_start_seconds) & (
        time_seconds <= detail_end_seconds
    )

    figure, axes = plt.subplots(3, 2, figsize=(14, 10))
    input_axes = axes[:, 0]
    output_axes = axes[:, 1]

    input_axes[0].plot(time_seconds, input_signal, linewidth=0.35)
    output_axes[0].plot(time_seconds, output_signal, linewidth=0.35)
    input_axes[0].set_title("Input: Chirp in Gaussian Noise")
    output_axes[0].set_title("Output: Filtered PCM")

    input_axes[1].plot(
        time_seconds[detail_mask], input_signal[detail_mask], linewidth=0.6
    )
    output_axes[1].plot(
        time_seconds[detail_mask], output_signal[detail_mask], linewidth=0.6
    )
    input_axes[1].set_title("Input Chirp Detail")
    output_axes[1].set_title("Output Chirp Detail")

    plot_spectrogram(input_axes[2], input_signal, "Input Spectrogram")
    plot_spectrogram(output_axes[2], output_signal, "Output Spectrogram")

    for axis in axes[0]:
        axis.set_ylabel("Amplitude")
        axis.grid(True, linestyle=":")
    for axis in axes[1]:
        axis.set_xlabel("Time (s)")
        axis.set_ylabel("Amplitude")
        axis.grid(True, linestyle=":")
    for axis in axes[2]:
        axis.set_xlabel("Time (s)")
        axis.set_ylabel("Frequency (Hz)")
        axis.set_ylim(0, 10_000)

    figure.tight_layout()
    figure.savefig(PLOT_PATH, dpi=160)
    plt.close(figure)


def main():
    input_signal = read_pcm(INPUT_PATH)
    output_signal = read_pcm(OUTPUT_PATH)

    if input_signal.size != output_signal.size:
        raise ValueError(
            f"Input has {input_signal.size} samples but output has "
            f"{output_signal.size} samples"
        )

    plot_streams(input_signal, output_signal)
    print(f"Plotted {input_signal.size} samples from {INPUT_PATH} and {OUTPUT_PATH}")
    print(f"Wrote comparison plot to {PLOT_PATH}")


if __name__ == "__main__":
    main()