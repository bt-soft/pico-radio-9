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
 * Last Modified: 2025.11.22, Saturday  10:53:55                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <hardware/uart.h>
#include <memory>

#include "AudioProcessor-c1.h"
#include "Config.h" // A globális config objektumhoz
#include "Utils.h"
#include "defines.h"

// AudioProcessor működés debug engedélyezése de csak DEBUG módban
// #define __ADPROC_DEBUG
#if defined(__DEBUG) && defined(__ADPROC_DEBUG)
#define ADPROC_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define ADPROC_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

/**
 * @brief AudioProcessorC1 konstruktor.
 */
AudioProcessorC1::AudioProcessorC1()
    : useFFT(false),        // FFT használat kikapcsolva alapértelmezetten
      is_running(false),    // Feldolgozás leállítva alapértelmezetten
      useBlockingDma(true), // Blokkoló DMA mód alapértelmezetten

      //-------
      // AGC alapértelmezett értékek inicializálása
      agcLevel_(1000.0f),      //
      agcAlpha_(0.02f),        //
      agcTargetPeak_(8000.0f), //
      agcMinGain_(0.1f),       //
      agcMaxGain_(100.0f),     // 20.0
      currentAgcGain_(1.0f),   //

      //------------- állítható értékek
      useAgc_(false),    // Alapértelmezetten az AGC ki van kapcsolva
      manualGain_(1.0f), // manual gain alapértelmezett érték (ha az AGC ki van kapcsolva)

      // Zajszűrés alapértelmezett értékek
      useNoiseReduction_(false), // Alapértelmezetten zajszűrés bekapcsolva
      smoothingPoints_(0)        // Alapértelmezett simítás értéke
{}

/**
 * @brief AudioProcessorC1 destruktor.
 */
AudioProcessorC1::~AudioProcessorC1() { stop(); }

/**
 * @brief Inicializálja az AudioProcessorC1 osztályt a megadott konfigurációval.
 * @param config Az ADC és DMA konfigurációs beállításai.
 * @param useFFT Jelzi, hogy FFT-t használunk-e a feldolgozáshoz.
 * @param useBlockingDma Blokkoló (true, SSTV/WEFAX) vagy nem-blokkoló (false, CW/RTTY) DMA mód.
 * @return Sikeres inicializálás esetén true, egyébként false.
 */
bool AudioProcessorC1::initialize(const AdcDmaC1::CONFIG &config, bool useFFT, bool useBlockingDma) {
    this->useFFT = useFFT;
    this->adcConfig = config;
    this->useBlockingDma = useBlockingDma;

    // Default AGC és erősítés beállítások
    this->setAgcEnabled(false); // AGC kikapcsolása
    this->setManualGain(1.0f);  // Manuális erősítés (1.0 = nincs extra erősítés)

    return true;
}

/**
 * @brief Elindítja az audio feldolgozást.
 */
void AudioProcessorC1::start() {
    adcDmaC1.initialize(this->adcConfig);
    is_running = true;
    ADPROC_DEBUG("core1: AudioProc-c1 start: elindítva, sampleCount=%d, samplingRate=%d Hz, useFFT=%d, is_running=%d\n", adcConfig.sampleCount,
                 adcConfig.samplingRate, useFFT, is_running);
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
    ADPROC_DEBUG("core1: AudioProc-c1 stop: leállítva, is_running=%d\n", is_running);
}

/**
 * @brief Átméretezi a mintavételezési konfigurációt.
 * @param sampleCount Az új mintaszám blokkonként.
 * @param samplingRate Az új mintavételezési sebesség Hz-ben.
 * @param bandwidthHz A sávszélesség Hz-ben (opcionális).
 */
