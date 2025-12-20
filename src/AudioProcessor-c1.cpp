/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: AudioProcessor-c1.cpp                                                                                         *
 * Created Date: 2025.11.15.                                                                                           *
 *                                                                                                                     *
 * Author: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----                                                                                                               *
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * License: MIT License                                                                                                *
 * 	Bárki szabadon használhatja, módosíthatja, terjeszthet, beépítheti más                                             *
 * 	projektbe (akár zártkódúba is), akár pénzt is kereshet vele                                                        *
 * 	Egyetlen feltétel:                                                                                                 *
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                     *
 * -----                                                                                                               *
 * Last Modified: 2025.12.20.                                                                                          *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 * 2025.12.20   BT  Újratervezés: AGC és Goertzel eltávolítva, tiszta fixpontos CMSIS-DSP Q15 FFT pipeline             *
 */

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "AudioProcessor-c1.h"
#include "Utils.h"
#include "defines.h"

// ============================================================================
// DEBUG MAKRÓK
// ============================================================================
#define __ADPROC_DEBUG
#if defined(__DEBUG) && defined(__ADPROC_DEBUG)
#define ADPROC_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define ADPROC_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// ============================================================================
// KONSTRUKTOR / DESTRUKTOR
// ============================================================================

/**
 * @brief AudioProcessorC1 konstruktor.
 * Alapértelmezett értékekkel inicializálja az osztályt.
 */
AudioProcessorC1::AudioProcessorC1()
    : is_running(false), useFFT(false), useBlockingDma(true), currentFftSize(0), currentBinWidthHz(0.0f), currentBandwidthHz(0),
      adcMidpoint_(1u << (ADC_BIT_DEPTH - 1)), // 2048 a 12-bit ADC-hez
      useNoiseReduction_(false),               // Zajszűrés KIKAPCSOLVA alapból
      smoothingPoints_(0),                     // Nincs simítás
      spectrumAveragingCount_(1)               // Nincs átlagolás
{}

/**
 * @brief AudioProcessorC1 destruktor.
 */
AudioProcessorC1::~AudioProcessorC1() { stop(); }

// ============================================================================
// INICIALIZÁLÁS ÉS VEZÉRLÉS
// ============================================================================

/**
 * @brief Inicializálja az AudioProcessorC1 osztályt.
 * @param config ADC és DMA konfiguráció
 * @param useFFT FFT használata
 * @param useBlockingDma Blokkoló DMA mód
 * @return Mindig true (a tényleges inicializálás a start()-ban történik)
 */
bool AudioProcessorC1::initialize(const AdcDmaC1::CONFIG &config, bool useFFT, bool useBlockingDma) {
    this->adcConfig = config;
    this->useFFT = useFFT;
    this->useBlockingDma = useBlockingDma;

    ADPROC_DEBUG("AudioProc-c1: Inicializálva - sampleCount=%d, samplingRate=%d, useFFT=%d, blocking=%d\n", config.sampleCount, config.samplingRate, useFFT,
                 useBlockingDma);

    return true;
}

/**
 * @brief Elindítja az audio feldolgozást.
 */
void AudioProcessorC1::start() {
    if (is_running) {
        return;
    }

    adcDmaC1.initialize(adcConfig);
    is_running = true;

    ADPROC_DEBUG("AudioProc-c1: ELINDÍTVA - sampleCount=%d, samplingRate=%d Hz, useFFT=%d\n", adcConfig.sampleCount, adcConfig.samplingRate, useFFT);
}

/**
 * @brief Leállítja az audio feldolgozást.
 */
void AudioProcessorC1::stop() {
    if (!is_running) {
        return;
    }

    adcDmaC1.finalize();
    is_running = false;

    ADPROC_DEBUG("AudioProc-c1: LEÁLLÍTVA\n");
}

/**
 * @brief Átméretezi a mintavételezési konfigurációt.
 * @param sampleCount Mintaszám blokkonként
 * @param samplingRate Mintavételezési sebesség (Hz)
 * @param bandwidthHz Audio sávszélesség (Hz)
 */
