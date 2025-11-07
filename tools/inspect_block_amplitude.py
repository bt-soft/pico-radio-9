from __future__ import annotations

from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

path = Path("test/cw-rtty/cw-15wpm-800Hz.wav")
fs, data = wavfile.read(path)
if data.dtype == np.uint8:
    signal = (data.astype(np.float64) - 128.0)
else:
    signal = data.astype(np.float64)

# Skálázás ~12 bites tartományra
signal *= (2048.0 / 128.0)

if fs != 3750:
    signal = resample_poly(signal, 3750, fs)
    fs = 3750

block = 128
blocks = len(signal) // block
maximums = []
for i in range(blocks):
    seg = signal[i * block:(i + 1) * block]
    maximums.append(np.abs(seg).max())

print(f"blocks={blocks}")
print(f"max amplitude={max(maximums):.1f}, mean={np.mean(maximums):.1f}")
print(f"first 20 maxima: {[round(v,1) for v in maximums[:20]]}")