void AudioProcessorC1::reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz) {

    this->stop();

    ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() HÍVÁS - sampleCount=%d, samplingRate=%d Hz, bandwidthHz=%d Hz\n", sampleCount, samplingRate,
                 bandwidthHz);

    const float oversampleFactor = 1.25f;
    uint32_t finalRate = samplingRate;
    if (bandwidthHz > 0) {
        uint32_t nyquist = bandwidthHz * 2u;
        uint32_t suggested = static_cast<uint32_t>(ceilf(nyquist * oversampleFactor));
        ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() - nyquist=%d Hz, suggested=%d Hz, finalRate(előtte)=%d Hz\n", nyquist, suggested, finalRate);

        if (finalRate == 0) {
            finalRate = suggested;
        } else if (finalRate < nyquist) {
            finalRate = suggested;
        }
        ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() - finalRate(utána)=%d Hz\n", finalRate);
    }

    // A mintavételezési frekvencia (finalRate) mindig egy érvényes, biztonságos tartományban legyen
    if (finalRate == 0) {
        finalRate = 44100u;
    }
    if (finalRate > 65535u) {
        finalRate = 65535u;
    }

    // Ha FFT-t használunk, előkészítjük a szükséges erőforrásokat (Arduino FFT - FLOAT)
    if (useFFT) {
        ADPROC_DEBUG("core1: Arduino FFT init, sampleCount=%d\n", sampleCount);

        // FFT tömbök átméretezése (FLOAT)
        this->currentFftSize = sampleCount;
        this->vReal.resize(sampleCount);
        this->vImag.resize(sampleCount);
        this->fftMagnitude.resize(sampleCount);

        // Arduino FFT objektum létrehozása
        FFT = ArduinoFFT<float>(vReal.data(), vImag.data(), sampleCount, finalRate);

        ADPROC_DEBUG("core1: Arduino FFT init OK, useFFT=%d\n", useFFT);

        // Egy bin szélessége Hz-ben
        float binWidth = (sampleCount > 0) ? ((float)finalRate / (float)sampleCount) : 0.0f;

#ifdef __ADPROC_DEBUG
        // Kiírjuk a FFT-hez kapcsolódó paramétereket
        uint32_t afBandwidth = bandwidthHz; // hangfrekvenciás sávszélesség
        uint16_t bins = (sampleCount / 2);  // bin-ek száma
        ADPROC_DEBUG("AudioProc-c1 Arduino FFT paraméterek: afBandwidth=%u Hz, finalRate=%u Hz, sampleCount=%u, bins=%u, binWidth=%.2f Hz\n", //
                     afBandwidth, finalRate, (unsigned)sampleCount, (unsigned)bins, binWidth);
#endif

        // Eltároljuk a kiszámított bin szélességet az példányban, hogy a feldolgozás a SharedData-ba írhassa
        this->currentBinWidthHz = binWidth;
    }

    // Átlagoló puffer inicializálása ha szükséges (az előző tartalom törlése)
    if (useFFT) {
        uint16_t spectrumSize = (sampleCount / 2);
        avgBuffer.clear();
        avgBuffer.resize(spectrumSize * spectrumAveragingCount_, 0.0f);
        avgWriteIndex_ = 0;
    }

    adcConfig.sampleCount = sampleCount;
    adcConfig.samplingRate = static_cast<uint16_t>(finalRate);

    ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() - adcConfig frissítve: sampleCount=%d, samplingRate=%d Hz\n", adcConfig.sampleCount,
                 adcConfig.samplingRate);
    this->start();
}

// /**
//  * @brief Ellenőrzi, hogy a bemeneti jel meghaladja-e a küszöbértéket.
//  * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
//  * @return true, ha a jel meghaladja a küszöböt, különben false.
//  */
// //[[maybe_unused]] // sehol sem használjuk
// bool AudioProcessorC1::checkSignalThreshold(int16_t *samples, uint16_t count) {

//     // Kikeressük a max abszolút értéket a bemeneti minták között
//     int16_t maxAbsRaw = std::abs(*std::max_element( //
//         samples, samples + count,                   //
//         [](int16_t a, int16_t b) {                  //
//             return std::abs(a) < std::abs(b);       //
//         }));

//     // Küszöb: ha a bemenet túl kicsi, akkor ne vegyen fel hamis spektrum-csúcsot.
//     constexpr int16_t RAW_SIGNAL_THRESHOLD = 80; // nyers ADC egységben, hangolandó
//     if (maxAbsRaw < RAW_SIGNAL_THRESHOLD) {
// #ifdef __ADPROC_DEBUG
//         // Ha túl kicsi a jel akkor azt jelezzük
//         static uint8_t thresholdDebugCounter = 0;
//         if (++thresholdDebugCounter >= 100) {
//             ADPROC_DEBUG("AudioProc-c1: nincs audió jel (maxAbsRaw=%d) -- FFT kihagyva\n", (int)maxAbsRaw);
//             thresholdDebugCounter = 0;
//         }
// #endif
//         return false;
//     }

//     // Jel meghaladja a küszöböt
//     return true;
// }

