#!/usr/bin/env python3
"""Build a snap cross-correlation template from a capture that contains only snaps."""

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SAMPLE_RATE_HZ = 48_000
DECIMATION_FACTOR = 6
XCORR_SAMPLE_RATE_HZ = SAMPLE_RATE_HZ // DECIMATION_FACTOR

ENVELOPE_WINDOW_SAMPLES = 96
EVENT_THRESHOLD_FACTOR = 6.0
EVENT_MERGE_GAP_SECONDS = 0.1
ALIGN_MAX_SHIFT_SECONDS = 0.001
ALIGN_ITERATIONS = 3
EDGE_TAPER_FRACTION = 0.1
NOISE_GUARD_SECONDS = 0.05
SCORE_SEARCH_SECONDS = 0.002
MIN_WINDOW_RMS_FACTOR = 4.0
HEADER_SAMPLES_PER_LINE = 12

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_CAPTURE_PATH = SCRIPT_DIRECTORY / "capture_02.txt"
DEFAULT_HEADER_PATH = SCRIPT_DIRECTORY.parents[1] / "Core" / "Src" / "pp_snap_template.h"
DEFAULT_PLOT_PATH = SCRIPT_DIRECTORY / "data" / "snap_template.png"


def read_capture(path):
    """Load int16 samples as floats with the DC offset removed."""
    samples = np.loadtxt(path, dtype=np.float64, ndmin=1)
    if samples.size == 0:
        raise ValueError(f"No samples found in {path}")
    return samples - np.median(samples)


def find_event_peaks(samples):
    """Return the sample index of the largest excursion of every detected snap."""
    kernel = np.ones(ENVELOPE_WINDOW_SAMPLES) / ENVELOPE_WINDOW_SAMPLES
    envelope = np.sqrt(np.convolve(samples**2, kernel, mode="same"))
    active = np.flatnonzero(envelope > EVENT_THRESHOLD_FACTOR * np.median(envelope))
    if active.size == 0:
        return []

    gap = int(EVENT_MERGE_GAP_SECONDS * SAMPLE_RATE_HZ)
    groups = np.split(active, np.flatnonzero(np.diff(active) > gap) + 1)
    return [
        int(group[0] + np.argmax(np.abs(samples[group[0] : group[-1] + 1])))
        for group in groups
    ]


def decimate_like_firmware(samples):
    """Mirror pp_dsp.c: two-tap average anti-aliasing, then keep every Nth sample."""
    anti_aliased = samples.copy()
    anti_aliased[1:] = 0.5 * (samples[1:] + samples[:-1])
    return anti_aliased[::DECIMATION_FACTOR]


def edge_taper(length):
    ramp_length = max(1, int(length * EDGE_TAPER_FRACTION))
    ramp = 0.5 * (1.0 - np.cos(np.pi * np.arange(ramp_length) / ramp_length))
    taper = np.ones(length)
    taper[:ramp_length] = ramp
    taper[-ramp_length:] = ramp[::-1]
    return taper


def align_events(samples, peaks, pre_samples, length):
    """Cut windows around each peak, align them by cross-correlation, normalise energy."""
    margin = int(ALIGN_MAX_SHIFT_SECONDS * SAMPLE_RATE_HZ)
    segments = []
    for peak in peaks:
        start = peak - pre_samples - margin
        stop = peak - pre_samples + length + margin
        if start >= 0 and stop <= samples.size:
            segments.append(samples[start:stop])
    if not segments:
        raise ValueError("No event has enough surrounding samples")

    reference = segments[0][margin : margin + length]
    aligned = []
    for _ in range(ALIGN_ITERATIONS):
        aligned = []
        for segment in segments:
            correlation = np.correlate(segment, reference, mode="valid")
            lag = int(np.argmax(np.abs(correlation)))
            polarity = 1.0 if correlation[lag] >= 0 else -1.0
            window = polarity * segment[lag : lag + length]
            aligned.append(window / np.linalg.norm(window))
        reference = np.mean(aligned, axis=0)
    return np.array(aligned), reference


def build_template(samples, peaks, pre_samples, length):
    """Return the unit-energy template at the xcorr rate and the aligned 48 kHz average."""
    aligned, average = align_events(samples, peaks, pre_samples, length)
    average_48khz = average * edge_taper(length)

    template = decimate_like_firmware(average_48khz)
    template -= template.mean()
    template /= np.linalg.norm(template)
    return template, average_48khz, aligned


def normalized_xcorr(stream, template):
    """Sliding NCC gated by window energy; entry i scores stream[i : i + len(template)]."""
    window_length = template.size
    correlation = np.correlate(stream, template, mode="valid")
    cumulative_energy = np.concatenate(([0.0], np.cumsum(stream**2)))
    window_energy = cumulative_energy[window_length:] - cumulative_energy[:-window_length]
    window_rms = np.sqrt(window_energy / window_length)
    scores = correlation / (np.sqrt(window_energy) * np.linalg.norm(template) + 1e-12)
    # NCC ignores loudness, so quiet background would otherwise score as high as a snap.
    scores[window_rms < MIN_WINDOW_RMS_FACTOR * np.median(window_rms)] = 0.0
    return scores


