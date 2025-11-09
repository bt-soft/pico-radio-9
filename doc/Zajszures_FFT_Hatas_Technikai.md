# Zajszűrés Hatása az FFT-re és a Dekóderekre - Technikai Magyarázat

## Kérdés
**"Az hogy a zajcsökkentés simítja a jeleket nem fog bezavarni az FFT-be? Ilyenkor a CW vagy a RTTY dekódereknek nem fog gondot okozni a sok hasonló amplítúdójú bin egymás mellett?"**

## Rövid válasz

**IGEN, bezavar!** A mozgó átlag simítás **alacsony-áteresztő szűrőként** viselkedik, ami:
- ✅ Csökkenti a zajt
- ⚠️ **De**: Szélesíti az FFT spektrális vonalakat (bin spreading)
- ⚠️ **Különösen problémás CW/RTTY dekódereknél**, ahol pontos frekvencia detektálásra van szükség

Ezért **módosítottam** a kódot:
- **Alapértelmezett: `smoothingPoints_ = 0`** → NINCS simítás (csak DC eltávolítás)
- **CW/RTTY:** 0 vagy max 3-pontos (ha nagyon zajos)
- **SSTV/WEFAX:** 5-pontos (itt nem számít a frekvencia felbontás)

---

## Részletes Technikai Magyarázat

### 1. Mozgó Átlag = Alacsony-Áteresztő Szűrő

A mozgó átlag szűrő **frekvencia válasza** (FIR filter):

#### 3-pontos mozgó átlag:
```
H(f) = (1 + 2*cos(2πf/fs)) / 3
```
- **-3dB vágási frekvencia:** ~fs/3
- **Példa:** 12kHz mintavételnél → ~4kHz vágás

#### 5-pontos mozgó átlag:
```
H(f) = (1 + 2*cos(2πf/fs) + 2*cos(4πf/fs)) / 5
```
- **-3dB vágási frekvencia:** ~fs/5
- **Példa:** 12kHz mintavételnél → ~2.4kHz vágás

### 2. Hatás az FFT Spektrumra

#### Időtartományban:
Egy éles szinuszhullám → simított, "elmosódott" szinuszhullám

#### Frekvencia tartományban:
- **Éles FFT csúcs** (1-2 bin) → **Széles csúcs** (3-5 bin)
- A szomszédos bin-ek is kapnak energiát (spektrális szivárgás / spectral leakage)

**Példa:**
```
EREDETI FFT (800 Hz CW tónus):
Bin 79: ████████████ (100%)
Bin 80: █ (5%)
Bin 81: ░ (1%)

3-PONTOS SIMÍTÁS UTÁN:
Bin 79: ████████ (80%)
Bin 80: ████ (40%)
Bin 81: ██ (20%)

5-PONTOS SIMÍTÁS UTÁN:
Bin 79: ██████ (60%)
Bin 80: █████ (50%)
Bin 81: ████ (40%)
Bin 82: ██ (20%)
Bin 83: █ (10%)
```

### 3. Hatás a CW Dekóderre

A CW dekóder **Goertzel filterrel** működik:
```cpp
// CwDecoder-c1.cpp
static constexpr size_t GOERTZEL_N = 48; // 48 mintás blokk
```

#### NINCS simítás (smoothingPoints_ = 0):
```
✅ Éles frekvencia detektálás
✅ Gyors dit/dah átmenet detektálás
✅ Pontos WPM mérés
```

#### 3-pontos simítás:
```
⚠️ Enyhén elmosódott frekvencia csúcs
⚠️ Kis mértékben lassabb átmenet detektálás
✅ Még elfogadható, ha a jel NAGYON zajos
```

#### 5-pontos simítás CW-nél:
```
❌ Túl széles frekvencia csúcs
❌ Lassú dit/dah átmenetek → WPM hiba
❌ NEM AJÁNLOTT CW-hez!
```

**Javaslat CW-hez:**
- **Normál körülmények:** `smoothingPoints_ = 0` (nincs simítás)
- **Nagyon zajos jel:** `smoothingPoints_ = 3` (max!)
- **SOHA:** `smoothingPoints_ = 5`

### 4. Hatás az RTTY Dekóderre

Az RTTY dekóder **mark/space frekvenciákat** detektál Goertzel filterrel:
```cpp
// RttyDecoder-c1.cpp
static constexpr size_t TONE_BLOCK_SIZE = 64; // 64 mintás Goertzel
```