/**
 * @brief DC komponens eltávolítása és zajszűrés mozgó átlaggal
 * @param input Bemeneti nyers ADC minták (uint16_t)
 * @param output Kimeneti DC-mentes minták zajszűréssel (int16_t)
 * @param count Minták száma
 *
 * Ez a metódus három dolgot végez:
 * 1. DC offset eltávolítása (ADC_MIDPOINT levonása)
 * 2. Opcionális mozgó átlagos simítás (0=nincs, 3 vagy 5 pont)
 * 3. uint16_t -> int16_t konverzió
 *
 * FONTOS: A mozgó átlag NEM csökkenti a minták számát, csak simítja őket!
 *
 * Dekóder-specifikus javaslatok:
 * - CW/RTTY: smoothingPoints_ = 0 vagy 3 (az FFT-n alapuló detektálás esetén nincs szükség simításra!)
 * - SSTV/WEFAX: smoothingPoints_ = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
 * - FFT megjelenítés: smoothingPoints_ = 3 (enyhe simítás)
 */
void AudioProcessorC1::removeDcAndSmooth(const uint16_t *input, int16_t *output, uint16_t count) {

    // A DC offset kiszámításához használt konstans shift.
    // Jelenleg 0, így nincs shiftes erősítés.
    constexpr uint8_t SHIFT = 0;

    // ------------------------------------------------------
    // 1) NINCS zajszűrés (noise reduction kikapcsolva)
    // ------------------------------------------------------
    if (!useNoiseReduction_ || smoothingPoints_ == 0) {
        // --------------------------------------------------
        // 1/A) Csak DC eltávolítás (SHIFT == 0)
        // --------------------------------------------------
        if constexpr (SHIFT == 0) {
            // A std::transform végigmegy az input tömbön,
            // és minden mintából kivonja az ADC középpont értékét.
            std::transform(input, input + count, output, [](uint16_t v) {
                // DC offset eltávolítás
                return int16_t(v - ADC_MIDPOINT);
            });
        } else {
            // --------------------------------------------------
            // 1/B) DC eltávolítás + shift + clamp-elés
            // SHIFT > 0 esetén történhet túlfutás, ezért kell clamp
            // --------------------------------------------------
            std::transform(input, input + count, output, [](uint16_t v) {
                // Minta DC korrekcióval
                int32_t val = ((int32_t)v - ADC_MIDPOINT) << SHIFT;

                // Clamp int16_t tartományra
                val = std::clamp(val, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
                return (int16_t)val;
            });
        }

        // Ezen az ágon nincs további feldolgozás
        return;
    }

    // ------------------------------------------------------
    // 2) Zajszűrés bekapcsolva – mozgó átlag simítással
    // ------------------------------------------------------

    // ------------------------------------------------------
    // 2/a) 5-pontos mozgó átlag (erősebb simítás)
    // ------------------------------------------------------
    if (smoothingPoints_ == 5) {
        for (uint16_t i = 0; i < count; i++) {
            // A sum kezdetben az aktuális minta DC-offset korrigált értéke
            int32_t sum = (int32_t)input[i] - ADC_MIDPOINT;
            uint8_t div = 1;

            // Előző 2 minta
            if (i >= 2) {
                sum += (int32_t)input[i - 2] - ADC_MIDPOINT;
                div++;
            }
            if (i >= 1) {
                sum += (int32_t)input[i - 1] - ADC_MIDPOINT;
                div++;
            }

            // Következő 2 minta
            if (i + 1 < count) {
                sum += (int32_t)input[i + 1] - ADC_MIDPOINT;
                div++;
            }
            if (i + 2 < count) {
                sum += (int32_t)input[i + 2] - ADC_MIDPOINT;
                div++;
            }

            // Átlagolás
            output[i] = sum / div;
        }
    } else {
        // ------------------------------------------------------
        // 2/b) 3-pontos mozgó átlag (gyorsabb, enyhébb simítás)
        // ------------------------------------------------------
        for (uint16_t i = 0; i < count; i++) {
            int32_t sum = (int32_t)input[i] - ADC_MIDPOINT;
            uint8_t div = 1;

            // Előző minta
            if (i >= 1) {
                sum += (int32_t)input[i - 1] - ADC_MIDPOINT;
                div++;
            }

            // Következő minta
            if (i + 1 < count) {
                sum += (int32_t)input[i + 1] - ADC_MIDPOINT;
                div++;
            }

            output[i] = sum / div;
        }
    }
}

/**
 * @brief Automatikus Erősítésszabályozás (AGC) vagy manuális gain alkalmazása
 * @param samples Bemeneti/kimeneti minták (in-place feldolgozás)
 * @param count Minták száma
 *
 * Két üzemmód:
 * 1. AGC mód (useAgc_ = true): Automatikus erősítés attack/release karakterisztikával
 * 2. Manuális mód (useAgc_ = false): Fix erősítési tényező alkalmazása
 *
 * Az AGC algoritmus:
 * - Exponenciális mozgó átlag a jel szintjére
 * - Gyors attack (0.3) / lassú release (0.01) válasz
 * - Cél amplitúdó: ~60% a q15 tartományból (20000 / 32768)
 * - Gain tartomány: 0.1x - 20x
 */
void AudioProcessorC1::applyAgc(int16_t *samples, uint16_t count) {

    if (!useAgc_ && manualGain_ == 1.0f) {
        // Sem AGC, sem manuális erősítés - nem megyünk tovább
        return;
    }

    // Manuális erősítés mód?
    if (!useAgc_) {
        if (manualGain_ != 1.0f) {
            for (uint16_t i = 0; i < count; i++) {
                int32_t val = (int32_t)(samples[i] * manualGain_);
                samples[i] = (int16_t)constrain(val, -32768, 32767);
            }
        }

        // Debug kimenet ritkítva (minden 100. blokkban)
#ifdef __DEBUG
        static uint32_t agcDebugCounter = 0;
        if (++agcDebugCounter >= 100) {
            ADPROC_DEBUG("AudioProcessorC1::applyAgc: MANUAL AGC mode, manualGain=%.2f\n", manualGain_);
            agcDebugCounter = 0;
        }
#endif
        return;
    }

    // AGC mód
    // 1. Maximum keresése a blokkban
    int32_t maxAbs = std::abs(*std::max_element( //
        samples, samples + count,                //
        [](int32_t a, int32_t b) {               //
            return std::abs(a) < std::abs(b);
        }));

    // 2. AGC szint frissítése (exponenciális mozgó átlag)
    agcLevel_ += agcAlpha_ * (maxAbs - agcLevel_);

    // 3. Cél erősítés számítása
    float targetGain = 1.0f;
    if (agcLevel_ > 10.0f) { // Nullával osztás és túl kis jelek elkerülése
        targetGain = agcTargetPeak_ / agcLevel_;
        targetGain = constrain(targetGain, agcMinGain_, agcMaxGain_);
    }

    // 4. Simított erősítés számítása (attack/release karakterisztika)
    constexpr float ATTACK_COEFF = 0.3f;   // Gyors attack (jel erősödik)
    constexpr float RELEASE_COEFF = 0.01f; // Lassú release (jel csillapodik)

    if (targetGain < currentAgcGain_) {
        // Jel erősödött -> gyorsabb attack (erősítés csökkentése)
        currentAgcGain_ += ATTACK_COEFF * (targetGain - currentAgcGain_);
    } else {
        // Jel gyengült -> lassabb release (erősítés növelése)
        currentAgcGain_ += RELEASE_COEFF * (targetGain - currentAgcGain_);
    }
    // Biztonsági határ ellenőrzés
    currentAgcGain_ = constrain(currentAgcGain_, agcMinGain_, agcMaxGain_);

    // 5. Erősítés alkalmazása a mintákra
    for (uint16_t i = 0; i < count; i++) {
        int32_t val = (int32_t)(samples[i] * currentAgcGain_);
        samples[i] = (int16_t)constrain(val, -32768, 32767);
    }

#ifdef __ADPROC_DEBUG
    // Debug kimenet ritkítva (minden 100. blokkban)
    static uint32_t agcDebugCounter = 0;
    if (++agcDebugCounter >= 100) {
        ADPROC_DEBUG("AudioProcessorC1::applyAgc: AUTO AGC mode, maxAbs=%d, agcLevel=%.1f, targetGain=%.2f, currentGain=%.2f\n", (int)maxAbs, agcLevel_,
                     targetGain, currentAgcGain_);
        agcDebugCounter = 0;
    }
#endif
}

/**
 * @brief Egy frekvencia-tartománybeli erősítést alkalmaz az FFT adatokra "lapított" Gauss-ablak formájában.
 * @param data FFT bemeneti/kimeneti adatok (FLOAT)
 * @param size Az adatok mérete (bin-ek száma)
 * @param fftBinWidthHz Egy bin szélessége Hz-ben
 * @param boostMinHz Az erősítési tartomány alsó határa Hz-ben
 * @param boostMaxHz Az erősítési tartomány felső határa Hz-ben
 * @param boostGain Az erősítési tényező (pl. 10.0 = 10x erősítés)
 *
 */
void AudioProcessorC1::applyFftGaussianWindow(float *data, uint16_t size, float fftBinWidthHz, float boostMinHz, float boostMaxHz, float boostGain) {

    // Lapított Gauss-szerű erősítés: a tartományon belül a maximumhoz közel Gauss, széleken lapított
    float centerFreq = (boostMinHz + boostMaxHz) / 2.0f;
    float sigma = (boostMaxHz - boostMinHz) * 1.2f; // még laposabb, szélesebb görbe
    float minGain = 1.0f;

    for (uint16_t i = 0; i < size; i++) {
        float freq = i * fftBinWidthHz;
        float gain = minGain;

        if (freq >= boostMinHz && freq <= boostMaxHz) {
            // Lapítottabb Gauss: a széleken még lassabb esés, gyök alatt
            float gauss = expf(-powf(freq - centerFreq, 2) / (2.0f * sigma * sigma));
            gauss = powf(gauss, 0.5f); // gyök alatt: még lassabb esés
            gain = minGain + (boostGain - minGain) * gauss;
        }
        data[i] *= gain;
    }
}

/**
 * @brief FFT magnitúdó értékek erősítése bizonyos frekvenciatartományokban.
 * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
 */
void AudioProcessorC1::gainFttMagnitudeValues(SharedData &sharedData) {
    // Frequency-dependent amplifier profile (dB -> linear)
    // Profile requested:
    // - baseline 0 dB
    // - < 4 kHz : -6 dB (csillapítás)
    // - 7 kHz .. 9 kHz : +8 dB
    // - >= 9 kHz : +10 dB

    if (sharedData.fftBinWidthHz <= 0.0f) {
        return;
    }

    const float binHz = sharedData.fftBinWidthHz;
    for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
        float freq = i * binHz;

        float dbGain = 0.0f; // default 0 dB

        if (freq < 4000.0f) {
            dbGain = -10.0f; // csillapítás
        } else if (freq >= 7000.0f && freq < 9000.0f) {
            dbGain = 18.0f; // erősítés
        } else if (freq >= 9000.0f) {
            dbGain = 8.0f; // erősítés
        } else {
            dbGain = 0.0f; // alapértelmezett
        }

        // Convert dB to linear
        float linearGain = powf(10.0f, dbGain / 20.0f);

        // Apply per-bin
        sharedData.fftSpectrumData[i] *= linearGain;
    }
}

