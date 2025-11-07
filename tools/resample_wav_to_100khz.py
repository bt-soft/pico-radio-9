#!/usr/bin/env python3
"""
resample_wav_to_65khz.py
Átkonvertál WAV fájlokat 65535 Hz mintavételre a WEFAX dekóderhez.
(RP2040 ADC maximális frekvenciája: 65535 Hz)

Használat:
    python resample_wav_to_65khz.py input.wav [output.wav]
    
Ha nincs megadva output fájl, akkor input-65k.wav lesz a kimenet.
"""
import sys
import numpy as np
import scipy.io.wavfile as wav
from scipy.signal import resample_poly
from pathlib import Path

def resample_to_65khz(input_path, output_path=None):
    """
    Átkonvertál egy WAV fájlt 65535 Hz mintavételre.
    
    Args:
        input_path: Bemeneti WAV fájl útvonala
        output_path: Kimeneti WAV fájl útvonala (opcionális)
    """
    input_path = Path(input_path)
    
    if not input_path.exists():
        raise FileNotFoundError(f"Fájl nem található: {input_path}")
    
    # Output fájl név generálás ha nincs megadva
    if output_path is None:
        output_path = input_path.parent / f"{input_path.stem}-65k{input_path.suffix}"
    else:
        output_path = Path(output_path)
    
    print(f"Betöltés: {input_path}")
    sr, data = wav.read(str(input_path))
    
    print(f"  Eredeti mintavétel: {sr} Hz")
    print(f"  Eredeti hossz: {len(data)} minta ({len(data)/sr:.2f} s)")
    print(f"  Adattípus: {data.dtype}")
    print(f"  Csatornák: {data.shape}")
    
    # Mono konverzió ha sztereo
    if data.ndim > 1:
        print(f"  Sztereó → mono konverzió...")
        data = data.mean(axis=1).astype(data.dtype)
    
    # Resample 65.5 kHz-re (RP2040 ADC max frekvencia)
    target_sr = 65535
    
    if sr == target_sr:
        print(f"  ⚠️ Már 65535 Hz mintavétel, másolás...")
        wav.write(str(output_path), target_sr, data)
        print(f"✓ Mentve: {output_path}")
        return
    
    print(f"  Resample {sr} Hz → {target_sr} Hz...")
    
    # Számoljuk ki a resample arányt (egyszerűsítve)
    from math import gcd
    up = target_sr
    down = sr
    common = gcd(up, down)
    up //= common
    down //= common
    
    print(f"  Resample arány: {up}/{down} (↑{up}x ↓{down}x)")
    
    # Resample
    data_resampled = resample_poly(data, up, down)
    
    # Biztosítjuk hogy az eredeti adattípus maradjon
    if data.dtype == np.int16:
        data_resampled = np.clip(data_resampled, -32768, 32767).astype(np.int16)
    elif data.dtype == np.uint8:
        data_resampled = np.clip(data_resampled, 0, 255).astype(np.uint8)
    else:
        data_resampled = data_resampled.astype(data.dtype)
    
    print(f"  Új hossz: {len(data_resampled)} minta ({len(data_resampled)/target_sr:.2f} s)")
    
    # Mentés
    wav.write(str(output_path), target_sr, data_resampled)
    print(f"✓ Mentve: {output_path}")
    print(f"  Fájlméret: {output_path.stat().st_size / 1024 / 1024:.2f} MB")

def main():
    if len(sys.argv) < 2:
        print("Használat: python resample_wav_to_65khz.py input.wav [output.wav]")
        print()
        print("Példák:")
        print("  python resample_wav_to_65khz.py phase-sample.wav")
        print("  python resample_wav_to_65khz.py input.wav output-65k.wav")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None
    
    try:
        resample_to_65khz(input_path, output_path)
    except Exception as e:
        print(f"❌ Hiba: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