void AudioProcessorC1::reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz) {
    // Leállítjuk a feldolgozást az újrakonfiguráláshoz
    stop();

    ADPROC_DEBUG("AudioProc-c1: Újrakonfigurálás - sampleCount=%d, samplingRate=%d Hz, bandwidthHz=%u Hz\n", sampleCount, samplingRate, bandwidthHz);

    // Nyquist-frekvencia alapú mintavételezési sebesség számítás
    // Ha a megadott samplingRate kisebb mint a Nyquist, akkor növeljük
    uint32_t finalRate = samplingRate;
    if (bandwidthHz > 0) {
        // Nyquist: minimum 2x a legnagyobb frekvencia
        uint32_t nyquist = bandwidthHz * 2u;
        // 25% túlmintavételezés a biztonság kedvéért
        uint32_t suggested = (nyquist * 5u) / 4u; // 1.25x fixpontosan

        if (finalRate == 0 || finalRate < nyquist) {
            finalRate = suggested;
        }
    }

    // Határértékek ellenőrzése
    if (finalRate == 0) {
        finalRate = 44100u; // Alapértelmezett
    }
    if (finalRate > 65535u) {
        finalRate = 65535u; // uint16_t maximum
    }

    // Konfiguráció frissítése
    adcConfig.sampleCount = sampleCount;
    adcConfig.samplingRate = static_cast<uint16_t>(finalRate);

    // Bandwidth tárolása (bin-kizáráshoz)
    currentBandwidthHz = bandwidthHz;

    // FFT inicializálása ha szükséges
    if (useFFT && sampleCount > 0) {
        currentFftSize = sampleCount;

        // Bin szélesség számítása (Hz) - float, mert a megjelenítéshez kell
        currentBinWidthHz = (float)finalRate / (float)sampleCount;

        // CMSIS-DSP Q15 FFT inicializálás
        initFixedPointFFT(sampleCount);

        ADPROC_DEBUG("AudioProc-c1: FFT inicializálva - bins=%d, binWidth=%.2f Hz\n", sampleCount / 2, currentBinWidthHz);
    }

    // Feldolgozás újraindítása
    start();
}

// ============================================================================
// ADC KALIBRÁCIÓ
// ============================================================================

/**
 * @brief Kalibrálja az ADC DC középpontját.
 *
 * Ez a függvény megméri az ADC "nyugalmi" értékét (DC offset), ami
 * ideális esetben 2048 lenne 12-bit ADC esetén, de a valóságban
 * eltérhet ettől a hardver miatt.
 *
 * @param sampleCount A kalibráció mintaszáma
 */
void AudioProcessorC1::calibrateDcMidpoint(uint32_t sampleCount) {
    if (sampleCount == 0) {
        sampleCount = ADC_MIDPOINT_MEASURE_SAMPLE_COUNT;
    }

    // Ha fut a DMA, ideiglenesen leállítjuk
    bool wasRunning = is_running;
    if (wasRunning) {
        stop();
    }

    // ADC csatorna beállítása az audio bemenetre
    adc_select_input(0); // ADC0 / GPIO26

    // Minták összegyűjtése és átlagolása
    uint64_t sum = 0;
    for (uint32_t i = 0; i < sampleCount; ++i) {
        sleep_us(50); // Várakozás a stabil ADC olvasáshoz
        sum += analogRead(PIN_AUDIO_INPUT);
    }

    // Átlag számítása
    adcMidpoint_ = static_cast<uint32_t>(sum / sampleCount);

    // Ha előtte futott, újraindítjuk
    if (wasRunning) {
        start();
    }

    ADPROC_DEBUG("AudioProc-c1: DC midpoint kalibrálva = %u (mérések: %u)\n", adcMidpoint_, sampleCount);
}

// ============================================================================
// FŐ FELDOLGOZÁSI FÜGGVÉNY
// ============================================================================