/**
 * @brief Feldolgozza a legfrissebb audio blokkot és feltölti a megadott SharedData struktúrát.
 * SSTV és WEFAX módban csak a nyers mintákat másoljuk, FFT nélkül.
 * Más módokban lefuttatja az FFT-t, kiszámolja a spektrumot és a domináns frekvenciát.
 */
bool AudioProcessorC1::processAndFillSharedData(SharedData &sharedData) {

    // Ha nem fut a feldolgozás, akkor kilépünk
    if (!is_running) {
        return false;
    }

#ifdef __ADPROC_DEBUG
    uint32_t methodStartTime = micros();
#endif

    // DMA buffer lekérése blokkoló vagy nem-blokkoló módon
    // - BLOKKOLÓ mód (true): SSTV/WEFAX - garantáltan teljes blokk
    // - NEM-BLOKKOLÓ mód (false): CW/RTTY - azonnal visszatér, nullptr ha nincs adat
    uint16_t *completePingPongBufferPtr = adcDmaC1.getCompletePingPongBufferPtr(useBlockingDma);

    if (completePingPongBufferPtr == nullptr) {
        // Nincs még kész adat (csak nem-blokkoló módban), később próbálkozunk újra
        // #define LAST_DMA_WORK_OUTPUT_INTERVAL 1000 * 1 // 1 másodperc
        //     static uint32_t lastDmaWorkDebugOutput = 0;
        //     if (Utils::timeHasPassed(lastDmaWorkDebugOutput, LAST_DMA_WORK_OUTPUT_INTERVAL)) {
        // ADPROC_DEBUG("AudioProc-c1: DMA még dolgozik (nem-blokkoló mód)\n");
        //          lastDmaWorkDebugOutput = millis();
        //     }
        return false;
    }

#ifdef __ADPROC_DEBUG
    uint32_t start = micros();
    // start itt a getCompletePingPongBufferPtr() hívás után van; a waitTime mutatja, mennyit vártunk a
    // friss ping-pong bufferre (blokkoló módban ez okozza a nagy Total értéket)
    uint32_t waitTime = (start >= methodStartTime) ? (start - methodStartTime) : 0;
#endif

    // 1. A nyers minták feldolgozása: DC offset eltávolítás + zajszűrés
    sharedData.rawSampleCount = std::min((uint16_t)adcConfig.sampleCount, (uint16_t)MAX_RAW_SAMPLES_SIZE); // Biztonsági ellenőrzés
    // DC komponens eltávolítása és opcionális zajszűrés (mozgó átlag simítás)
    this->removeDcAndSmooth(completePingPongBufferPtr, sharedData.rawSampleData, sharedData.rawSampleCount);

    // 2. AGC (Automatikus Erősítésszabályozás) vagy manuális gain alkalmazása
    // Ez javítja a dinamikát gyenge jelek esetén, és véd a túlvezéreléstől
    this->applyAgc(sharedData.rawSampleData, sharedData.rawSampleCount);

    // Másolat mentése a feldolgozott nyers mintákról segéd detektorokhoz (Goertzel stb.)
    lastRawSamples.resize(sharedData.rawSampleCount);
    memcpy(lastRawSamples.data(), sharedData.rawSampleData, sharedData.rawSampleCount * sizeof(int16_t));
    lastRawSampleCount = sharedData.rawSampleCount;

#ifdef __ADPROC_DEBUG
    // --- Gyors mérés: RMS, maxAbs, medián zajbecslés, SNR becslés ---
    auto computeRms = [](const int16_t *buf, uint16_t n) -> float {
        double sumsq = 0.0;
        for (uint16_t i = 0; i < n; ++i) {
            double v = (double)buf[i];
            sumsq += v * v;
        }
        return (n > 0) ? (float)sqrt(sumsq / (double)n) : 0.0f;
    };

    auto computeMaxAbs = [](const int16_t *buf, uint16_t n) -> int32_t {
        int32_t m = 0;
        for (uint16_t i = 0; i < n; ++i) {
            int32_t a = std::abs((int32_t)buf[i]);
            if (a > m)
                m = a;
        }
        return m;
    };

    auto computeMedianAbs = [](int16_t *work, const int16_t *buf, uint16_t n) -> float {
        // copy absolute values to work (caller must provide buffer length >= n)
        for (uint16_t i = 0; i < n; ++i)
            work[i] = (int16_t)std::abs((int32_t)buf[i]);
        // simple nth_element for median
        uint16_t mid = n / 2;
        std::nth_element(work, work + mid, work + n);
        if (n % 2 == 1)
            return (float)work[mid];
        // even -> average two
        int16_t a = work[mid];
        std::nth_element(work, work + mid - 1, work + n);
        int16_t b = work[mid - 1];
        return ((float)a + (float)b) * 0.5f;
    };

    // Metrikák számítása minden 50. blokknál, hogy ne árasztson el minket a debug
    static uint16_t debugCounter = 0;
    if (++debugCounter >= 50) {
        debugCounter = 0;
        float rms = computeRms(sharedData.rawSampleData, sharedData.rawSampleCount);
        int32_t maxAbs = computeMaxAbs(sharedData.rawSampleData, sharedData.rawSampleCount);

        // reuse local stack array for median (safe size check)
        static std::vector<int16_t> medianWork;
        medianWork.resize(sharedData.rawSampleCount);
        float medianAbs = computeMedianAbs(medianWork.data(), sharedData.rawSampleData, sharedData.rawSampleCount);

        // crude SNR estimate: assume signal peak ~ maxAbs, noise ~ medianAbs
        float snr_db = 0.0f;
        if (medianAbs > 0.0f) {
            float ratio = ((float)maxAbs) / medianAbs;
            snr_db = 20.0f * log10f(ratio);
        }

        ADPROC_DEBUG("AudioProc-c1 METRICS: rms=%.1f, maxAbs=%ld, medianAbs=%.1f, estSNR(dB)=%.2f\n", rms, (long)maxAbs, medianAbs, snr_db);
    }
#endif

    // Ha nem kell FFT (pl. SSTV, WEFAX), akkor nem megyünk tovább
    if (!useFFT) {
        sharedData.fftSpectrumSize = 0;      // nincs spektrum
        sharedData.dominantFrequency = 0;    // nincs domináns frekvencia
        sharedData.dominantAmplitude = 0.0f; // nincs a domináns frekvenciának amplitúdója (FLOAT)
        sharedData.fftBinWidthHz = 0.0f;     // nincs bin szélesség
        return true;
    }

    // Biztonsági ellenőrzés: a puffer mérete megfelelő-e?
    if (vReal.size() < adcConfig.sampleCount) {
        return false;
    }

    // 3. FFT bemeneti adatok feltöltése
    // A rawSampleData már int16_t DC-mentesített értékek AGC/manual gain után
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        vReal[i] = (float)sharedData.rawSampleData[i]; // int16_t -> float (skálázás már megtörtént)
    }
    // Imaginárius rész nullázása
    memset(vImag.data(), 0, adcConfig.sampleCount * sizeof(float));

