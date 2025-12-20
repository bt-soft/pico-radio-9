# UICompSpectrumVis Refaktorálás - 2024.12.20

## Probléma
Az AudioProcessor Q15 FFT-re váltott, a magnitude értékek jelentősen kisebbek (~200-500 vs. ~32767).
Minden vizualizációs módban különböző gain számítás volt, ami redundáns és hibás volt.

## Megoldás

### 1. Új helper függvények (UICompSpectrumVis.cpp, ~90-240 sorok)

#### `q15ToFloatWithGain(q15_t magQ15, float gain)`
- Biztonságos Q15 → float konverzió gain-nel
- Constrain 0..255 tartományba
- Nem csordulhat túl

#### `q15ToUint8Safe(q15_t magQ15, float gain)`
- Q15 → uint8 (0..255) konverzió
- Használja a `q15ToFloatWithGain` függvényt

#### `q15ToPixelHeightSafe(q15_t magQ15, float gain, uint16_t maxHeight)`
- Q15 → pixel magasság konverzió
- Normalizálva 0..maxHeight tartományba

#### `calculateDisplayGain(magnitudeData, minBin, maxBin, isAutoGain, manualGainDb)`
**KÖZÖS GAIN SZÁMÍTÁS MINDEN MÓDHOZ**
- Automatikus mód: maximum magnitude alapján, cél 200/255 kihasználtság, 50x minimum gain
- Manuális mód: 300x alapértelmezett, vagy dB alapú (gainDb * 150x)
- Simítás: 90% régi + 10% új (lassú változás)
- Gain tartomány: 50-500x

### 2. Refaktorált módok

#### ✅ Envelope (renderEnvelope)
- Új gain számítás: `calculateDisplayGain()` használata
- Buffer explicit scroll (wabuf_ tölt fel jobbról balra)
- 4x skálázás (agresszív megjelenítés)
- Középvonalról szimmetrikus rajzolás (cián szín)

#### ✅ Waterfall (renderWaterfall)  
- Új gain számítás: `calculateDisplayGain()` használata
- `q15ToUint8Safe()` konverzió túlcsordulás nélkül
- Színes vízesés paletta (kék→zöld→sárga→piros)

#### 🔄 SpectrumBar (renderSpectrumBar)
**KÖVETKEZŐ LÉPÉS**: Átírni ugyanazon logikával
- Jelenlegi probléma: `q15ToPixelHeight` túlcsordulás
- Megoldás: `q15ToPixelHeightSafe()` + `calculateDisplayGain()`

#### 🔄 Oscilloscope (renderOscilloscope)
**KÖVETKEZŐ LÉPÉS**: Ellenőrizni és tesztelni

#### 🔄 CW/RTTY Tuning Aid
**KÖVETKEZŐ LÉPÉS**: Ellenőrizni és tesztelni

## Tesztelés

### Envelope mód ✅
- Gain: 300x manuális, 50-500x auto
- Skálázás: 4x (400% kihasználtság)
- Buffer scroll: működik
- Eredmény: Jól látható, dinamikus görbe

### Waterfall mód ✅
- Gain: 300x manuális
- Színek: láthatóak
- Eredmény: Működik

### Bars mód ❌
- Még nem refaktorálva
- Várható probléma: túlcsordulás nagy gain-nél

## Következő lépések

1. **SpectrumBar refaktorálás** (prioritás: MAGAS)
   - Cseréld `q15ToPixelHeight` → `q15ToPixelHeightSafe`
   - Használd `calculateDisplayGain()`
   - Távolítsd el a `Q15_CORRECTION_FACTOR` hacks-eket

2. **Oscilloscope ellenőrzés**
   - Nem használ FFT magnitude-ot, időtartománybeli adat
   - Valószínűleg nem kell módosítani

3. **Debug logok eltávolítása**
   - Envelope: remove excessive logging
   - Waterfall: remove excessive logging

4. **Dokumentáció**
   - Minden új függvényhez Doxygen komment
   - README frissítése

## Megjegyzések

- A régi `q15ToUint8` és `q15ToPixelHeight` függvények **deprecated**
- Backward compatibility miatt még benne vannak, de ne használd
- Minden új kódban használd a `*Safe` változatokat
- A `calculateDisplayGain()` az egyetlen igazság forrása a gain-hez!
