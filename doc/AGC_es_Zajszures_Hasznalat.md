# AGC és Zajszűrés - Használati Útmutató

## Áttekintés

Az AudioProcessorC1 osztályba beépített AGC (Automatikus Erősítésszabályozás) és zajszűrés funkciók javítják az audio feldolgozás minőségét és dinamikáját.

## Új funkciók

### 1. AGC (Automatikus Erősítésszabályozás)

Az AGC automatikusan beállítja a jel erősítését, hogy:
- **Gyenge jelek** esetén növelje az erősítést → jobb dekódolási minőség
- **Erős jelek** esetén csökkentse az erősítést → túlvezérelés elkerülése
- **Stabil szintet** tartson → egyenletes dekódolási teljesítmény

**Működési elvek:**
- **Exponenciális mozgó átlag** a jel szintjének követésére
- **Attack/Release karakterisztika:**
  - Gyors attack (0.3): Ha a jel hirtelen erősödik, gyorsan csökkenti az erősítést
  - Lassú release (0.01): Ha a jel gyengül, lassan növeli az erősítést (természetes dinamika)
- **Cél amplitúdó:** ~60% a maximális q15 tartományból (20000 / 32768)
- **Erősítési tartomány:** 0.1x - 20x

### 2. Zajszűrés (Mozgó Átlagos Simítás)

A zajszűrés csökkenti az ADC zaj hatását **anélkül**, hogy módosítaná a minták számát:
- **0 = Nincs simítás**: Csak DC offset eltávolítás (alapértelmezett, CW/RTTY-hez ajánlott)
- **3-pontos mozgó átlag**: Gyorsabb, enyhe simítás (gyenge jelminőség esetén)
- **5-pontos mozgó átlag**: Lassabb, erős simítás (SSTV/WEFAX-hoz ajánlott)

**⚠️ FONTOS:** A mozgó átlag simítás **szélesíti az FFT bin-eket**!
- **CW/RTTY dekódereknél** ez **problémát okozhat** a frekvencia detektálásban
- **Goertzel filterek maguk zajszűrnek**, ezért CW/RTTY-nél **nincs szükség simításra**
- **SSTV/WEFAX-nál** nincs FFT-alapú detektálás, így ott az 5-pontos simítás biztonságos

**Ajánlás:** Kezdd `smoothingPoints = 0` beállítással, csak ha nagyon zajos a jel, használj 3-pontost!

## Használat

### AGC Beállítások

#### AGC Bekapcsolása (Auto mód)
```cpp
AudioProcessorC1 audioProcessor;

// AGC bekapcsolása (alapértelmezett)
audioProcessor.setAgcEnabled(true);
```

#### Manuális Gain Mód
```cpp
// AGC kikapcsolása, manuális erősítés beállítása
audioProcessor.setAgcEnabled(false);
audioProcessor.setManualGain(2.5f);  // 2.5x erősítés (0.1 - 20.0 tartomány)
```

#### AGC Állapot Lekérdezése
```cpp
bool isAuto = audioProcessor.isAgcEnabled();        // true = AGC, false = manuális
float currentGain = audioProcessor.getCurrentAgcGain(); // Aktuális erősítési faktor
float manualGain = audioProcessor.getManualGain();     // Beállított manuális gain
```

### Zajszűrés Beállítások

#### Zajszűrés Be/Ki
```cpp
// Zajszűrés bekapcsolása (alapértelmezett)
audioProcessor.setNoiseReductionEnabled(true);

// Zajszűrés kikapcsolása (csak DC offset eltávolítás)
audioProcessor.setNoiseReductionEnabled(false);
```

#### Simítás Erősségének Állítása
```cpp
// NINCS simítás (alapértelmezett - CW/RTTY-hez ajánlott)
audioProcessor.setSmoothingPoints(0);

// 3-pontos mozgó átlag (gyorsabb, enyhébb - csak ha NAGYON zajos)
audioProcessor.setSmoothingPoints(3);

// 5-pontos mozgó átlag (lassabb, erősebb - SSTV/WEFAX-hoz)
audioProcessor.setSmoothingPoints(5);
```

#### Zajszűrés Állapot Lekérdezése
```cpp
bool noiseReduction = audioProcessor.isNoiseReductionEnabled();
```

## Feldolgozási Lánc

Az új feldolgozási sorrend:

```
1. DMA → Nyers ADC minták (uint16_t)
2. removeDcAndSmooth() → DC offset eltávolítás + zajszűrés (int16_t)
3. applyAgc() → AGC vagy manuális gain alkalmazása
4. FFT előkészítés (ha kell FFT)
5. Hanning ablak
6. FFT számítás
7. Spektrum és domináns frekvencia
```

## Javasolt Beállítások Dekóderenként

### CW Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC bekapcsolva
audioProcessor.setNoiseReductionEnabled(true); // DC eltávolítás
audioProcessor.setSmoothingPoints(0);        // NINCS simítás (Goertzel maga zajszűr!)
```

**⚠️ Csak NAGYON zajos jel esetén:**
```cpp
audioProcessor.setSmoothingPoints(3);        // MAX 3-pont, 5-et SOHA NE használj!
```

### RTTY Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC bekapcsolva
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(0);        // NINCS simítás (Goertzel maga zajszűr!)
```

