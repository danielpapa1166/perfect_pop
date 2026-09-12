#!/usr/bin/env python3
"""Generate a C-loadable chirp-in-noise test signal and diagnostic plot."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


SAMPLE_RATE_HZ = 48_000
DURATION_SECONDS = 10
CHIRP_START_SECONDS = 5.0
CHIRP_DURATION_SECONDS = 0.100
CHIRP_START_FREQUENCY_HZ = 1_000.0
CHIRP_END_FREQUENCY_HZ = 8_000.0
CHIRP_AMPLITUDE = 0.70
NOISE_STANDARD_DEVIATION = 0.04
RANDOM_SEED = 20260912

DATA_DIRECTORY = Path(__file__).resolve().parent / "data"
PCM_OUTPUT_PATH = DATA_DIRECTORY / "chirp_noise_48khz_s16le.pcm"
PLOT_OUTPUT_PATH = DATA_DIRECTORY / "chirp_noise_48khz.png"


def generate_signal():
    """Return a 10-second float signal containing noise and one linear chirp."""
    sample_count = SAMPLE_RATE_HZ * DURATION_SECONDS
    signal = np.random.default_rng(RANDOM_SEED).normal(
        scale=NOISE_STANDARD_DEVIATION, size=sample_count
    )

    chirp_sample_count = int(CHIRP_DURATION_SECONDS * SAMPLE_RATE_HZ)
    chirp_time_seconds = np.arange(chirp_sample_count) / SAMPLE_RATE_HZ
    chirp_rate_hz_per_second = (
        CHIRP_END_FREQUENCY_HZ - CHIRP_START_FREQUENCY_HZ
    ) / CHIRP_DURATION_SECONDS
    chirp_phase_radians = 2.0 * np.pi * (
        CHIRP_START_FREQUENCY_HZ * chirp_time_seconds
        + 0.5 * chirp_rate_hz_per_second * chirp_time_seconds**2
    )

    chirp_start_sample = int(CHIRP_START_SECONDS * SAMPLE_RATE_HZ)
    chirp_end_sample = chirp_start_sample + chirp_sample_count
    signal[chirp_start_sample:chirp_end_sample] += (
        CHIRP_AMPLITUDE * np.sin(chirp_phase_radians)
    )

    return signal


def save_plot(signal):
    """Save a whole-signal view, chirp detail, and spectrogram."""
    time_seconds = np.arange(signal.size) / SAMPLE_RATE_HZ
    chirp_window_start = CHIRP_START_SECONDS - 0.025
    chirp_window_end = CHIRP_START_SECONDS + CHIRP_DURATION_SECONDS + 0.025
    chirp_mask = (time_seconds >= chirp_window_start) & (
        time_seconds <= chirp_window_end
    )

    figure, (overview_axis, detail_axis, spectrogram_axis) = plt.subplots(
        3, 1, figsize=(11, 9)
    )
    overview_axis.plot(time_seconds, signal, linewidth=0.35)
    overview_axis.set_title("10 s Chirp-in-Noise Signal")
    overview_axis.set_ylabel("Amplitude")
    overview_axis.grid(True, linestyle=":")

    detail_axis.plot(time_seconds[chirp_mask], signal[chirp_mask], linewidth=0.6)
    detail_axis.set_title("Chirp Detail")
    detail_axis.set_xlabel("Time (s)")
    detail_axis.set_ylabel("Amplitude")
    detail_axis.grid(True, linestyle=":")

    spectrogram_axis.specgram(signal, NFFT=1024, Fs=SAMPLE_RATE_HZ, noverlap=768)
    spectrogram_axis.set_title("Spectrogram")
    spectrogram_axis.set_xlabel("Time (s)")
    spectrogram_axis.set_ylabel("Frequency (Hz)")
    spectrogram_axis.set_ylim(0, 10_000)

    figure.tight_layout()
    figure.savefig(PLOT_OUTPUT_PATH, dpi=160)
    plt.close(figure)


def main():
    signal = generate_signal()
    pcm_signal = np.rint(np.clip(signal, -1.0, 1.0) * np.iinfo(np.int16).max).astype(
        "<i2"
    )

    DATA_DIRECTORY.mkdir(parents=True, exist_ok=True)
    pcm_signal.tofile(PCM_OUTPUT_PATH)
    save_plot(signal)

    print(f"Wrote {pcm_signal.size} mono samples to {PCM_OUTPUT_PATH}")
    print(f"Wrote plot to {PLOT_OUTPUT_PATH}")


if __name__ == "__main__":
    main()