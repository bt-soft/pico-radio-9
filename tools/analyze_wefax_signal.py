 #!/usr/bin/env python3
"""
WEFAX jel részletes analízis
- Frekvencia spektrum elemzés (FFT)
- Phasing detektálás és periódus mérés
- Multitone detektálás
- Képsorok elemzése

Használat:
  python analyze_wefax_signal.py <wav_file>
"""

import sys
import numpy as np
from scipy.io import wavfile
from scipy import signal
from scipy.fft import rfft, rfftfreq
import os

def analyze_spectrum(data, samplerate, duration_sec=5.0, label=""):
    """Frekvencia spektrum elemzés FFT-vel"""
    print(f"\n{'='*60}")
    print(f"FREKVENCIA SPEKTRUM ELEMZÉS {label}")
    print(f"{'='*60}")
    
    # Első N másodperc
    samples_to_analyze = int(duration_sec * samplerate)
    if samples_to_analyze > len(data):
        samples_to_analyze = len(data)
    
    chunk = data[:samples_to_analyze]
    
    # FFT számítás
    fft_values = np.abs(rfft(chunk))
    fft_freqs = rfftfreq(len(chunk), 1/samplerate)
    
    # Normalizálás
    fft_values = fft_values / len(chunk)
    
    # Keressük a top 10 csúcsot a releváns tartományban (300-3000 Hz)
    mask = (fft_freqs >= 300) & (fft_freqs <= 3000)
    relevant_freqs = fft_freqs[mask]
    relevant_fft = fft_values[mask]
    
    # Top 10 csúcs
    top_indices = np.argsort(relevant_fft)[-10:][::-1]
    
    print(f"[DATA] Elemzett szakasz: {duration_sec} sec ({samples_to_analyze:,} minta)")
    print(f"\n  TOP 10 FREKVENCIA CSÚCS (300-3000 Hz tartomány):")
    print(f"{'Rang':<6}{'Frekvencia':<15}{'Amplitúdó':<15}{'Relatív %':<12}")
    print("-" * 60)
    
    max_amp = relevant_fft[top_indices[0]]
    
    for i, idx in enumerate(top_indices):
        freq = relevant_freqs[idx]
        amp = relevant_fft[idx]
        rel_pct = (amp / max_amp) * 100
        print(f"#{i+1:<5}{freq:>8.1f} Hz    {amp:>10.1f}      {rel_pct:>6.1f}%")
    
    # Próbáljuk meg azonosítani a fekete és fehér frekvenciákat
    print(f"\n[SEARCH] FREKVENCIA AZONOSÍTÁS:")
    
    # Az első két legerősebb csúcs valószínűleg a fekete és fehér
    if len(top_indices) >= 2:
        freq1 = relevant_freqs[top_indices[0]]
        freq2 = relevant_freqs[top_indices[1]]
        amp1 = relevant_fft[top_indices[0]]
        amp2 = relevant_fft[top_indices[1]]
        
        # Rendezzük frekvencia szerint
        if freq1 > freq2:
            freq1, freq2 = freq2, freq1
            amp1, amp2 = amp2, amp1
        
        separation = freq2 - freq1
        
        print(f"   Valószínű FEKETE frekvencia: {freq1:.1f} Hz (amplitúdó: {amp1:.1f})")
        print(f"   Valószínű FEHÉR frekvencia: {freq2:.1f} Hz (amplitúdó: {amp2:.1f})")
        print(f"   Frekvencia szeparáció: {separation:.1f} Hz")
        
        # Standard CCIR-476: fekete=1500 Hz, fehér=2300 Hz (800 Hz szeparáció)
        black_offset = freq1 - 1500
        white_offset = freq2 - 2300
        
        print(f"\n   [RULER] Eltérés a standard CCIR-476-tól:")
        print(f"      Fekete: {black_offset:+.1f} Hz (1500 Hz helyett {freq1:.1f} Hz)")
        print(f"      Fehér: {white_offset:+.1f} Hz (2300 Hz helyett {freq2:.1f} Hz)")
        
        if abs(black_offset) > 200 or abs(white_offset) > 200:
            print(f"   [!]  FIGYELEM: Nagy eltérés az standard frekvenciáktól (>200 Hz)!")
            print(f"      A dekóder frekvencia kalibrációjának működnie kell!")
    
    return fft_freqs, fft_values