#ifdef __ADPROC_DEBUG
    uint32_t preprocTime = micros() - start;
#endif

    // 4. Ablakozás (Hamming window - Arduino FFT beépített)
#ifdef __ADPROC_DEBUG
    start = micros();
#endif
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);

    // 5. FFT futtatása
    FFT.compute(FFTDirection::Forward);

#ifdef __ADPROC_DEBUG
    // Mentjük a komplex bin értékeket (real/imag) debug célokra, mielőtt a komplexToMagnitude() felülírja őket
    std::unique_ptr<float[]> debugReal(new float[adcConfig.sampleCount]);
    std::unique_ptr<float[]> debugImag(new float[adcConfig.sampleCount]);
    for (uint16_t i = 0; i < adcConfig.sampleCount; ++i) {
        debugReal[i] = vReal[i];
        debugImag[i] = vImag[i];
    }
#endif

    // 6. Magnitude számítás
    FFT.complexToMagnitude();

#ifdef __ADPROC_DEBUG
    uint32_t fftTime = micros() - start;
#endif

    // 7. Spektrum másolása a megosztott pufferbe (FLOAT -> FLOAT)
#ifdef __ADPROC_DEBUG
    start = micros();
#endif

    // Arduino FFT után a vReal tömb tartalmazza a magnitude értékeket
    // Nem-koherens spektrális átlagolás: az aktuális magnitúdó-keret mentése a körkörös avgBufferbe
    uint16_t spectrumSize = adcConfig.sampleCount / 2;
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)MAX_FFT_SPECTRUM_SIZE);

    if (spectrumAveragingCount_ <= 1) {
        // Nincs átlagolás, közvetlen átmásolás
        memcpy(sharedData.fftSpectrumData, vReal.data(), sharedData.fftSpectrumSize * sizeof(float));
    } else {
        // ensure avgBuffer size
        if (avgBuffer.size() < (size_t)spectrumAveragingCount_ * (size_t)spectrumSize) {
            avgBuffer.clear();
            avgBuffer.resize((size_t)spectrumAveragingCount_ * (size_t)spectrumSize, 0.0f);
            avgWriteIndex_ = 0;
        }

        // write current frame into avgBuffer at avgWriteIndex_
        size_t base = (size_t)avgWriteIndex_ * (size_t)spectrumSize;
        for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
            avgBuffer[base + i] = vReal[i];
        }

        // átlagolás számítása a keretek között
        for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
            float sum = 0.0f;
            for (uint8_t k = 0; k < spectrumAveragingCount_; ++k) {
                sum += avgBuffer[(size_t)k * (size_t)spectrumSize + i];
            }
            sharedData.fftSpectrumData[i] = sum / (float)spectrumAveragingCount_;
        }

        // advance circular index
        avgWriteIndex_ = (uint8_t)((avgWriteIndex_ + 1) % spectrumAveragingCount_);
    }

    // DC bin (bin[0]) nullázása, ezt nem használjuk
    if (sharedData.fftSpectrumSize > 0) {
        sharedData.fftSpectrumData[0] = 0.0f;
    }

    // Az aktuális FFT bin szélesség beállítása a sharedData-ban
    sharedData.fftBinWidthHz = this->currentBinWidthHz;

    // 8. Domináns frekvencia keresése (FLOAT)