/**
 * @brief Feldolgozza a legfrissebb audio blokkot.
 *
 * Feldolgozási lánc:
 * 1. DMA puffer lekérése (blokkoló vagy nem-blokkoló módban)
 * 2. DC offset eltávolítása (uint16_t -> int16_t)
 * 3. Ha useFFT=true: Q15 FFT feldolgozás
 *
 * @param sharedData Kimeneti struktúra
 * @return true ha sikeres, false ha nincs adat
 */
bool AudioProcessorC1::processAndFillSharedData(SharedData &sharedData) {
    // Ellenőrzés: fut-e a feldolgozás
    if (!is_running) {
        return false;
    }

    // DMA puffer lekérése
    // - Blokkoló mód (SSTV/WEFAX): megvárja a teljes blokkot
    // - Nem-blokkoló mód (CW/RTTY): nullptr-t ad vissza ha nincs kész adat
    uint16_t *dmaBuffer = adcDmaC1.getCompletePingPongBufferPtr(useBlockingDma);

    if (dmaBuffer == nullptr) {
        // Nincs kész adat (csak nem-blokkoló módban)
        return false;
    }

    // --- 1. LÉPÉS: DC offset eltávolítása ---
    // A nyers ADC minták (0-4095) átalakítása előjeles értékekké (-2048..+2047)
    sharedData.rawSampleCount = std::min((uint16_t)adcConfig.sampleCount, (uint16_t)MAX_RAW_SAMPLES_SIZE);
    removeDcOffset(dmaBuffer, sharedData.rawSampleData, sharedData.rawSampleCount);

    // --- 2. LÉPÉS: FFT feldolgozás (ha szükséges) ---
    if (!useFFT) {
        // Nincs FFT - csak a nyers minták kellenek (SSTV, WEFAX)
        sharedData.fftSpectrumSize = 0;
        sharedData.dominantFrequency = 0;
        sharedData.dominantAmplitude = 0;
        sharedData.fftBinWidthHz = 0.0f;
        return true;
    }

    // Q15 FFT feldolgozás
    return processFixedPointFFT(sharedData);
}

// ============================================================================
// DC OFFSET ELTÁVOLÍTÁS
// ============================================================================

/**
 * @brief DC offset eltávolítása a nyers ADC mintákból.
 *
 * A 12-bit ADC értékek (0-4095) átalakítása előjeles int16_t értékekké,
 * a mért DC középpont levonásával. Ez biztosítja, hogy a jel 0 körül
 * ingadozzon, ami az FFT-hez szükséges.
 *
 * FONTOS: int16_t és q15_t ugyanaz a formátum, ezért a kimenet
 * közvetlenül használható az FFT-hez!
 *
 * @param input Bemeneti ADC minták (12-bit, 0-4095)
 * @param output Kimeneti DC-mentes minták (-2048..+2047)
 * @param count Minták száma
 */
void AudioProcessorC1::removeDcOffset(const uint16_t *input, int16_t *output, uint16_t count) {
    // Egyszerű, gyors DC offset eltávolítás
    // A mért adcMidpoint_ értéket vonjuk le minden mintából
    const int32_t midpoint = static_cast<int32_t>(adcMidpoint_);

    for (uint16_t i = 0; i < count; ++i) {
        // uint16_t - midpoint -> int32_t -> int16_t
        // A 12-bit ADC értékek beleférnek int16_t-be DC offset után
        output[i] = static_cast<int16_t>(static_cast<int32_t>(input[i]) - midpoint);
    }
}

// ============================================================================
// SPEKTRUM ÁTLAGOLÁS (jelenleg kikapcsolva)
// ============================================================================

/**
 * @brief Beállítja a spektrum átlagolás keretszámát.
 * @param n Keretek száma (1 = nincs átlagolás)
 */
void AudioProcessorC1::setSpectrumAveragingCount(uint8_t n) {
    spectrumAveragingCount_ = (n == 0) ? 1 : n;
    ADPROC_DEBUG("AudioProc-c1: Spektrum átlagolás = %d keret\n", spectrumAveragingCount_);
}