def detect_phasing_structure(data, samplerate, black_freq, white_freq):
    """Phasing struktúra detektálása Goertzel algoritmussal"""
    print(f"\n{'='*60}")
    print(f"PHASING STRUKTÚRA ELEMZÉS")
    print(f"{'='*60}")
    print(f"Frekvenciák: fekete={black_freq:.1f} Hz, fehér={white_freq:.1f} Hz")
    
    # Blokkos feldolgozás (mint a dekóder)
    block_size = 64
    num_blocks = len(data) // block_size
    
    # Goertzel számítás blokkonként
    def goertzel_power(freq, samples, samplerate):
        N = len(samples)
        k = int(0.5 + (N * freq) / samplerate)
        w = (2.0 * np.pi * k) / N
        cosine = np.cos(w)
        coeff = 2.0 * cosine
        
        q0 = 0.0
        q1 = 0.0
        q2 = 0.0
        
        for sample in samples:
            q0 = coeff * q1 - q2 + sample
            q2 = q1
            q1 = q0
        
        real = q1 - q2 * cosine
        imag = q2 * np.sin(w)
        power = (real * real + imag * imag) / N
        
        return power
    
    black_powers = []
    white_powers = []
    timestamps = []
    
    print(f"[DATA] Feldolgozás: {num_blocks:,} blokk ({block_size} minta/blokk)...")
    
    # Első 10000 blokk elemzése (400000 minta = 10 sec @ 40kHz)
    max_blocks = min(10000, num_blocks)
    
    for i in range(max_blocks):
        start = i * block_size
        end = start + block_size
        block = data[start:end].astype(np.float64)
        
        black_power = goertzel_power(black_freq, block, samplerate)
        white_power = goertzel_power(white_freq, block, samplerate)
        
        black_powers.append(black_power)
        white_powers.append(white_power)
        timestamps.append(i * block_size / samplerate * 1000)  # ms
    
    black_powers = np.array(black_powers)
    white_powers = np.array(white_powers)
    timestamps = np.array(timestamps)
    
    # Szint detektálás (mint a dekóder)
    PHASING_HYSTERESIS = 5.0
    MIN_PHASING_POWER = 500.0
    
    total_powers = black_powers + white_powers
    
    levels = []
    for i in range(len(black_powers)):
        if total_powers[i] > MIN_PHASING_POWER:
            if white_powers[i] > black_powers[i] * PHASING_HYSTERESIS:
                levels.append(1)  # WHITE
            elif black_powers[i] > white_powers[i] * PHASING_HYSTERESIS:
                levels.append(0)  # BLACK
            else:
                levels.append(-1)  # Uncertain
        else:
            levels.append(-1)  # Noise
    
    levels = np.array(levels)
    
    # Élek detektálása (WHITE->BLACK falling edge)
    edges = []
    last_level = 0
    
    for i in range(1, len(levels)):
        if levels[i] == 0 and last_level == 1:  # Falling edge
            edges.append(i)
        if levels[i] != -1:
            last_level = levels[i]
    
    print(f"\n[SEARCH] PHASING DETEKTÁLÁS EREDMÉNYE:")
    print(f"   Detektált lefutó élek (WHITE BLACK): {len(edges)}")
    
    if len(edges) >= 2:
        # Periódusok számítása
        periods = []
        for i in range(1, len(edges)):
            period_blocks = edges[i] - edges[i-1]
            period_ms = period_blocks * block_size / samplerate * 1000
            periods.append(period_ms)
        
        periods = np.array(periods)
        
        print(f"\n[RULER] PHASING PERIÓDUSOK:")
        print(f"   Periódusok száma: {len(periods)}")
        print(f"   Átlag: {np.mean(periods):.2f} ms")
        print(f"   Medián: {np.median(periods):.2f} ms")
        print(f"   Min: {np.min(periods):.2f} ms")
        print(f"   Max: {np.max(periods):.2f} ms")
        print(f"   Szórás: {np.std(periods):.2f} ms")
        
        # Első 20 periódus kiírása
        print(f"\n   Első 20 periódus:")
        for i in range(min(20, len(periods))):
            print(f"      #{i+1:2d}: {periods[i]:6.1f} ms")
        
        # Várható érték
        print(f"\n   [INFO] Várható periódus IOC576 esetén: 500.0 ms")
        print(f"      Mért átlag: {np.mean(periods):.1f} ms")
        
        avg_period = np.mean(periods)
        if abs(avg_period - 500) < 10:
            print(f"   [OK] Phasing periódus MEGFELELŐ (~500 ms)")
        elif abs(avg_period - 1000) < 20:
            print(f"   [!]  Phasing periódus ~1000 ms - valószínűleg IOC288!")
        else:
            print(f"   [X] Phasing periódus NEM MEGFELELŐ! ({avg_period:.1f} ms)")
            print(f"      Várható: 500 ms (IOC576) vagy 1000 ms (IOC288)")
    else:
        print(f"   [X] NINCS elég phasing él detektálva!")
        print(f"      Lehet, hogy rossz frekvenciák, vagy nincs phasing a jelben.")

