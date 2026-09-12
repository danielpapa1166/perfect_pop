#!/usr/bin/env python3
"""Plot PCM input alongside raw float x-correlation simulator output."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


SAMPLE_RATE_HZ = 48_000
XCORR_HISTORY_SECONDS = 0.250
DATA_DIRECTORY = Path(__file__).resolve().parent / "data"
INPUT_PATH = DATA_DIRECTORY / "chirp_noise_48khz_s16le.pcm"
OUTPUT_PATH = DATA_DIRECTORY / "chirp_noise_xcorr_48khz_f32le.raw"
PLOT_PATH = DATA_DIRECTORY / "chirp_noise_xcorr_comparison.png"


def read_pcm(path):
    """Read mono signed 16-bit little-endian PCM as normalized float samples."""
    samples = np.fromfile(path, dtype="<i2")
    if samples.size == 0:
        raise ValueError(f"No samples found in {path}")
    return samples.astype(np.float64) / np.iinfo(np.int16).max


def read_xcorr(path):
    """Read the simulator's little-endian 32-bit float x-correlation stream."""
    samples = np.fromfile(path, dtype="<f4")
    if samples.size == 0:
        raise ValueError(f"No samples found in {path}")
    if not np.isfinite(samples).all():
        raise ValueError(f"Non-finite samples found in {path}")
    return samples.astype(np.float64)


def plot_spectrogram(axis, signal, title):
    """Plot a spectrogram, or indicate that a stream is silent."""
    axis.set_title(title)
    if np.any(signal):
        with np.errstate(divide="ignore"):
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
    input_detail_start_seconds = 4.975
    input_detail_end_seconds = 5.125
    output_detail_start_seconds = input_detail_start_seconds + XCORR_HISTORY_SECONDS
    output_detail_end_seconds = input_detail_end_seconds + XCORR_HISTORY_SECONDS
    input_detail_mask = (time_seconds >= input_detail_start_seconds) & (
        time_seconds <= input_detail_end_seconds
    )
    output_detail_mask = (time_seconds >= output_detail_start_seconds) & (
        time_seconds <= output_detail_end_seconds
    )

    figure, axes = plt.subplots(3, 2, figsize=(14, 10))
    input_axes = axes[:, 0]
    output_axes = axes[:, 1]

    input_axes[0].plot(time_seconds, input_signal, linewidth=0.35)
    output_axes[0].plot(time_seconds, output_signal, linewidth=0.35)
    input_axes[0].set_title("Input: Chirp in Gaussian Noise")
    output_axes[0].set_title("Output: X-Correlation")

    input_axes[1].plot(
        time_seconds[input_detail_mask], input_signal[input_detail_mask], linewidth=0.6
    )
    output_axes[1].plot(
        time_seconds[output_detail_mask],
        output_signal[output_detail_mask],
        linewidth=0.6,
    )
    input_axes[1].set_title("Input Chirp Detail")
    output_axes[1].set_title("Output Detail (250 ms History Delay)")

    plot_spectrogram(input_axes[2], input_signal, "Input Spectrogram")
    plot_spectrogram(output_axes[2], output_signal, "Output X-Correlation Spectrogram")

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
    output_signal = read_xcorr(OUTPUT_PATH)

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