/**
 * @brief Lekéri a spektrum átlagolás keretszámát.
 */
uint8_t AudioProcessorC1::getSpectrumAveragingCount() const { return spectrumAveragingCount_; }

// ============================================================================
// CMSIS-DSP Q15 FFT IMPLEMENTÁCIÓ
// ============================================================================

/**
 * @brief CMSIS-DSP Q15 FFT inicializálása.
 *
 * Előkészíti az FFT példányt és a szükséges puffereket.
 * A Hanning ablakot is létrehozza Q15 formátumban.
 *
 * @param sampleCount FFT méret (2 hatványa: 64, 128, 256, 512, 1024)
 */
void AudioProcessorC1::initFixedPointFFT(uint16_t sampleCount) {
    // CMSIS-DSP FFT példány inicializálása
    arm_status status = arm_cfft_init_q15(&fft_inst_q15, sampleCount);
    if (status != ARM_MATH_SUCCESS) {
        ADPROC_DEBUG("HIBA: CMSIS-DSP FFT inicializálás sikertelen! status=%d\n", status);
        return;
    }

    // Pufferek méretezése
    // FFT bemenet: komplex formátum [re0, im0, re1, im1, ...]
    fftInput_q15.resize(sampleCount * 2);

    // Magnitude kimenet: N darab érték (de csak N/2-t használunk)
    magnitude_q15.resize(sampleCount);

    // Hanning ablak létrehozása
    buildHanningWindow_q15(sampleCount);

    ADPROC_DEBUG("AudioProc-c1: CMSIS-DSP Q15 FFT inicializálva - N=%d\n", sampleCount);
}

/**
 * @brief Hanning ablak létrehozása Q15 formátumban.
 *
 * A Hanning ablak csökkenti a spektrális szivárgást (spectral leakage).
 * Képlet: w[n] = 0.5 * (1 - cos(2*pi*n / (N-1)))
 *
 * A számítás float-ban történik, de csak egyszer, inicializáláskor.
 *
 * @param size Ablak mérete
 */
void AudioProcessorC1::buildHanningWindow_q15(uint16_t size) {
    hanningWindow_q15.resize(size);

    for (uint16_t i = 0; i < size; ++i) {
        // Hanning ablak számítása
        float angle = 2.0f * (float)M_PI * (float)i / (float)(size - 1);
        float windowVal = 0.5f * (1.0f - cosf(angle));

        // Konverzió Q15 formátumba
        hanningWindow_q15[i] = floatToQ15(windowVal);
    }

    ADPROC_DEBUG("AudioProc-c1: Hanning ablak létrehozva Q15 formátumban - size=%d\n", size);
}

/**
 * @brief Q15 FFT feldolgozás végrehajtása.
 *
 * A teljes FFT feldolgozási lánc:
 * 1. Bemeneti adatok előkészítése (komplex formátum)
 * 2. Hanning ablak alkalmazása (Q15 szorzás)
 * 3. CMSIS-DSP FFT futtatása
 * 4. Magnitude számítás
 * 5. Domináns frekvencia keresése
 *
 * FONTOS MEGJEGYZÉSEK A CMSIS-DSP Q15 FFT-RŐL:
 * - Az arm_cfft_q15 automatikusan skáláz minden butterfly szakaszban (log2(N) bit)
 * - Az arm_cmplx_mag_q15 kimenete Q2.14 formátumú (17 bites jobbra tolás a négyzetösszegből)
 * - A magnitude értékek így N-től FÜGGETLENEK lesznek (ez a kívánt viselkedés!)
 * - NEM szabad visszaskálázni az FFT kimenetet, mert az SATURÁCIÓHOZ vezet!
 *
 * @param sharedData Kimeneti struktúra
 * @return true ha sikeres
 */