def event_and_noise_scores(scores, peaks, held_out_peak, pre_samples, template_length):
    """Best score at one snap and the best score away from every snap."""
    expected = (held_out_peak - pre_samples) // DECIMATION_FACTOR
    search = int(SCORE_SEARCH_SECONDS * XCORR_SAMPLE_RATE_HZ)
    event_score = scores[max(0, expected - search) : expected + search + 1].max()

    guard = int(NOISE_GUARD_SECONDS * SAMPLE_RATE_HZ)
    quiet = np.ones(scores.size, dtype=bool)
    for peak in peaks:
        first = max(0, (peak - guard) // DECIMATION_FACTOR - template_length)
        quiet[first : (peak + guard) // DECIMATION_FACTOR + 1] = False
    return event_score, scores[quiet].max()


def leave_one_out_report(samples, peaks, pre_samples, length):
    """Build each template without one snap and score it on that snap versus background."""
    stream = decimate_like_firmware(samples)
    print("Leave-one-out (held-out snap NCC vs. worst background NCC):")
    for index, peak in enumerate(peaks):
        template, _, _ = build_template(
            samples, peaks[:index] + peaks[index + 1 :], pre_samples, length
        )
        scores = normalized_xcorr(stream, template)
        event_score, noise_score = event_and_noise_scores(
            scores, peaks, peak, pre_samples, template.size
        )
        print(
            f"  snap at {peak / SAMPLE_RATE_HZ:7.3f} s: "
            f"{event_score:5.2f} vs {noise_score:5.2f}"
        )


def save_header(template, path):
    scaled = np.rint(template / np.max(np.abs(template)) * np.iinfo(np.int16).max)
    rows = [
        "    " + ", ".join(str(int(v)) for v in scaled[i : i + HEADER_SAMPLES_PER_LINE]) + ","
        for i in range(0, scaled.size, HEADER_SAMPLES_PER_LINE)
    ]
    header = "\n".join(
        (
            "/* Generated by tools/filter_sim/build_templates.py. */",
            "#ifndef SRC_PP_SNAP_TEMPLATE_H_",
            "#define SRC_PP_SNAP_TEMPLATE_H_",
            "",
            "#include <stdint.h>",
            "",
            f"#define PP_SNAP_TEMPLATE_SAMPLE_RATE_HZ {XCORR_SAMPLE_RATE_HZ}U",
            f"#define PP_SNAP_TEMPLATE_DECIMATION_FACTOR {DECIMATION_FACTOR}U",
            f"#define PP_SNAP_TEMPLATE_SAMPLE_COUNT {scaled.size}U",
            "",
            "static const int16_t pp_snap_template[PP_SNAP_TEMPLATE_SAMPLE_COUNT] = {",
            *rows,
            "};",
            "",
            "#endif /* SRC_PP_SNAP_TEMPLATE_H_ */",
            "",
        )
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(header, encoding="ascii")


def save_plot(samples, peaks, aligned, average_48khz, template, path):
    stream = decimate_like_firmware(samples)
    scores = normalized_xcorr(stream, template)

    figure, (overlay_axis, template_axis, score_axis) = plt.subplots(3, 1, figsize=(11, 9))

    time_ms = np.arange(average_48khz.size) / SAMPLE_RATE_HZ * 1000.0
    for window in aligned:
        overlay_axis.plot(time_ms, window, linewidth=0.5, alpha=0.5)
    overlay_axis.plot(time_ms, average_48khz, color="k", linewidth=1.5, label="average")
    overlay_axis.set_title(f"{len(aligned)} aligned snaps (unit energy, 48 kHz)")
    overlay_axis.set_xlabel("Time (ms)")
    overlay_axis.legend()

    template_axis.stem(np.arange(template.size) / XCORR_SAMPLE_RATE_HZ * 1000.0, template)
    template_axis.set_title(f"Template at {XCORR_SAMPLE_RATE_HZ} Hz ({template.size} samples)")
    template_axis.set_xlabel("Time (ms)")

    score_axis.plot(np.arange(scores.size) / XCORR_SAMPLE_RATE_HZ, scores, linewidth=0.5)
    for peak in peaks:
        score_axis.axvline(peak / SAMPLE_RATE_HZ, color="r", alpha=0.3)
    score_axis.set_title("Normalised cross-correlation over the capture")
    score_axis.set_xlabel("Time (s)")
    score_axis.set_ylim(-1.0, 1.0)

    figure.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(path, dpi=140)
    plt.show()
    # plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_file", type=Path, nargs="?", default=DEFAULT_CAPTURE_PATH)
    parser.add_argument("--pre-ms", type=float, default=1.0, help="Window length before the peak")
    parser.add_argument("--length-ms", type=float, default=8.0, help="Total window length")
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER_PATH)
    parser.add_argument("--plot", type=Path, default=DEFAULT_PLOT_PATH)
    args = parser.parse_args()

    pre_samples = int(args.pre_ms * SAMPLE_RATE_HZ / 1000.0)
    length = int(args.length_ms * SAMPLE_RATE_HZ / 1000.0) // DECIMATION_FACTOR * DECIMATION_FACTOR
    if length <= pre_samples:
        parser.error("--length-ms must exceed --pre-ms")

    try:
        samples = read_capture(args.capture_file)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    peaks = find_event_peaks(samples)
    print(f"Found {len(peaks)} snaps at: " + ", ".join(f"{p / SAMPLE_RATE_HZ:.3f} s" for p in peaks))
    if len(peaks) < 2:
        parser.error("need at least two snaps to build and validate a template")

    template, average_48khz, aligned = build_template(samples, peaks, pre_samples, length)
    leave_one_out_report(samples, peaks, pre_samples, length)

    save_header(template, args.header)
    save_plot(samples, peaks, aligned, average_48khz, template, args.plot)
    print(f"Wrote {template.size}-sample template to {args.header}")
    print(f"Wrote plot to {args.plot}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