def analyze_multitone(data, samplerate, duration_sec=2.0, label=""):
    """Multitone szakasz elemzése"""
    print(f"\n{'='*60}")
    print(f"MULTITONE ELEMZÉS {label}")
    print(f"{'='*60}")
    
    samples = int(duration_sec * samplerate)
    if samples > len(data):
        samples = len(data)
    
    chunk = data[:samples]
    
    # FFT
    fft_values = np.abs(rfft(chunk))
    fft_freqs = rfftfreq(len(chunk), 1/samplerate)
    
    # Normalizálás
    fft_values = fft_values / len(chunk)
    
    # 300-3000 Hz tartomány
    mask = (fft_freqs >= 300) & (fft_freqs <= 3000)
    relevant_freqs = fft_freqs[mask]
    relevant_fft = fft_values[mask]
    
    # Csúcsok keresése (lokális maximumok)
    from scipy.signal import find_peaks
    peaks, properties = find_peaks(relevant_fft, height=np.max(relevant_fft) * 0.1)
    
    peak_freqs = relevant_freqs[peaks]
    peak_amps = relevant_fft[peaks]
    
    # Rendezés amplitúdó szerint
    sorted_indices = np.argsort(peak_amps)[::-1]
    
    print(f"[MUSIC] Detektált frekvencia komponensek (>10% a maximumhoz képest):")
    print(f"{'#':<4}{'Frekvencia':<15}{'Amplitúdó':<15}{'Relatív %':<12}")
    print("-" * 60)
    
    if len(sorted_indices) > 0:
        max_amp = peak_amps[sorted_indices[0]]
        
        for i, idx in enumerate(sorted_indices[:15]):  # Top 15
            freq = peak_freqs[idx]
            amp = peak_amps[idx]
            rel_pct = (amp / max_amp) * 100
            print(f"{i+1:<4}{freq:>8.1f} Hz    {amp:>10.1f}      {rel_pct:>6.1f}%")
    else:
        print("   Nincs jelentős csúcs detektálva")
    
    print(f"\n[INFO] Multitone jellemzők:")
    print(f"   Detektált csúcsok: {len(peak_freqs)}")
    print(f"   Elemzett időtartam: {duration_sec} sec")
    
    if len(peak_freqs) > 5:
        print(f"   [OK] Valószínűleg MULTITONE (sok frekvencia komponens)")
        
        # Frekvencia távolságok elemzése (egyenletes lépésköz?)
        if len(peak_freqs) >= 3:
            sorted_peak_freqs = np.sort(peak_freqs)
            freq_diffs = np.diff(sorted_peak_freqs)
            avg_step = np.mean(freq_diffs)
            std_step = np.std(freq_diffs)
            
            print(f"   Frekvencia lépésköz:")
            print(f"      Átlag: {avg_step:.1f} Hz")
            print(f"      Szórás: {std_step:.1f} Hz")
            
            if std_step < avg_step * 0.3:  # Ha a szórás kicsi
                print(f"      [OK] Egyenletes lépésköz - szabályos multitone")
            else:
                print(f"      [!]  Változó lépésköz - nem szabályos multitone")
    else:
        print(f"   [!]  Kevés komponens - lehet, hogy NEM multitone")