bool AudioProcessorC1::processFixedPointFFT(SharedData &sharedData) {
    const uint16_t N = adcConfig.sampleCount;

    // Biztonsági ellenőrzés
    if (fftInput_q15.size() < N * 2) {
        ADPROC_DEBUG("HIBA: FFT puffer túl kicsi!\n");
        return false;
    }

    // --- 1. LÉPÉS: Komplex formátumba rendezés ÉS Q15 SKÁLÁZÁS ---
    // A valós bemeneti adatokat komplex formátumba tesszük: [re, im, re, im, ...]
    // Az imaginárius részek nullák
    // FONTOS: A rawSampleData ~11-bites előjeles (-2048..+2047),
    // de a Q15 formátum 15-bites (-32768..+32767)!
    // Ezért 4 bittel balra toljuk (x16) a dinamikatartomány jobb kihasználásához!
    constexpr int inputScaleShift = 4; // 11 bit -> 15 bit

    // DEBUG: input maximum keresése
    q15_t inputMax = 0;
    for (uint16_t i = 0; i < N; ++i) {
        q15_t scaled = static_cast<q15_t>(sharedData.rawSampleData[i] << inputScaleShift);
        fftInput_q15[2 * i] = scaled; // Valós rész (felskálázva)
        fftInput_q15[2 * i + 1] = 0;  // Imaginárius rész = 0
        if (abs(scaled) > inputMax)
            inputMax = abs(scaled);
    }

    // --- 2. LÉPÉS: Hanning ablak alkalmazása ---
    // Q15 * Q15 szorzás: az eredmény Q30, amit 15 bittel jobbra tolunk -> Q15
    for (uint16_t i = 0; i < N; ++i) {
        q31_t windowed = ((q31_t)fftInput_q15[2 * i] * (q31_t)hanningWindow_q15[i]) >> 15;
        fftInput_q15[2 * i] = static_cast<q15_t>(__SSAT(windowed, 16)); // Saturáció
    }

    // --- 3. LÉPÉS: CMSIS-DSP FFT futtatása ---
    // Paraméterek: 0 = forward FFT, 1 = bit-reversal engedélyezve
    // FONTOS: Az FFT AUTOMATIKUSAN SKÁLÁZ log2(N) bittel a túlcsordulás elkerülésére!
    arm_cfft_q15(&fft_inst_q15, fftInput_q15.data(), 0, 1);

    // DEBUG: FFT kimenet ellenőrzése
    q15_t fftMaxRe = 0, fftMaxIm = 0;
    for (uint16_t i = 0; i < N; ++i) {
        if (abs(fftInput_q15[2 * i]) > fftMaxRe)
            fftMaxRe = abs(fftInput_q15[2 * i]);
        if (abs(fftInput_q15[2 * i + 1]) > fftMaxIm)
            fftMaxIm = abs(fftInput_q15[2 * i + 1]);
    }

    // --- 4. LÉPÉS: Magnitude számítás ---
    // arm_cmplx_mag_q15: sqrt(re^2 + im^2) minden komplex számra
    // FONTOS: A kimenet Q2.14 formátumú! (lásd CMSIS-DSP dokumentáció)
    // A harmadik paraméter a KOMPLEX számok száma!
    arm_cmplx_mag_q15(fftInput_q15.data(), magnitude_q15.data(), N);

    // DEBUG: Magnitude ellenőrzése a skálázás előtt
    q15_t magMaxBefore = 0;
    for (uint16_t i = 0; i < N; ++i) {
        if (magnitude_q15[i] > magMaxBefore)
            magMaxBefore = magnitude_q15[i];
    }

    // --- 5. LÉPÉS: NINCS VISSZASKÁLÁZÁS! ---
    // A CMSIS-DSP Q15 FFT auto-scaling már N-független kimenetet ad.
    // Az arm_cmplx_mag_q15 kimenete is konzisztens minden N-re.
    // A magnitude értékeket KÖZVETLENÜL használjuk, skálázás nélkül!
    //
    // Korábbi hiba: visszaskáláztunk log2(N) bittel, de ez SATURÁCIÓHOZ vezetett!
    // (pl. 1514 << 8 = 387584 > 32767 -> saturált 32767-re)

    // Debug kimenet minden feldolgozásnál (ideiglenesen)
    static uint32_t debugCounter = 0;
    if (++debugCounter >= 50) {
        debugCounter = 0;
        ADPROC_DEBUG("FFT DEBUG: inputMax=%d, fftMaxRe=%d, fftMaxIm=%d, mag=%d, N=%d\n", inputMax, fftMaxRe, fftMaxIm, magMaxBefore, N);
    }

    // --- 6. LÉPÉS: Eredmények másolása a SharedData-ba ---
    uint16_t spectrumSize = N / 2; // Csak a pozitív frekvenciák (Nyquist)
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)MAX_FFT_SPECTRUM_SIZE);

    // Spektrum adatok másolása (skálázás nélkül)
    memcpy(sharedData.fftSpectrumData, magnitude_q15.data(), sharedData.fftSpectrumSize * sizeof(q15_t));

    // DC bin (bin[0]) nullázása - ez csak DC offset, nem hasznos információ
    if (sharedData.fftSpectrumSize > 0) {
        sharedData.fftSpectrumData[0] = 0;
    }

    // Bin szélesség (Hz)
    sharedData.fftBinWidthHz = currentBinWidthHz;

    // --- 7. LÉPÉS: Domináns frekvencia keresése ---
    // A legnagyobb amplitúdójú bin megkeresése (DC bin kihagyásával)
    uint16_t maxIndex = 1;
    q15_t maxValue = sharedData.fftSpectrumData[1];

    for (uint16_t i = 2; i < sharedData.fftSpectrumSize; ++i) {
        if (sharedData.fftSpectrumData[i] > maxValue) {
            maxValue = sharedData.fftSpectrumData[i];
            maxIndex = i;
        }
    }

    // Domináns frekvencia számítása (Hz)
    // f = bin_index * (Fs / N) = bin_index * binWidth
    sharedData.dominantFrequency = static_cast<uint32_t>(maxIndex * currentBinWidthHz);
    sharedData.dominantAmplitude = maxValue;

