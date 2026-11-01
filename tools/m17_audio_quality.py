#!/usr/bin/env python3
"""Measure audible artefacts in a decoded M17/Codec2 WAV recording.

The tool is deliberately reference-free: it looks for the things that are
audible in a real recording even when the original microphone signal is not
available.

Typical use:

    m17_audio_quality.py capture.wav
    m17_audio_quality.py capture.wav --pause-threshold 120 --json report.json
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import wave
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np


@dataclass
class ToneArtefact:
    start_s: float
    duration_s: float
    frequency_hz: float
    tonality_db: float
    rms: float


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        if wav.getnchannels() != 1:
            raise ValueError("expected a mono WAV")
        if wav.getsampwidth() != 2:
            raise ValueError("expected 16-bit PCM WAV")
        rate = wav.getframerate()
        samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2")
    return samples.astype(np.float64), rate


def frame_metrics(samples: np.ndarray, rate: int, frame_ms: float, hop_ms: float):
    frame = max(32, int(rate * frame_ms / 1000.0))
    hop = max(8, int(rate * hop_ms / 1000.0))
    window = np.hanning(frame)
    freqs = np.fft.rfftfreq(frame, 1.0 / rate)
    band = (freqs >= 300.0) & (freqs <= 3500.0)
    band_freqs = freqs[band]

    rows = []
    for start in range(0, max(0, len(samples) - frame + 1), hop):
        segment = samples[start:start + frame]
        spectrum = np.abs(np.fft.rfft(segment * window)) ** 2
        band_power = spectrum[band]
        peak_index = int(np.argmax(band_power))
        peak_power = float(band_power[peak_index])
        median_power = float(np.median(band_power)) + 1e-12
        tonality = 10.0 * math.log10(peak_power / median_power + 1e-12)
        rows.append(
            {
                "start": start,
                "time_s": start / rate,
                "rms": float(np.sqrt(np.mean(segment * segment))),
                "peak_frequency_hz": float(band_freqs[peak_index]),
                "tonality_db": tonality,
            }
        )
    return rows, frame, hop


def group_indices(indices: list[int], max_gap: int = 2) -> list[list[int]]:
    groups: list[list[int]] = []
    for index in indices:
        if groups and index <= groups[-1][-1] + max_gap:
            groups[-1].append(index)
        else:
            groups.append([index])
    return groups


def analyse(wav_path: Path, pause_threshold: float | None, min_tone_ms: float):
    samples, rate = read_wav(wav_path)
    if len(samples) == 0:
        raise ValueError("empty WAV")

    rows, frame, hop = frame_metrics(samples, rate, 20.0, 10.0)
    rms_values = np.array([row["rms"] for row in rows])

    if pause_threshold is None:
        # Keep the detector insensitive to isolated low-level Codec2 noise but
        # still classify quiet inter-word gaps as pauses.
        pause_threshold = max(80.0, float(np.percentile(rms_values, 15)) * 2.5)

    pause_flags = rms_values < pause_threshold
    candidate = [
        index for index, row in enumerate(rows)
        if pause_flags[index]
        and row["tonality_db"] >= 15.0
        and 500.0 <= row["peak_frequency_hz"] <= 3500.0
    ]

    tones: list[ToneArtefact] = []
    for group in group_indices(candidate):
        duration = len(group) * hop / rate
        if duration * 1000.0 < min_tone_ms:
            continue
        tones.append(
            ToneArtefact(
                start_s=rows[group[0]]["time_s"],
                duration_s=duration,
                frequency_hz=float(statistics.median(
                    rows[i]["peak_frequency_hz"] for i in group)),
                tonality_db=float(statistics.median(
                    rows[i]["tonality_db"] for i in group)),
                rms=float(statistics.median(rows[i]["rms"] for i in group)),
            )
        )

    abs_samples = np.abs(samples)
    differences = np.abs(np.diff(samples))
    local_rms = max(1.0, float(np.percentile(rms_values, 75)))
    click_threshold = max(12000.0, local_rms * 12.0)

    summary = {
        "file": str(wav_path),
        "sample_rate_hz": rate,
        "duration_s": len(samples) / rate,
        "peak": float(abs_samples.max()),
        "rms": float(np.sqrt(np.mean(samples * samples))),
        "dc": float(np.mean(samples)),
        "clipping_samples": int(np.count_nonzero(abs_samples >= 32000)),
        "pause_threshold": float(pause_threshold),
        "pause_fraction": float(np.mean(pause_flags)),
        "pause_rms_median": float(np.median(rms_values[pause_flags]))
        if np.any(pause_flags) else None,
        "speech_rms_median": float(np.median(rms_values[~pause_flags]))
        if np.any(~pause_flags) else None,
        "click_threshold": click_threshold,
        "click_count": int(np.count_nonzero(differences > click_threshold)),
        "max_sample_step": float(differences.max()) if len(differences) else 0.0,
        "tone_artefact_count": len(tones),
        "tone_artefact_total_s": float(sum(t.duration_s for t in tones)),
        "tone_artefacts": [asdict(t) for t in tones],
    }
    return summary


def print_summary(summary: dict):
    print(f"soubor: {summary['file']}")
    print(f"trvání: {summary['duration_s']:.2f} s, Fs={summary['sample_rate_hz']} Hz")
    print(
        f"RMS: {summary['rms']:.1f}, peak: {summary['peak']:.0f}, "
        f"DC: {summary['dc']:.2f}, clipping: {summary['clipping_samples']}"
    )
    print(
        f"pauzy: práh RMS {summary['pause_threshold']:.1f}, "
        f"podíl {100.0 * summary['pause_fraction']:.1f} %, "
        f"šumová podlaha {summary['pause_rms_median']:.1f}"
    )
    print(
        f"tónové artefakty v pauzách: {summary['tone_artefact_count']}, "
        f"celkem {summary['tone_artefact_total_s']:.2f} s"
    )
    print(
        f"kliky: {summary['click_count']} (práh kroku {summary['click_threshold']:.0f}, "
        f"max {summary['max_sample_step']:.0f})"
    )
    for item in summary["tone_artefacts"]:
        print(
            f"  {item['start_s']:7.3f} s  "
            f"{item['duration_s'] * 1000.0:5.0f} ms  "
            f"{item['frequency_hz']:6.0f} Hz  "
            f"tonalita {item['tonality_db']:.1f} dB  "
            f"RMS {item['rms']:.1f}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Measure tone artefacts, clicks and level in a decoded M17 WAV")
    parser.add_argument("wav", type=Path)
    parser.add_argument("--pause-threshold", type=float, default=None,
                        help="absolute RMS threshold for inter-word pauses")
    parser.add_argument("--min-tone-ms", type=float, default=30.0,
                        help="minimum duration of a reported tone artefact")
    parser.add_argument("--json", type=Path, default=None,
                        help="write the complete result as JSON")
    args = parser.parse_args()

    summary = analyse(args.wav, args.pause_threshold, args.min_tone_ms)
    print_summary(summary)
    if args.json is not None:
        args.json.write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