def main():
    if len(sys.argv) < 2:
        print("Használat: python analyze_wefax_signal.py <wav_file>")
        sys.exit(1)
    
    wav_file = sys.argv[1]
    
    if not os.path.exists(wav_file):
        print(f"[X] Fájl nem található: {wav_file}")
        sys.exit(1)
    
    print(f"\n{'='*60}")
    print(f"WEFAX JEL RÉSZLETES ANALÍZIS")
    print(f"{'='*60}")
    print(f"[FILE] Fájl: {os.path.basename(wav_file)}")
    
    # WAV beolvasás
    samplerate, data = wavfile.read(wav_file)
    
    if data.ndim > 1:
        data = np.mean(data, axis=1).astype(data.dtype)
    
    print(f"[AUDIO] Mintavételi frekvencia: {samplerate} Hz")
    print(f"[TIME]  Időtartam: {len(data) / samplerate:.2f} sec")
    print(f"[DATA] Minták: {len(data):,}")
    
    # 1. KEZDŐ Multitone elemzés (első 2 sec)
    print(f"\n{'#'*60}")
    print(f"# 1. FÁZIS: KEZDŐ MULTITONE")
    print(f"{'#'*60}")
    analyze_multitone(data, samplerate, duration_sec=2.0, label="(első 2 sec)")
    
    # 2. Frekvencia spektrum (5-15 sec - phasing valószínű helye)
    print(f"\n{'#'*60}")
    print(f"# 2. FÁZIS: PHASING ZÓNA")
    print(f"{'#'*60}")
    skip_sec = 5.0
    skip_samples = int(skip_sec * samplerate)
    if skip_samples < len(data):
        fft_freqs, fft_values = analyze_spectrum(
            data[skip_samples:], samplerate, 
            duration_sec=10.0, 
            label="(5-15 sec, phasing)"
        )
        
        # Azonosított frekvenciák
        mask = (fft_freqs >= 300) & (fft_freqs <= 3000)
        relevant_freqs = fft_freqs[mask]
        relevant_fft = fft_values[mask]
        top_indices = np.argsort(relevant_fft)[-2:][::-1]
        
        if len(top_indices) >= 2:
            freq1 = relevant_freqs[top_indices[0]]
            freq2 = relevant_freqs[top_indices[1]]
            
            if freq1 > freq2:
                freq1, freq2 = freq2, freq1
            
            # 3. Phasing struktúra detektálás
            print(f"\n{'#'*60}")
            print(f"# 3. FÁZIS: PHASING PERIÓDUS MÉRÉS")
            print(f"{'#'*60}")
            detect_phasing_structure(
                data[skip_samples:], 
                samplerate, 
                black_freq=freq1, 
                white_freq=freq2
            )
    
    # 4. VÉGSŐ Multitone elemzés (utolsó 5 sec)
    print(f"\n{'#'*60}")
    print(f"# 4. FÁZIS: VÉGSŐ MULTITONE")
    print(f"{'#'*60}")
    end_duration = 5.0
    end_samples = int(end_duration * samplerate)
    if end_samples < len(data):
        start_pos = len(data) - end_samples
        analyze_multitone(data[start_pos:], samplerate, duration_sec=5.0, label="(utolsó 5 sec)")
    
    # 5. TELJES JEL STRUKTÚRA ÁTTEKINTÉS
    print(f"\n{'#'*60}")
    print(f"# 5. TELJES JEL STRUKTÚRA")
    print(f"{'#'*60}")
    
    # Blokkos feldolgozás - detektáljuk a multitone zónákat
    block_size = 64
    blocks_per_sec = samplerate // block_size
    num_blocks = len(data) // block_size
    
    print(f"\n[SEARCH] JEL STRUKTÚRA DETEKTÁLÁS:")
    print(f"   Teljes idő: {len(data) / samplerate:.1f} sec")
    print(f"   Blokkok száma: {num_blocks:,}")
    print(f"   Blokkok/sec: {blocks_per_sec}")
    
    # Egyszerű RMS számítás blokkonként (multitone detektáláshoz)
    print(f"\n   Időbeli RMS profil készítése...")
    rms_values = []
    time_values = []
    
    for i in range(0, num_blocks, 10):  # Minden 10. blokk (gyorsaság)
        start = i * block_size
        end = start + block_size
        if end > len(data):
            break
        
        block = data[start:end].astype(np.float64)
        rms = np.sqrt(np.mean(block**2))
        rms_values.append(rms)
        time_values.append(i * block_size / samplerate)
    
    rms_values = np.array(rms_values)
    time_values = np.array(time_values)
    
    # Keressük a magas RMS szakaszokat (multitone/phasing)
    rms_threshold = np.mean(rms_values) * 1.5
    high_rms_mask = rms_values > rms_threshold
    
    # Összefüggő szakaszok keresése
    print(f"\n   [DATA] MAGAS JELERŐSSÉG  SZAKASZOK (RMS > {rms_threshold:.0f}):")
    
    in_section = False
    section_start = 0
    
    for i in range(len(high_rms_mask)):
        if high_rms_mask[i] and not in_section:
            # Szakasz kezdete
            section_start = time_values[i]
            in_section = True
        elif not high_rms_mask[i] and in_section:
            # Szakasz vége
            section_end = time_values[i-1] if i > 0 else time_values[i]
            duration = section_end - section_start
            print(f"      {section_start:6.1f} - {section_end:6.1f} sec  (időtartam: {duration:5.1f} sec)")
            in_section = False
    
    # Ha a vége is magas RMS
    if in_section:
        section_end = time_values[-1]
        duration = section_end - section_start
        print(f"      {section_start:6.1f} - {section_end:6.1f} sec  (időtartam: {duration:5.1f} sec)")
    
    print(f"\n   [INFO] Értelmezés:")
    print(f"      - Első magas RMS szakasz: Kezdő multitone + phasing")
    print(f"      - Középső alacsony RMS: Képsorok (gyengébb jel)")
    print(f"      - Utolsó magas RMS szakasz: Végső multitone")
    
    print(f"\n{'='*60}")
    print(f"[OK] ANALÍZIS BEFEJEZVE")
    print(f"{'='*60}\n")

if __name__ == '__main__':
    main()