#ifdef __ADPROC_DEBUG
    start = micros();
#endif

    // Max érték keresése float tömbben (DC bin nélkül, i=1-től indítva) a DC (0. bin) értékét már nulláztuk feljebb
    uint32_t maxIndex = 0;
    float maxValue = 0.0f;
    for (uint16_t i = 1; i < sharedData.fftSpectrumSize; i++) {
        if (sharedData.fftSpectrumData[i] > maxValue) {
            maxValue = sharedData.fftSpectrumData[i];
            maxIndex = i;
        }
    }
    sharedData.dominantAmplitude = maxValue;

    // Domináns frekvencia kiszámítása a bin indexéből Hz-ben
    float dominantFreqHz = 0.0f;
    if (adcConfig.sampleCount > 0) {
        dominantFreqHz = ((float)adcConfig.samplingRate / (float)adcConfig.sampleCount) * (float)maxIndex;
    }
    sharedData.dominantFrequency = (uint32_t)dominantFreqHz;

#ifdef __ADPROC_DEBUG
    uint32_t dominantTime = micros() - start;
    uint32_t totalTime = (micros() >= methodStartTime) ? (micros() - methodStartTime) : 0;

    // Futási idők, domináns frekvencia és a csúcsfeszültség (mV) kiírása minden 100. blokkban
    static uint8_t runDebugCounter = 0;
    if (++runDebugCounter >= 100) {

        const float N = (float)adcConfig.sampleCount;
        float amp_counts = (N > 0.0f) ? ((2.0f / N) * maxValue) : 0.0f; // egyszerű közelítés a csúcs amplitúdóra (ADC counts)
        float amp_mV_peak = amp_counts * ADC_LSB_VOLTAGE_MV;            // csúcs amplitúdó mV-ban

        ADPROC_DEBUG(
            "AudioProc-c1: Total=%lu us, Wait=%lu us, PreProc=%lu us, FFT=%lu us, DomSearch=%lu us, DomFreq=%.1f Hz, amp=%.3f (counts), peak=%.3f mV\n",
            totalTime, waitTime, preprocTime, fftTime, dominantTime, dominantFreqHz, amp_counts, amp_mV_peak);

        runDebugCounter = 0;
    }
