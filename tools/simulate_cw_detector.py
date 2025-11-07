from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from typing import List

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

NUM_FREQ_BINS = 5
FREQ_BIN_SPACING = 100.0
TONE_THRESHOLD = 3000.0
NOISE_FLOOR_ALPHA = 0.10
TONE_RELEASE_FACTOR = 0.55
MIN_PEAK_RATIO = 1.8
MIN_DYNAMIC_THRESHOLD = 1200.0
TONE_RELEASE_BLOCKS = 0
DEFAULT_DIT_MS = 80
MIN_DIT_MS = 30
MAX_DIT_MS = 240


@dataclass
class GoertzelBin:
    target_freq: float
    coeff: float
    q1: float = 0.0
    q2: float = 0.0
    magnitude: float = 0.0


class CwDetectorSim:
    def __init__(self, center_freq: float, sampling_rate: float, samples_per_block: int) -> None:
        self.center_freq = center_freq
        self.sampling_rate = sampling_rate
        self.samples_per_block = samples_per_block
        self.freq_bins = self._init_bins()
        self.active_bin_index = NUM_FREQ_BINS // 2
        self.noise_floor = 0.0
        self.tone_present = False
        self.tone_release_counter = 0
        self.last_max = 0.0
        self.last_second = 0.0
        self.last_peak_ratio = 0.0
        self.last_activation = 0.0
        self.last_release = 0.0

    def _init_bins(self) -> List[GoertzelBin]:
        bins: List[GoertzelBin] = []
        center_index = NUM_FREQ_BINS // 2
        for i in range(NUM_FREQ_BINS):
            freq = self.center_freq + (i - center_index) * FREQ_BIN_SPACING
            k = (self.samples_per_block * freq) / self.sampling_rate
            omega = (2.0 * math.pi * k) / self.samples_per_block
            coeff = 2.0 * math.cos(omega)
            bins.append(GoertzelBin(freq, coeff))
        return bins

    def process_block(self, block: np.ndarray) -> bool:
        for sample in block:
            sample_f = float(sample)
            for b in self.freq_bins:
                q0 = b.coeff * b.q1 - b.q2 + sample_f
                b.q2 = b.q1
                b.q1 = q0

        max_mag = 0.0
        second_mag = 0.0
        max_index = self.active_bin_index

        for i, b in enumerate(self.freq_bins):
            mag_sq = (b.q1 * b.q1) + (b.q2 * b.q2) - (b.q1 * b.q2 * b.coeff)
            mag = math.sqrt(max(mag_sq, 0.0))
            b.magnitude = mag
            if mag > max_mag:
                second_mag = max_mag
                max_mag = mag
                max_index = i
            elif mag > second_mag:
                second_mag = mag
            b.q1 = 0.0
            b.q2 = 0.0

        noise_accum = 0.0
        noise_count = 0
        for i, b in enumerate(self.freq_bins):
            if abs(i - max_index) > 1:
                noise_accum += b.magnitude
                noise_count += 1
        if noise_count:
            noise_sample = noise_accum / noise_count
        else:
            noise_sample = second_mag if second_mag > 0.0 else max_mag * 0.5

        if self.noise_floor == 0.0:
            self.noise_floor = noise_sample
        else:
            self.noise_floor = self.noise_floor * (1.0 - NOISE_FLOOR_ALPHA) + noise_sample * NOISE_FLOOR_ALPHA

        activation_threshold = max(self.noise_floor + TONE_THRESHOLD, MIN_DYNAMIC_THRESHOLD)
        release_threshold = max(self.noise_floor + (TONE_THRESHOLD * TONE_RELEASE_FACTOR), MIN_DYNAMIC_THRESHOLD * 0.5)
        denom = second_mag if second_mag > 1.0 else activation_threshold
        peak_ratio = (max_mag / denom) if denom > 0.0 else max_mag

        tone_detected = self.tone_present
        if not self.tone_present:
            if max_mag >= activation_threshold and peak_ratio >= MIN_PEAK_RATIO:
                tone_detected = True
                self.tone_release_counter = TONE_RELEASE_BLOCKS
                self.active_bin_index = max_index
            else:
                tone_detected = False

        if tone_detected:
            if max_mag >= release_threshold:
                self.tone_release_counter = TONE_RELEASE_BLOCKS
                self.active_bin_index = max_index
            elif self.tone_release_counter > 0:
                self.tone_release_counter -= 1
            else:
                tone_detected = False

        self.tone_present = tone_detected
        if not tone_detected:
            self.tone_release_counter = 0

        self.last_max = max_mag
        self.last_second = second_mag
        self.last_peak_ratio = peak_ratio
        self.last_activation = activation_threshold
        self.last_release = release_threshold
        return tone_detected


def main() -> None:
    path = Path("test/cw-rtty/cw-15wpm-800Hz.wav")
    fs, data = wavfile.read(path)
    if data.dtype == np.uint8:
        # Átalakítás 12 bites skálára (ADC offset eltávolítása + felskálázás)
        signal = (data.astype(np.float64) - 128.0) * (2048.0 / 128.0)
    else:
        signal = data.astype(np.float64)
        signal = signal * (2048.0 / np.max(np.abs(signal)))

    target_fs = 3750
    if fs != target_fs:
        up = target_fs
        down = fs
        signal = resample_poly(signal, up, down)
        fs = target_fs

    block = 128
    detector = CwDetectorSim(center_freq=800.0, sampling_rate=fs, samples_per_block=block)
    blocks = len(signal) // block
    tone_flags = []
    metrics = []
    for i in range(blocks):
        segment = signal[i * block:(i + 1) * block]
        detected = detector.process_block(segment)
        tone_flags.append(detected)
        metrics.append(
            (
                detector.last_max,
                detector.last_second,
                detector.noise_floor,
                detector.last_peak_ratio,
                detector.last_activation,
                detector.last_release,
            )
        )
    block_ms = 1000.0 * block / fs
    durations = []
    current = False
    start = None
    times = []
    for idx, flag in enumerate(tone_flags):
        t = idx * block_ms
        times.append((t, detector.noise_floor))
        if flag and not current:
            current = True
            start = t
        elif not flag and current:
            durations.append(t - (start if start is not None else t))
            current = False
            start = None
    if current and start is not None:
        durations.append((len(tone_flags) * block_ms) - start)

    print(f"Block duration: {block_ms:.2f} ms")
    print(f"Tone segments detected: {len([d for d in durations if d>0])}")
    if durations:
        print("Durations ms (first 10):", [round(d, 1) for d in durations[:10]])
        print("Min/Max/Mean:", min(durations), max(durations), sum(durations) / len(durations))
    else:
        print("No tone detected")

    # print sample metrics around first tone
    for idx in range(40, 65):
        if idx >= len(metrics):
            break
        mx, snd, noise, ratio, activation, release = metrics[idx]
        print(
            f"block {idx:04d}: max={mx:8.1f}, second={snd:8.1f}, noise={noise:7.1f}, "
            f"ratio={ratio:4.2f}, act={activation:7.1f}, rel={release:7.1f}, flag={tone_flags[idx]}"
        )

    off_blocks = [i for i, flag in enumerate(tone_flags) if not flag]
    print(f"first off blocks: {off_blocks[:20]}")


if __name__ == "__main__":
    main()