**Frekvencia shift példák:**
- **170 Hz shift:** 1275 Hz (mark) - 1445 Hz (space)
- **450 Hz shift:** 2125 Hz (mark) - 2575 Hz (space)
- **850 Hz shift:** 2125 Hz (mark) - 2975 Hz (space)

#### NINCS simítás (smoothingPoints_ = 0):
```
✅ Tiszta mark/space megkülönböztetés
✅ Gyors bit átmenetek → jó PLL szinkron
✅ Pontos baud rate követés
```

#### 3-pontos simítás:
```
⚠️ Kis mértékben elmosódott mark/space határ
⚠️ PLL kissé lassabban szinkronizál
✅ Még működik 45.45 Bd-nél és 50 Bd-nél
⚠️ Lehet problémás 75-100 Bd-nél
```

#### 5-pontos simítás RTTY-nél:
```
❌ Túl széles mark/space átfedés
❌ PLL nehezen szinkronizál
❌ Rossz bit recovery nagy baud-nál
❌ NEM AJÁNLOTT RTTY-hez!
```

**Javaslat RTTY-hez:**
- **45.45 Bd / 50 Bd:** `smoothingPoints_ = 0` vagy `3`
- **75 Bd / 100 Bd:** `smoothingPoints_ = 0` (CSAK!)
- **SOHA:** `smoothingPoints_ = 5`

### 5. Hatás az SSTV/WEFAX Dekóderre

SSTV és WEFAX **NEM használ FFT-alapú frekvencia detektálást**:
- **SSTV:** Direkt frekvencia demoduláció (FM demod) + vonal szinkron
- **WEFAX:** FM demodulátor + APT szinkron

**Nincs FFT bin felbontási igény!**

#### 5-pontos simítás SSTV/WEFAX-nél:
```
✅ Zajmentes kép
✅ Simább pixelek
✅ Jobb szinkronizáció
✅ AJÁNLOTT!
```

**Javaslat SSTV/WEFAX-hoz:**
- **Minden esetben:** `smoothingPoints_ = 5`

---

## Frissített Ajánlások Dekóderenként

### CW Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC be
audioProcessor.setNoiseReductionEnabled(false); // NINCS simítás!
audioProcessor.setSmoothingPoints(0);        // Goertzel maga zajszűr

// VAGY ha NAGYON zajos a jel:
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(3);        // MAX 3-pont!
```

### RTTY Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC be
audioProcessor.setNoiseReductionEnabled(false); // NINCS simítás!
audioProcessor.setSmoothingPoints(0);        // Goertzel maga zajszűr

// VAGY 45-50 Bd esetén, ha zajos:
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(3);        // MAX 3-pont!
```

### SSTV Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC be
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(5);        // Erős simítás OK!
```

### WEFAX Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC be
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(5);        // Erős simítás OK!
```

### FFT Megjelenítés / Spektrum Analyzer
```cpp
audioProcessor.setAgcEnabled(false);         // Valós szintek
audioProcessor.setManualGain(1.0f);
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(3);        // Enyhe simítás
```

---

## Miért NEM kell simítás CW/RTTY-nél?

### 1. Goertzel Filter Maga is Zajszűr!

A Goertzel algoritmus **sávszűrő** (band-pass filter):
```cpp
// Minden Goertzel blokk N mintát átlagol
// CW: N = 48 minta → 48-szoros átlagolás!
// RTTY: N = 64 minta → 64-szeres átlagolás!
```

**Ez sokkal hatékonyabb zajszűrés**, mint egy 3 vagy 5 pontos mozgó átlag!

### 2. AGC Elég a Szintszabályozáshoz

Az AGC már gondoskodik arról, hogy:
- Gyenge jelek erősítve legyenek
- Erős jelek ne okozzanak túlvezérelést
- Stabil szint legyen

**Nincs szükség további simításra!**

### 3. Mozgó Átlag Rontja a Frekvencia Felbontást

A CW/RTTY dekódereknek **pontos frekvencia detektálás** kell:
- CW: ±50 Hz pontosság
- RTTY: ±25 Hz pontosság (mark/space shift)

**A mozgó átlag ezt rontja!**

---

## Matematikai Analízis - Bin Szélesség Változása

