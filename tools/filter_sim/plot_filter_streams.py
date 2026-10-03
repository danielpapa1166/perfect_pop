#!/usr/bin/env python3
"""Plot PCM input alongside raw float x-correlation simulator output."""

import argparse
import re
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


SAMPLE_RATE_HZ = 48_000
XCORR_SAMPLE_RATE_HZ = 8_000
XCORR_HISTORY_SECONDS = 0.250
CAPTURE_DETAIL_SECONDS = 0.1
DATA_DIRECTORY = Path(__file__).resolve().parent / "data"
SNAP_SIM_PATH = Path(__file__).resolve().parent / "build" / "filter_sim_snap"
SNAP_TEMPLATE_HEADER_PATH = (
    Path(__file__).resolve().parents[2] / "Core" / "Src" / "pp_snap_template.h"
)
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
    input_detail_start_seconds = 4.9
    input_detail_end_seconds = 6.0
    output_detail_start_seconds = input_detail_start_seconds # + XCORR_HISTORY_SECONDS
    output_detail_end_seconds = input_detail_end_seconds # + XCORR_HISTORY_SECONDS
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
    #plt.close(figure)
    plt.show()


def read_snap_template(path):
    """Parse the int16 template array out of the generated C header."""
    body = re.search(r"pp_snap_template\[[^\]]*\]\s*=\s*\{([^}]*)\}", path.read_text())
    if body is None:
        raise ValueError(f"No pp_snap_template array found in {path}")
    return np.array([int(v) for v in re.findall(r"-?\d+", body.group(1))], dtype=np.int16)


def run_snap_simulation(capture_path):
    """Convert a text capture to PCM, run the snap-template simulator, return both paths."""
    if not SNAP_SIM_PATH.exists():
        raise FileNotFoundError(
            f"{SNAP_SIM_PATH} not found; run: cmake -S . -B build && cmake --build build"
        )
    samples = np.loadtxt(capture_path, dtype=np.int16, ndmin=1)
    pcm_path = DATA_DIRECTORY / f"{capture_path.stem}_48khz_s16le.pcm"
    raw_path = DATA_DIRECTORY / f"{capture_path.stem}_xcorr_48khz_f32le.raw"
    DATA_DIRECTORY.mkdir(parents=True, exist_ok=True)
    samples.astype("<i2").tofile(pcm_path)
    subprocess.run([str(SNAP_SIM_PATH), str(pcm_path), str(raw_path)], check=True)
    return pcm_path, raw_path


def plot_capture_streams(input_signal, output_signal, template, plot_path):
    """Save input, filter window, and x-correlation output for a snap capture."""
    time_seconds = np.arange(input_signal.size) / SAMPLE_RATE_HZ
    peak_index = int(np.argmax(np.abs(output_signal)))
    detail_start = max(0.0, time_seconds[peak_index] - CAPTURE_DETAIL_SECONDS / 2)
    detail_end = detail_start + CAPTURE_DETAIL_SECONDS
    detail_mask = (time_seconds >= detail_start) & (time_seconds <= detail_end)

    figure, axes = plt.subplots(3, 2, figsize=(14, 10))
    input_axes = axes[:, 0]
    output_axes = axes[:, 1]

    input_axes[0].plot(time_seconds, input_signal, linewidth=0.35)
    input_axes[0].set_title("Input: Snap Capture")
    output_axes[0].plot(time_seconds, output_signal, linewidth=0.35)
    output_axes[0].set_title("Output: X-Correlation")

    template_seconds = np.arange(template.size) / XCORR_SAMPLE_RATE_HZ
    input_axes[1].plot(template_seconds, template / np.iinfo(np.int16).max, marker=".")
    input_axes[1].set_title(f"X-Correlation Filter Window ({template.size} samples @ 8 kHz)")
    output_axes[1].plot(
        time_seconds[detail_mask], output_signal[detail_mask], linewidth=0.6
    )
    output_axes[1].set_title("Output Detail Around Strongest Peak")

    plot_spectrogram(input_axes[2], input_signal, "Input Spectrogram")
    plot_spectrogram(output_axes[2], output_signal, "Output X-Correlation Spectrogram")

    for axis in axes[0]:
        axis.set_ylabel("Amplitude")
        axis.grid(True, linestyle=":")
    for axis in axes[1]:
        axis.set_ylabel("Amplitude")
        axis.grid(True, linestyle=":")
    input_axes[1].set_xlabel("Time (s)")
    output_axes[1].set_xlabel("Time (s)")
    for axis in axes[2]:
        axis.set_xlabel("Time (s)")
        axis.set_ylabel("Frequency (Hz)")
        axis.set_ylim(0, 10_000)

    figure.tight_layout()
    figure.savefig(plot_path, dpi=160)
    plt.show()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--capture",
        type=Path,
        help="Text capture of int16 samples to run through the snap-template simulator",
    )
    args = parser.parse_args()

    if args.capture is not None:
        pcm_path, raw_path = run_snap_simulation(args.capture)
        input_signal = read_pcm(pcm_path)
        output_signal = read_xcorr(raw_path)
        plot_path = DATA_DIRECTORY / f"{args.capture.stem}_xcorr_comparison.png"
        plot_capture_streams(
            input_signal,
            output_signal,
            read_snap_template(SNAP_TEMPLATE_HEADER_PATH),
            plot_path,
        )
        print(f"Wrote comparison plot to {plot_path}")
        return

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