#ifdef __ADPROC_DEBUG
    // Debug kimenet ritkítva (5 másodpercenként)
    static uint32_t lastDebugTime = 0;
    if (Utils::timeHasPassed(lastDebugTime, 5000)) {
        lastDebugTime = millis();

        // Vpp számítás a domináns frekvenciához
        //
        // A magnitude értékek közvetlenül az arm_cmplx_mag_q15 kimenetéből jönnek.
        // Az input 4 bittel fel lett skálázva (inputScaleShift = 4).
        //
        // Empirikus kalibráció: 300 mVpp bemenetnél mag ≈ 1514
        // Képlet: Vpp = mag * kalibrációs_szorzó * ADC_LSB_VOLTAGE_MV

        // Vissza kell skálázni az input skálázással (4 bit = 16x)
        float magnitudeAdc = (float)maxValue / 16.0f;

        // Hanning ablak és FFT kompenzáció (empirikusan kalibrálva: 2.0)
        // Korábbi érték 4.0 volt, de az dupla Vpp-t adott
        float peakAdc = magnitudeAdc * 2.0f;

        // ADC egységből mV-ba (1 LSB = 3300mV / 4096 ≈ 0.8057 mV)
        float peakMv = peakAdc * ADC_LSB_VOLTAGE_MV;

        // Peak-to-peak (Vpp = 2 * Vpeak)
        float vppMv = peakMv * 2.0f;

        ADPROC_DEBUG("AudioProc-c1: FFT kész - domFreq=%u Hz, amp=%d (%.1f mVpp), bins=%d, N=%d\n", sharedData.dominantFrequency, maxValue, vppMv,
                     sharedData.fftSpectrumSize, N);
    }
#endif

    return true;
}