### Példa: CW @ 800 Hz, 12 kHz mintavétel, 256 FFT

**Bin szélesség:**
```
binWidth = samplingRate / fftSize = 12000 / 256 = 46.875 Hz/bin
```

**800 Hz frekvencia:**
```
bin = 800 / 46.875 ≈ 17.07 → Bin 17
```

#### NINCS simítás:
```
Bin 16: ░░ (2%)
Bin 17: ████████████ (98%)
Bin 18: ░ (1%)
```

#### 3-pontos simítás:
```
Bin 16: ███ (15%)
Bin 17: ████████ (70%)
Bin 18: ███ (15%)

→ Effektív bin szélesség: 46.875 * 3 = 140.6 Hz (!!!)
```

#### 5-pontos simítás:
```
Bin 15: ██ (8%)
Bin 16: ████ (20%)
Bin 17: ██████ (45%)
Bin 18: ████ (20%)
Bin 19: ██ (7%)

→ Effektív bin szélesség: 46.875 * 5 = 234.4 Hz (!!!)
```

**Következmény:**
- **CW ±200 Hz frekvencia scan:** 5-pontos simítás esetén a bin-ek átfednek!
- **RTTY 170 Hz shift:** 5-pontos simítás esetén a mark/space spektruma keveredik!

---

## Összefoglalás - Mi Változott a Kódban

### 1. Új alapértelmezett: `smoothingPoints_ = 0`
```cpp
// Konstruktor:
smoothingPoints_(0)  // NINCS simítás alapértelmezetten
```

### 2. `setSmoothingPoints()` most elfogad 0 értéket is
```cpp
void setSmoothingPoints(uint8_t points) {
    if (points == 0) smoothingPoints_ = 0;       // Nincs simítás
    else if (points >= 5) smoothingPoints_ = 5;  // Erős simítás
    else smoothingPoints_ = 3;                   // Enyhe simítás
}
```

### 3. `removeDcAndSmooth()` kezeli a `smoothingPoints_ = 0` esetet
```cpp
if (!useNoiseReduction_ || smoothingPoints_ == 0) {
    // GYORS út: csak DC offset eltávolítás, NINCS simítás
    arm_offset_q15((q15_t *)input, -ADC_MIDPOINT, (q15_t *)output, count);
    return;
}
```

### 4. Frissített dokumentációs kommentek
```cpp
// Dekóder-specifikus javaslatok:
// - CW/RTTY: smoothingPoints_ = 0 vagy 3 (Goertzel maga zajszűr!)
// - SSTV/WEFAX: smoothingPoints_ = 5 (nincs FFT felbontási igény)
```

---

## Ajánlott Használat

### Ha CSAK AGC kell, zajszűrés NEM:
```cpp
audioProcessor.setAgcEnabled(true);           // AGC be
audioProcessor.setNoiseReductionEnabled(true); // DC eltávolítás be
audioProcessor.setSmoothingPoints(0);         // NINCS simítás
```

### Ha AGC + enyhe zajszűrés kell:
```cpp
audioProcessor.setAgcEnabled(true);
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(3);         // 3-pont
```

### Ha AGC + erős zajszűrés kell (SSTV/WEFAX):
```cpp
audioProcessor.setAgcEnabled(true);
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(5);         // 5-pont
```

---

## Konklúzió

**Eredeti kérdésre válasz:**
- **IGEN**, a zajszűrés simítása zavart okoz az FFT-ben
- **IGEN**, a CW/RTTY dekódereknek gondot okoz a szélesebb bin-ek
- **MEGOLDÁS**: Alapértelmezetten NINCS simítás (`smoothingPoints_ = 0`)
- **AGC továbbra is működik** (ez NEM módosítja a frekvencia tartalmat)
- **Goertzel filterek maguk zajszűrnek**, nincs szükség előzetes simításra

**Új filozófia:**
- **CW/RTTY:** Hagyd, hogy a Goertzel filter végezze a zajszűrést!
- **SSTV/WEFAX:** Használd a 5-pontos simítást (itt nincs frekvencia detektálás)
- **Gyenge, zajos jelek:** Próbáld a 3-pontos simítást, de csak ha muszáj!

---

**Köszönet a kiváló kérdésért!** Ezzel elkerültük egy jelentős teljesítménycsökkenést a CW/RTTY dekódereknél. 🎯