**⚠️ 45-50 Bd esetén, ha zajos:**
```cpp
audioProcessor.setSmoothingPoints(3);        // MAX 3-pont, 75-100 Bd-nél SOHA!
```

### SSTV Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC bekapcsolva
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(5);        // Erős simítás (nincs FFT detektálás)
```

### WEFAX Dekóder
```cpp
audioProcessor.setAgcEnabled(true);          // AGC bekapcsolva
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(5);        // Erős simítás (nincs FFT detektálás)
```

### Dominant Frequency / FFT Megjelenítés
```cpp
audioProcessor.setAgcEnabled(false);         // Manuális kontroll (valós szintek)
audioProcessor.setManualGain(1.0f);          // Nincs erősítés
audioProcessor.setNoiseReductionEnabled(true);
audioProcessor.setSmoothingPoints(3);        // Enyhe simítás az FFT-hez
```

## Finomhangolási Lehetőségek

Ha más AGC paraméterekre van szükség, az `AudioProcessor-c1.h` és `.cpp` fájlokban módosíthatók:

### AGC Paraméterek (AudioProcessor-c1.cpp konstruktor)
```cpp
agcLevel_(2000.0f),        // Kezdő AGC szint (hangolandó jelszinttől függően)
agcAlpha_(0.02f),          // Szűrési sebesség (0.01 lassú - 0.1 gyors)
agcTargetPeak_(20000.0f),  // Cél csúcs amplitúdó q15-ben
agcMinGain_(0.1f),         // Min erősítés (0.1x = 10%-ra csökkent)
agcMaxGain_(20.0f),        // Max erősítés (20x)
```

### Attack/Release Konstansok (applyAgc() metódusban)
```cpp
constexpr float ATTACK_COEFF = 0.3f;   // Gyors attack (0.1-1.0)
constexpr float RELEASE_COEFF = 0.01f; // Lassú release (0.001-0.1)
```

## Debug Kimenet

Ha `__ADPROC_DEBUG` és `__DEBUG` definiálva van, minden 100. blokkban kiírja:
```
AGC: maxAbs=15234, agcLevel=14523.2, targetGain=1.38, currentGain=1.42, mode=AUTO
```

## Technikai Részletek

### Miért NEM csökken a minták száma?

A zajszűrés **mozgó átlag FIR szűrő** - minden kimenet minta a szomszédos bemeneti minták súlyozott átlaga:
```
output[i] = (input[i-1] + input[i] + input[i+1]) / 3
```

**NEM** decimáció (pl. 4 -> 1), hanem **simítás** (N -> N).

### Fixpontos vs. Lebegőpontos

- **DC eltávolítás & Zajszűrés:** int32 számítás → int16 kimenet
- **AGC:** float számítás (precíz erősítés) → int16 alkalmazás
- **FFT:** q15 fixpontos (CMSIS-DSP optimalizált)

Ez kiegyensúlyozott teljesítmény/pontosság arányt biztosít.

## Tesztelési Javaslatok

1. **Kezdd NINCS simítással (alapértelmezett):**
   ```cpp
   setAgcEnabled(true);
   setNoiseReductionEnabled(true);
   setSmoothingPoints(0);  // NINCS simítás
   ```

2. **Gyenge jelek esetén:** Növeld az `agcMaxGain_` értéket (pl. 30.0f vagy 50.0f)

3. **Túl zajos jel esetén (CW/RTTY):** Próbáld a 3-pontos simítást
   ```cpp
   setSmoothingPoints(3);  // Max 3-pont!
   ```

4. **SSTV/WEFAX esetén:** Használd az 5-pontos simítást
   ```cpp
   setSmoothingPoints(5);  // Erős simítás OK
   ```

5. **Stabil, erős jel esetén:** Kapcsold ki az AGC-t, használj fix gain-t

6. **Spektrum megjelenítésnél:** AGC kikapcsolva, hogy a valós jelerősségeket lásd

**⚠️ FONTOS:** A CW és RTTY dekóderek Goertzel filterei **maguk végzik a zajszűrést**! 
A mozgó átlag simítás **rontja a frekvencia felbontást**, ezért csak **szükség esetén** használd!

## Teljesítmény

Az új funkciók CPU overhead-je (RP2040 @ 133MHz):
- **removeDcAndSmooth (3pt):** ~5-10 µs / 128 minta
- **removeDcAndSmooth (5pt):** ~10-15 µs / 128 minta
- **applyAgc:** ~15-20 µs / 128 minta
- **Teljes overhead:** ~30-35 µs blokkonként

Ez elhanyagolható a teljes feldolgozási időhöz képest (FFT: ~200-500 µs).

## Összegzés

Az AGC és zajszűrés jelentősen javítja a dekódolási teljesítményt gyenge vagy zajos jelekkel, miközben megőrzi a mintaszámot és frekvenciát, így a dekóderek továbbra is pontosan működnek.

**Alapértelmezett konfiguráció (ajánlott a legtöbb esetben):**
- AGC: BE
- Zajszűrés: BE (de NINCS simítás!)
- Simítás: 0-pontos (NINCS mozgó átlag)

**Indoklás:** A CW és RTTY dekóderek Goertzel filterei hatékonyabban zajszűrnek, mint egy 3-5 pontos mozgó átlag, ráadásul nem rontják a frekvencia felbontást!

Speciális igényekhez állítsd be egyedileg a paramétereket!
