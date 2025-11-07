from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from math import gcd
from scipy.io import wavfile
from scipy.signal import hilbert, resample_poly


def compute_envelope(signal: np.ndarray, smooth_samples: int = 64) -> np.ndarray:
    analytic = hilbert(signal)
    envelope = np.abs(analytic)
    if smooth_samples > 1:
        kernel = np.ones(smooth_samples, dtype=np.float64) / smooth_samples
        envelope = np.convolve(envelope, kernel, mode="same")
    return envelope


def detect_pulses(envelope: np.ndarray, fs: float, threshold_ratio: float = 0.35) -> list[tuple[float, float]]:
    if envelope.size == 0:
        return []

    threshold = envelope.max() * threshold_ratio
    active = envelope > threshold

    edges: list[tuple[int, int]] = []
    in_pulse = False
    start_idx = 0

    for idx, state in enumerate(active):
        if state and not in_pulse:
            in_pulse = True
            start_idx = idx
        elif not state and in_pulse:
            edges.append((start_idx, idx))
            in_pulse = False

    if in_pulse:
        edges.append((start_idx, len(active)))

    return [(start / fs * 1000.0, end / fs * 1000.0) for start, end in edges]


def summarise_durations(pulses: list[tuple[float, float]]) -> dict[str, float]:
    if not pulses:
        return {}

    durations = np.array([end - start for start, end in pulses])
    return {
        "count": len(durations),
        "min_ms": float(durations.min()),
        "max_ms": float(durations.max()),
        "mean_ms": float(durations.mean()),
        "median_ms": float(np.median(durations)),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyse CW WAV file and report tone durations")
    parser.add_argument("wav_path", type=Path, help="Path to the WAV file")
    parser.add_argument("--target-fs", type=float, default=3750.0, help="Target sample rate after resampling")
    parser.add_argument("--smooth", type=int, default=96, help="Smoothing window (samples) for the envelope")
    parser.add_argument("--threshold", type=float, default=0.35, help="Threshold ratio of envelope max")
    args = parser.parse_args()

    wav_path: Path = args.wav_path
    if not wav_path.exists():
        raise SystemExit(f"File not found: {wav_path}")

    fs, data = wavfile.read(str(wav_path))
    signal = data.astype(np.float64)

    if data.dtype == np.uint8:
        signal = (signal - 128.0) / 128.0
    elif data.dtype == np.int16:
        signal = signal / 32768.0
    elif data.dtype == np.int32:
        signal = signal / 2147483648.0
    else:
        signal = signal / np.abs(signal).max()

    if signal.ndim > 1:
        signal = signal.mean(axis=1)

    if abs(fs - args.target_fs) > 1.0:
        up = int(round(args.target_fs * 100))
        down = int(round(fs * 100))
        # Reduce fraction to keep resample_poly efficient
        factor = gcd(up, down) or 1
        up //= factor
        down //= factor
        signal = resample_poly(signal, up, down)
        fs = args.target_fs

    envelope = compute_envelope(signal, smooth_samples=args.smooth)
    pulses = detect_pulses(envelope, fs, threshold_ratio=args.threshold)
    summary = summarise_durations(pulses)

    print(f"Analysed: {wav_path}")
    print(f"Sample rate after resample: {fs:.2f} Hz")
    print(f"Envelope max: {envelope.max():.4f}")
    print("Pulse statistics:")
    if summary:
        for key, value in summary.items():
            print(f"  {key:>8}: {value:.2f}")
    else:
        print("  (no pulses detected)")

    print("First 10 pulse durations (ms):")
    for start_ms, end_ms in pulses[:10]:
        print(f"  start={start_ms:8.1f} ms end={end_ms:8.1f} ms dur={end_ms - start_ms:6.1f} ms")

    gaps = []
    for (prev_start, prev_end), (next_start, _) in zip(pulses, pulses[1:]):
        gaps.append(next_start - prev_end)

    if gaps:
        gaps_arr = np.array(gaps)
        print("Gap statistics (ms):")
        print(f"  count   : {len(gaps)}")
        print(f"  min_ms  : {gaps_arr.min():.2f}")
        print(f"  max_ms  : {gaps_arr.max():.2f}")
        print(f"  mean_ms : {gaps_arr.mean():.2f}")
        print(f"  median_ms: {np.median(gaps_arr):.2f}")
    else:
        print("Gap statistics: (not enough pulses)")


if __name__ == "__main__":
    main()
