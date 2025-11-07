#!/usr/bin/env python3
"""
WAV fájl átalakító script Raspberry Pi Pico-hoz
- Megvizsgálja az input WAV fájl paramétereit
- Átalakítja a megadott mintavételi frekvenciára (alapértelmezett: 40000 Hz)
- Mono csatornára konvertál (ha stereo)
- 16-bit signed integer formátumra konvertál
- Megőrzi az eredeti időtartamot

Használat:
  python convert_wav_for_pico.py input.wav [output.wav] [--samplerate 40000]

Példák:
  python convert_wav_for_pico.py test.wav
  python convert_wav_for_pico.py test.wav output.wav
  python convert_wav_for_pico.py test.wav output.wav --samplerate 48000
"""

import sys
import numpy as np
from scipy.io import wavfile
from scipy import signal
import argparse
import os

def format_duration(seconds):
    """Időtartam formázása ember-olvasható formában"""
    minutes = int(seconds // 60)
    secs = seconds % 60
    if minutes > 0:
        return f"{minutes}:{secs:05.2f} (perc:sec)"
    else:
        return f"{secs:.2f} sec"

def format_filesize(bytes):
    """Fájlméret formázása ember-olvasható formában"""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes < 1024.0:
            return f"{bytes:.2f} {unit}"
        bytes /= 1024.0
    return f"{bytes:.2f} TB"

def analyze_wav(filepath):
    """WAV fájl részletes analízise"""
    print(f"\n{'='*60}")
    print(f"INPUT FÁJL ANALÍZIS: {os.path.basename(filepath)}")
    print(f"{'='*60}")
    
    # Fájl méret
    filesize = os.path.getsize(filepath)
    print(f"📁 Fájlméret: {format_filesize(filesize)}")
    
    # WAV paraméterek beolvasása
    samplerate, data = wavfile.read(filepath)
    
    # Alap információk
    print(f"🔊 Mintavételi frekvencia: {samplerate} Hz")
    print(f"⏱️  Időtartam: {format_duration(len(data) / samplerate)}")
    print(f"📊 Minták száma: {len(data):,}")
    
    # Csatornák
    if data.ndim == 1:
        print(f"🎵 Csatornák: Mono (1 csatorna)")
        channels = 1
    else:
        print(f"🎵 Csatornák: Stereo ({data.shape[1]} csatorna)")
        channels = data.shape[1]
    
    # Adattípus és bit mélység
    dtype = data.dtype
    print(f"🔢 Adattípus: {dtype}")
    
    if dtype == np.int16:
        bit_depth = 16
        print(f"📏 Bit mélység: 16-bit signed integer")
    elif dtype == np.int32:
        bit_depth = 32
        print(f"📏 Bit mélység: 32-bit signed integer")
    elif dtype == np.float32:
        bit_depth = 32
        print(f"📏 Bit mélység: 32-bit float")
    elif dtype == np.float64:
        bit_depth = 64
        print(f"📏 Bit mélység: 64-bit float")
    elif dtype == np.uint8:
        bit_depth = 8
        print(f"📏 Bit mélység: 8-bit unsigned integer")
    else:
        bit_depth = dtype.itemsize * 8
        print(f"📏 Bit mélység: {bit_depth}-bit")
    
    # Amplitúdó statisztika
    if data.ndim == 1:
        data_mono = data
    else:
        data_mono = data[:, 0]  # Első csatorna
    
    min_val = np.min(data_mono)
    max_val = np.max(data_mono)
    mean_val = np.mean(data_mono)
    rms_val = np.sqrt(np.mean(data_mono.astype(np.float64)**2))
    
    print(f"\n📈 JELERŐSSÉG STATISZTIKA:")
    print(f"   Min: {min_val:,}")
    print(f"   Max: {max_val:,}")
    print(f"   Átlag: {mean_val:.2f}")
    print(f"   RMS: {rms_val:.2f}")
    
    # Dinamika tartomány
    if dtype in [np.int16, np.int32, np.int8]:
        max_possible = np.iinfo(dtype).max
        dynamic_range_db = 20 * np.log10(max_possible / (rms_val + 1e-10))
        print(f"   Dinamika tartomány: {dynamic_range_db:.1f} dB")
    
    return samplerate, data, channels, dtype

def convert_to_mono(data):
    """Stereo -> Mono konverzió (átlagolással)"""
    if data.ndim == 1:
        return data
    else:
        print(f"🔄 Stereo -> Mono konverzió (csatornák átlagolása)...")
        return np.mean(data, axis=1).astype(data.dtype)

def convert_to_int16(data, original_dtype):
    """Konverzió 16-bit signed integer formátumra"""
    if original_dtype == np.int16:
        return data
    
    print(f"🔄 {original_dtype} -> int16 konverzió...")
    
    if original_dtype in [np.float32, np.float64]:
        # Float (-1.0 ... +1.0) -> int16
        data = np.clip(data, -1.0, 1.0)
        return (data * 32767).astype(np.int16)
    elif original_dtype == np.int32:
        # int32 -> int16 (skálázás)
        return (data / 65536).astype(np.int16)
    elif original_dtype == np.uint8:
        # uint8 (0...255) -> int16 (-32768...32767)
        return ((data.astype(np.int32) - 128) * 256).astype(np.int16)
    else:
        # Általános konverzió
        return data.astype(np.int16)

def resample_audio(data, original_rate, target_rate):
    """Mintavételi frekvencia átalakítás (high-quality resampling)"""
    if original_rate == target_rate:
        print(f"✓ Mintavételi frekvencia már {target_rate} Hz - nincs szükség átalakításra")
        return data
    
    print(f"🔄 Mintavétel átalakítás: {original_rate} Hz -> {target_rate} Hz...")
    
    # Számítsuk ki az új minták számát
    num_samples = int(len(data) * target_rate / original_rate)
    
    # Használjuk a scipy.signal.resample függvényt (FFT-alapú, high quality)
    resampled = signal.resample(data, num_samples)
    
    # Ellenőrizzük, hogy nem lépte-e túl az int16 tartományt
    resampled = np.clip(resampled, -32768, 32767)
    
    return resampled.astype(np.int16)

def convert_wav(input_path, output_path, target_samplerate=40000):
    """Fő konverziós függvény"""
    
    # 1. Analízis
    original_rate, data, channels, dtype = analyze_wav(input_path)
    
    print(f"\n{'='*60}")
    print(f"KONVERZIÓ FOLYAMAT")
    print(f"{'='*60}")
    
    # 2. Mono konverzió
    data_mono = convert_to_mono(data)
    
    # 3. int16 konverzió
    data_int16 = convert_to_int16(data_mono, dtype)
    
    # 4. Resampling
    data_resampled = resample_audio(data_int16, original_rate, target_samplerate)
    
    # 5. Mentés
    print(f"💾 Mentés: {os.path.basename(output_path)}...")
    wavfile.write(output_path, target_samplerate, data_resampled)
    
    # 6. Eredmény összefoglaló
    output_filesize = os.path.getsize(output_path)
    output_duration = len(data_resampled) / target_samplerate
    
    print(f"\n{'='*60}")
    print(f"✅ KONVERZIÓ SIKERES!")
    print(f"{'='*60}")
    print(f"📁 Output fájl: {os.path.basename(output_path)}")
    print(f"📦 Fájlméret: {format_filesize(output_filesize)}")
    print(f"🔊 Mintavételi frekvencia: {target_samplerate} Hz")
    print(f"⏱️  Időtartam: {format_duration(output_duration)}")
    print(f"📊 Minták száma: {len(data_resampled):,}")
    print(f"🎵 Csatornák: Mono (1 csatorna)")
    print(f"📏 Bit mélység: 16-bit signed integer")
    print(f"{'='*60}\n")

def main():
    parser = argparse.ArgumentParser(
        description='WAV fájl átalakító Raspberry Pi Pico-hoz',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Példák:
  %(prog)s input.wav
  %(prog)s input.wav output.wav
  %(prog)s input.wav output.wav --samplerate 48000
  %(prog)s ~/Downloads/wefax_sample.wav test/wefax/wefax-40k.wav
        """
    )
    
    parser.add_argument('input', help='Input WAV fájl')
    parser.add_argument('output', nargs='?', help='Output WAV fájl (opcionális)')
    parser.add_argument('--samplerate', '-r', type=int, default=40000,
                        help='Cél mintavételi frekvencia Hz-ben (alapértelmezett: 40000)')
    
    args = parser.parse_args()
    
    # Input fájl ellenőrzése
    if not os.path.exists(args.input):
        print(f"❌ HIBA: Input fájl nem található: {args.input}")
        sys.exit(1)
    
    # Output fájlnév generálása, ha nincs megadva
    if args.output is None:
        base, ext = os.path.splitext(args.input)
        args.output = f"{base}-{args.samplerate}Hz{ext}"
    
    # Konverzió
    try:
        convert_wav(args.input, args.output, args.samplerate)
    except Exception as e:
        print(f"\n❌ HIBA a konverzió során:")
        print(f"   {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    main()