#endif

    // Még a végén ráküldünk egy spektrum erősítést, ha nem AGC módban vagyunk
    if (!isAgcEnabled()) {
        // this->gainFttMagnitudeValues(sharedData);
    }

    return true;
}

/**
 * @brief Beállítja a spektrum átlagolásának keretszámát.
 * @param n Az átlagolandó keretek száma (1 = nincs átlagolás)
 */
void AudioProcessorC1::setSpectrumAveragingCount(uint8_t n) {
    if (n == 0) {
        n = 1;
    }

    spectrumAveragingCount_ = n;
    if (currentFftSize > 0 && useFFT) {
        uint16_t spectrumSizeLocal = currentFftSize / 2;
        avgBuffer.clear();
        avgBuffer.resize((size_t)spectrumAveragingCount_ * (size_t)spectrumSizeLocal, 0.0f);
        avgWriteIndex_ = 0;
    }
}

/**
 * @brief Lekéri a spektrum átlagolásának keretszámát.
 * @return Az átlagolandó keretek száma (1 = nincs átlagolás)
 */
uint8_t AudioProcessorC1::getSpectrumAveragingCount() const { return spectrumAveragingCount_; }

// Compute Goertzel magnitude for a target frequency using the last processed rawSampleData
float AudioProcessorC1::computeGoertzelMagnitude(float targetFreqHz) {
    if (lastRawSampleCount == 0 || lastRawSamples.empty())
        return -1.0f;
    if (adcConfig.samplingRate == 0)
        return -1.0f;

    const uint16_t N = lastRawSampleCount;
    const float fs = (float)adcConfig.samplingRate;
    // Closest bin index
    float kf = (targetFreqHz * (float)N) / fs;
    int k = (int)roundf(kf);
    if (k < 0 || k >= (int)N)
        return -1.0f;

    const float omega = 2.0f * M_PI * (float)k / (float)N;
    const float sine = sinf(omega);
    const float cosine = cosf(omega);
    const float coeff = 2.0f * cosine;

    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;
    for (uint16_t i = 0; i < N; ++i) {
        q0 = coeff * q1 - q2 + (float)lastRawSamples[i];
        q2 = q1;
        q1 = q0;
    }

    float real = q1 - q2 * cosine;
    float imag = q2 * sine;
    float magnitude = sqrtf(real * real + imag * imag);
    return magnitude;
}
