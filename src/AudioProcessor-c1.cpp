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
 * Last Modified: 2025.12.18, Thursday  08:34:32                                                                       *
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
#define __ADPROC_DEBUG
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
      useNoiseReduction_(false), // Alapértelmezett zajszűrés bekapcsolva
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

    // Ha FFT-t használunk, előkészítjük a szükséges erőforrásokat
    if (useFFT) {
        this->currentFftSize = sampleCount;

        // CMSIS-DSP Q15 fixpontos FFT inicializálás
        ADPROC_DEBUG("core1: CMSIS-DSP Q15 FFT init, sampleCount=%d\n", sampleCount);
        initFixedPointFFT(sampleCount);
        ADPROC_DEBUG("core1: CMSIS-DSP Q15 FFT init OK, useFFT=%d\n", useFFT);

        // Egy bin szélessége Hz-ben
        float binWidth = (sampleCount > 0) ? ((float)finalRate / (float)sampleCount) : 0.0f;

        // Eltároljuk a bandwidth-et a dinamikus bin-kizáráshoz (AM ~6kHz, FM ~15kHz)
        this->currentBandwidthHz = bandwidthHz;

#ifdef __ADPROC_DEBUG
        // Kiírjuk a FFT-hez kapcsolódó paramétereket
        uint16_t bins = (sampleCount / 2);                                                                                            // bin-ek száma
        ADPROC_DEBUG("AudioProc-c1 FFT paraméterek: afBandwidth=%u Hz, finalRate=%u Hz, sampleCount=%u, bins=%u, binWidth=%.2f Hz\n", //
                     this->currentBandwidthHz, finalRate, (unsigned)sampleCount, (unsigned)bins, binWidth);
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

/**
 * @brief DC komponens eltávolítása és zajszűrés mozgó átlaggal
 * @param input Bemeneti nyers ADC minták (uint16_t)
 * @param output Kimeneti DC-mentes minták zajszűréssel (int16_t)
 * @param count Minták száma
 *
 * Ez a metódus három dolgot végez:
 * 1. DC offset eltávolítása (adcMidpoint_ levonása)
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
            std::transform(input, input + count, output, [this](uint16_t v) {
                // DC offset eltávolítás
                // Use runtime measured midpoint
                return int16_t((int32_t)v - (int32_t)adcMidpoint_);
            });
        } else {
            // --------------------------------------------------
            // 1/B) DC eltávolítás + shift + clamp-elés
            // SHIFT > 0 esetén történhet túlfutás, ezért kell clamp
            // --------------------------------------------------
            std::transform(input, input + count, output, [this](uint16_t v) {
                // Minta DC korrekcióval
                int32_t val = ((int32_t)v - (int32_t)adcMidpoint_) << SHIFT;

                // Clamp int16_t tartományra
                val = std::clamp(val, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
                return (int16_t)val;
            });
        }

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
            int32_t sum = (int32_t)input[i] - (int32_t)adcMidpoint_;
            uint8_t div = 1;

            // Előző 2 minta
            if (i >= 2) {
                sum += (int32_t)input[i - 2] - (int32_t)adcMidpoint_;
                div++;
            }
            if (i >= 1) {
                sum += (int32_t)input[i - 1] - (int32_t)adcMidpoint_;
                div++;
            }

            // Következő 2 minta
            if (i + 1 < count) {
                sum += (int32_t)input[i + 1] - (int32_t)adcMidpoint_;
                div++;
            }
            if (i + 2 < count) {
                sum += (int32_t)input[i + 2] - (int32_t)adcMidpoint_;
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
            int32_t sum = (int32_t)input[i] - (int32_t)adcMidpoint_;
            uint8_t div = 1;

            // Előző minta
            if (i >= 1) {
                sum += (int32_t)input[i - 1] - (int32_t)adcMidpoint_;
                div++;
            }

            // Következő minta
            if (i + 1 < count) {
                sum += (int32_t)input[i + 1] - (int32_t)adcMidpoint_;
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

// /**
//  * @brief Egy frekvencia-tartománybeli erősítést alkalmaz az FFT adatokra "lapított" Gauss-ablak formájában.
//  * @param data FFT bemeneti/kimeneti adatok (FLOAT)
//  * @param size Az adatok mérete (bin-ek száma)
//  * @param fftBinWidthHz Egy bin szélessége Hz-ben
//  * @param boostMinHz Az erősítési tartomány alsó határa Hz-ben
//  * @param boostMaxHz Az erősítési tartomány felső határa Hz-ben
//  * @param boostGain Az erősítési tényező (pl. 10.0 = 10x erősítés)
//  *
//  */
// void AudioProcessorC1::applyFftGaussianWindow(float *data, uint16_t size, float fftBinWidthHz, float boostMinHz, float boostMaxHz, float boostGain) {

//     // Lapított Gauss-szerű erősítés: a tartományon belül a maximumhoz közel Gauss, széleken lapított
//     float centerFreq = (boostMinHz + boostMaxHz) / 2.0f;
//     float sigma = (boostMaxHz - boostMinHz) * 1.2f; // még laposabb, szélesebb görbe
//     float minGain = 1.0f;

//     for (uint16_t i = 0; i < size; i++) {
//         float freq = i * fftBinWidthHz;
//         float gain = minGain;

//         if (freq >= boostMinHz && freq <= boostMaxHz) {
//             // Lapítottabb Gauss: a széleken még lassabb esés, gyök alatt
//             float gauss = expf(-powf(freq - centerFreq, 2) / (2.0f * sigma * sigma));
//             gauss = powf(gauss, 0.5f); // gyök alatt: még lassabb esés
//             gain = minGain + (boostGain - minGain) * gauss;
//         }
//         data[i] *= gain;
//     }
// }

/**
 * @brief Kalibrálja az ADC DC középpontját a megadott mintaszám alapján.
 * @param sampleCount A kalibrációhoz használt mintaszám (alapértelmezett: ADC_MIDPOINT_MEASURE_SAMPLE_COUNT).
 */
void AudioProcessorC1::calibrateDcMidpoint(uint32_t sampleCount) {
    if (sampleCount == 0) {
        sampleCount = ADC_MIDPOINT_MEASURE_SAMPLE_COUNT;
    }

    // Ha a DMA fut, ideiglenesen leállítjuk, hogy biztonságosan tudjuk olvasni az ADC-t
    bool wasRunning = is_running;
    if (wasRunning) {
        stop();
    }

    // Állítsuk az ADC csatornát az audio bemenetre (ADC0 / GPIO26)
    adc_select_input(0);

    uint64_t sum = 0;
    for (uint32_t i = 0; i < sampleCount; ++i) {
        sleep_us(50); // Rövid várakozás az ADC stabilizálására
        uint32_t v = analogRead(PIN_AUDIO_INPUT);
        sum += v;
    }

    uint32_t midpoint = static_cast<uint32_t>(sum / sampleCount);
    adcMidpoint_ = midpoint;

    // Ha előtte futott a DMA, indítsuk újra
    if (wasRunning) {
        start();
    }

#ifdef __ADPROC_DEBUG
    // Számoljuk a mért ADC értéknek megfelelő feszültséget (12-bit ADC, Vref ~= 3.3V)
    float adcV = ((float)midpoint * CORE1_ADC_V_REFERENCE) / CORE1_ADC_CONVERSION_FACTOR;
    ADPROC_DEBUG("AudioProc-c1: Kalibrált ADC midpoint=%u (mérések száma: %u), V=%.3f V\n", midpoint, sampleCount, adcV);
#endif
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

    // #ifdef __ADPROC_DEBUG
    //     // --- Gyors mérés: RMS, maxAbs, medián zajbecslés, SNR becslés ---
    //     auto computeRms = [](const int16_t *buf, uint16_t n) -> float {
    //         double sumsq = 0.0;
    //         for (uint16_t i = 0; i < n; ++i) {
    //             double v = (double)buf[i];
    //             sumsq += v * v;
    //         }
    //         return (n > 0) ? (float)sqrt(sumsq / (double)n) : 0.0f;
    //     };

    //     auto computeMaxAbs = [](const int16_t *buf, uint16_t n) -> int32_t {
    //         int32_t m = 0;
    //         for (uint16_t i = 0; i < n; ++i) {
    //             int32_t a = std::abs((int32_t)buf[i]);
    //             if (a > m)
    //                 m = a;
    //         }
    //         return m;
    //     };

    //     auto computeMedianAbs = [](int16_t *work, const int16_t *buf, uint16_t n) -> float {
    //         // copy absolute values to work (caller must provide buffer length >= n)
    //         for (uint16_t i = 0; i < n; ++i)
    //             work[i] = (int16_t)std::abs((int32_t)buf[i]);
    //         // simple nth_element for median
    //         uint16_t mid = n / 2;
    //         std::nth_element(work, work + mid, work + n);
    //         if (n % 2 == 1)
    //             return (float)work[mid];
    //         // even -> average two
    //         int16_t a = work[mid];
    //         std::nth_element(work, work + mid - 1, work + n);
    //         int16_t b = work[mid - 1];
    //         return ((float)a + (float)b) * 0.5f;
    //     };

    //     // Metrikák számítása minden 50. blokknál, hogy ne árasztson el minket a debug
    //     static uint16_t debugCounter = 0;
    //     if (++debugCounter >= 50) {
    //         debugCounter = 0;
    //         float rms = computeRms(sharedData.rawSampleData, sharedData.rawSampleCount);
    //         int32_t maxAbs = computeMaxAbs(sharedData.rawSampleData, sharedData.rawSampleCount);

    //         // reuse local stack array for median (safe size check)
    //         static std::vector<int16_t> medianWork;
    //         medianWork.resize(sharedData.rawSampleCount);
    //         float medianAbs = computeMedianAbs(medianWork.data(), sharedData.rawSampleData, sharedData.rawSampleCount);

    //         // crude SNR estimate: assume signal peak ~ maxAbs, noise ~ medianAbs
    //         float snr_db = 0.0f;
    //         if (medianAbs > 0.0f) {
    //             float ratio = ((float)maxAbs) / medianAbs;
    //             snr_db = LINEAR_TO_DECIBELL(ratio);
    //         }

    //         ADPROC_DEBUG("AudioProc-c1 METRICS: rms=%.1f, maxAbs=%ld, medianAbs=%.1f, estSNR(dB)=%.2f\n", rms, (long)maxAbs, medianAbs, snr_db);
    //     }
    // #endif

    // Ha nem kell FFT (pl. SSTV, WEFAX), akkor nem megyünk tovább
    if (!useFFT) {
        sharedData.fftSpectrumSize = 0;      // nincs spektrum
        sharedData.dominantFrequency = 0;    // nincs domináns frekvencia
        sharedData.dominantAmplitude = 0.0f; // nincs a domináns frekvenciának amplitúdója (FLOAT)
        sharedData.fftBinWidthHz = 0.0f;     // nincs bin szélesség
        return true;
    }

    // CMSIS-DSP Q15 fixpontos FFT feldolgozás
    uint32_t fftTime_us = 0, domTime_us = 0;
    return processFixedPointFFT(sharedData, fftTime_us, domTime_us);
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

/**
 * @brief Fixpontos FFT inicializálása
 * @param sampleCount FFT méret
 */
void AudioProcessorC1::initFixedPointFFT(uint16_t sampleCount) {
    // CMSIS-DSP FFT instance inicializálása
    arm_status status = arm_cfft_init_q15(&fft_inst_q15, sampleCount);
    if (status != ARM_MATH_SUCCESS) {
        ADPROC_DEBUG("HIBA: CMSIS-DSP FFT inicializálás nem sikerült: %d\n", status);
        // Fallback lebegőpontosra
        useFixedPointFFT_ = false;
        return;
    }

    // Fixpontos pufferek méretezése
    fftInput_q15.resize(sampleCount * 2); // Komplex: [re,im,re,im,...]
    magnitude_q15.resize(sampleCount);    // Magnitude eredmény - TELJES FFT kimenet (csak felét használjuk majd)

    // Hanning ablak létrehozása fixpontosan
    buildHanningWindow_q15(sampleCount);

    ADPROC_DEBUG("CMSIS-DSP Q15 FFT sikeresen inicializálva: %d minta\n", sampleCount);
}

/**
 * @brief Fixpontos Hanning ablak létrehozása
 * @param size Ablak mérete
 */
void AudioProcessorC1::buildHanningWindow_q15(uint16_t size) {
    hanningWindow_q15.resize(size);

    for (uint16_t i = 0; i < size; i++) {
        // Hanning ablak: w[n] = 0.5 * (1 - cos(2*pi*n/(N-1)))
        float angle = 2.0f * M_PI * i / (size - 1);
        float window_val = 0.5f * (1.0f - cosf(angle));
        hanningWindow_q15[i] = floatToQ15(window_val);
    }

    ADPROC_DEBUG("Hanning ablak létrehozva Q15 formátumban: %d elem\n", size);
}

/**
 * @brief Ellenőrzi, hogy egy FFT bin index a megadott audio frekvenciatartományon belül van-e.
 * A felső határt dinamikusan határozza meg az aktuális bandwidth alapján (AM ~6kHz, FM ~15kHz).
 * Ez javítja az AM waterfall három-sáv artefaktját, ami a statikus MAX_AUDIO_FREQUENCY_HZ (15kHz) miatt keletkezett.
 * @param binIndex A vizsgált bin index
 * @param binWidthHz Egy bin szélessége Hz-ben
 * @param spectrumSize A spektrum mérete (bin-ek száma)
 * @return true, ha a bin benne van a [MIN_AUDIO_FREQUENCY_HZ, dinamikus maxFreq] tartományban, különben false
 */
bool AudioProcessorC1::isBinInAudioRange(uint16_t binIndex, float binWidthHz, uint16_t spectrumSize) const {
    if (binWidthHz <= 0.0f || spectrumSize == 0) {
        return false;
    }

    // Dinamikus felső határ: ha currentBandwidthHz be van állítva (AM ~6kHz, FM ~15kHz),
    // akkor azt használjuk; egyébként visszaesünk a statikus MAX_AUDIO_FREQUENCY_HZ-re (kompatibilitás)
    float maxFreq = (currentBandwidthHz > 0) ? (float)currentBandwidthHz : MAX_AUDIO_FREQUENCY_HZ;

    // Bin alsó és felső határa (Hz)
    float binLow = (float)binIndex * binWidthHz;
    float binHigh = (float)(binIndex + 1) * binWidthHz;

    // Ha a bin teljesen a tartományon kívül esik, false.
    if (binHigh <= MIN_AUDIO_FREQUENCY_HZ) {
        return false;
    }
    if (binLow >= maxFreq) {
        return false;
    }

    return true; // részben vagy teljesen átfed -> megtartjuk
}

/**
 * @brief Fixpontos FFT feldolgozás (CMSIS-DSP)
 * @param sharedData SharedData struktúra kitöltéséhez
 * @return Sikeres feldolgozás esetén true
 */
bool AudioProcessorC1::processFixedPointFFT(SharedData &sharedData, uint32_t &fftTime_us, uint32_t &domTime_us) {

    // Biztonsági ellenőrzés
    if (fftInput_q15.size() < adcConfig.sampleCount * 2) {
        ADPROC_DEBUG("HIBA: Fixpontos FFT puffer túl kicsi\n");
        return false;
    }

#ifdef __ADPROC_DEBUG
    uint32_t startTotal = micros();
    q15_t inputMax = 0, windowedMax = 0, fftMax = 0;
#endif

    // 1. int16_t -> q15_t konverzió + komplex formátumba rendezés
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        // Direkt másolás: int16_t és q15_t ugyanaz a formátum!
        fftInput_q15[2 * i] = sharedData.rawSampleData[i]; // Real rész
        fftInput_q15[2 * i + 1] = 0;                       // Imaginárius rész = 0
    }

#ifdef __ADPROC_DEBUG
    // DEBUG: Nyers bemeneti statisztika (ablakozás előtt)
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        q15_t absVal = abs(fftInput_q15[2 * i]);
        if (absVal > inputMax)
            inputMax = absVal;
    }
#endif

    // 2. Hanning ablak alkalmazása (fixpontos szorzás)
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        // Q15 * Q15 = Q30, majd >> 15 = Q15
        q31_t windowed = ((q31_t)fftInput_q15[2 * i] * hanningWindow_q15[i]) >> 15;
        fftInput_q15[2 * i] = __SSAT(windowed, 16); // Saturáció Q15 tartományra
    }

#ifdef __ADPROC_DEBUG
    // DEBUG: Ablakozott adatok statisztika
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        q15_t absVal = abs(fftInput_q15[2 * i]);
        if (absVal > windowedMax)
            windowedMax = absVal;
    }
#endif

    // 3. CMSIS-DSP FFT futtatása
    uint32_t startFft = micros();
    arm_cfft_q15(&fft_inst_q15, fftInput_q15.data(), 0, 1); // 0=FFT, 1=bit-reversal

    // KRITIKUS: A CMSIS-DSP Q15 FFT automatikus skálázást végez minden butterfly szakaszban!
    // Ez log2(N) bites jobbra tolást jelent. Például N=1024 esetén 10 bit veszteség.
    // Vissza kell majd skálázni a kimenet értékeit!
    uint16_t fftScaleBits = 0;
    uint16_t N = adcConfig.sampleCount;
    while (N > 1) {
        fftScaleBits++;
        N >>= 1;
    }
    // Teljes rescale, majd utána jön a magnitude számítás.
    for (uint16_t i = 0; i < adcConfig.sampleCount * 2; i++) {
        q31_t scaled = ((q31_t)fftInput_q15[i]) << fftScaleBits;
        fftInput_q15[i] = __SSAT(scaled, 16);
    }

#ifdef __ADPROC_DEBUG
    // DEBUG: FFT kimenet statisztika (komplex adatok) - skálázás után
    for (uint16_t i = 0; i < adcConfig.sampleCount; i++) {
        q15_t absVal = abs(fftInput_q15[i]);
        if (absVal > fftMax)
            fftMax = absVal;
    }

    uint32_t endFft = micros();
    fftTime_us = endFft - startFft;
    uint32_t startDom = micros();
#endif

    // 4. Magnitude számítás
    // FONTOS: arm_cmplx_mag_q15 harmadik paramétere a KOMPLEX számok száma!
    // Az FFT kimenet N komplex szám (2*N q15_t érték), magnitude kimenet N érték
    // Mi csak az első N/2-t használjuk (pozitív frekvenciák)
    arm_cmplx_mag_q15(fftInput_q15.data(), magnitude_q15.data(), adcConfig.sampleCount);

    // 5. Spektrum adatok másolása SharedData-ba (Q15 formátumban)
    uint16_t spectrumSize = adcConfig.sampleCount / 2;
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)MAX_FFT_SPECTRUM_SIZE);

    if (spectrumAveragingCount_ <= 1) {
        // Nincs átlagolás, közvetlen másolás Q15 formátumban
        memcpy(sharedData.fftSpectrumData, magnitude_q15.data(), sharedData.fftSpectrumSize * sizeof(q15_t));
    } else {
        // TODO: Q15 átlagolás implementálása (egyelőre egyszerű másolás)
        memcpy(sharedData.fftSpectrumData, magnitude_q15.data(), sharedData.fftSpectrumSize * sizeof(q15_t));
    }

    // DC bin (bin[0]) nullázása Q15 formátumban
    if (sharedData.fftSpectrumSize > 0) {
        sharedData.fftSpectrumData[0] = 0;
    }

    // Bin szélesség beállítása (ELŐBB, mert a bin-kizáráshoz kell)
    sharedData.fftBinWidthHz = currentBinWidthHz;

    // --- BIN-KIZÁRÁS: Frekvencia tartományon kívüli bin-ek nullázása (MIN/MAX alapján) ---
    // KRITIKUS: Ez ELŐBB fut le, mint a zajküszöb!
    if (sharedData.fftBinWidthHz > 0.0f) {
        // Először: egyszerű bin-enkénti kizárás a tartományon kívülre
        for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
            if (!isBinInAudioRange(i, sharedData.fftBinWidthHz, sharedData.fftSpectrumSize)) {
                sharedData.fftSpectrumData[i] = 0;
            }
        }

        // Másodszor: pre-max ellenőrzés (MOST MÁR a kizárt adatokon dolgozunk!)
        uint16_t preMaxIdx = 0;
        q15_t preMaxVal = 0;
        for (uint16_t i = 1; i < sharedData.fftSpectrumSize; ++i) { // DC kihagyása
            if (sharedData.fftSpectrumData[i] > preMaxVal) {
                preMaxVal = sharedData.fftSpectrumData[i];
                preMaxIdx = i;
            }
        }

        if (preMaxVal > 0) {
            // Dinamikus felső határ használata (AM ~6kHz, FM ~15kHz)
            float maxFreq = (currentBandwidthHz > 0) ? (float)currentBandwidthHz : MAX_AUDIO_FREQUENCY_HZ;
            float preMaxFreq = (float)preMaxIdx * sharedData.fftBinWidthHz;
            // Ha a legnagyobb csúcs out-of-band (ez már NEM kellene hogy előforduljon a fenti kizárás után)
            if (preMaxFreq < MIN_AUDIO_FREQUENCY_HZ || preMaxFreq > maxFreq) {
                // Biztonsági fallback: teljes spektrum törlése
                memset(sharedData.fftSpectrumData, 0, sharedData.fftSpectrumSize * sizeof(sharedData.fftSpectrumData[0]));
                sharedData.dominantAmplitude = 0;
                sharedData.dominantFrequency = 0;
                return true;
            }
        }
    }

    // --- ZAJKÜSZÖB ALKALMAZÁSA (BIN-KIZÁRÁS UTÁN!) ---
    // Az alacsony amplitúdójú bin-eket nullázzuk (zaj-elnyomás)
    // KRITIKUS: Ez UTÁN történik a bin-kizárásnak, így csak az érvényes tartományon belüli zajt szűri!
    //
    // ADAPTÍV KÜSZÖB: Sávszélesség alapú + jelerősség alapú
    // AM (6kHz) → magasabb küszöb, FM (15kHz) → alacsonyabb küszöb

    // Először megkeressük a max értéket (pre-scan)
    q15_t preMaxValue = 0;
    for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
        if (sharedData.fftSpectrumData[i] > preMaxValue) {
            preMaxValue = sharedData.fftSpectrumData[i];
        }
    }

    // Sávszélesség alapú statikus küszöb számítás:
    // AM (6kHz)  → 1500 küszöb (szűk sáv, erősebb zajszűrés)
    // FM (15kHz) → 400 küszöb (széles sáv, enyhébb zajszűrés)
    // Lineáris interpoláció a kettő között
    constexpr uint16_t STATIC_NOISE_THRESHOLD_AM = 1500;
    constexpr uint16_t STATIC_NOISE_THRESHOLD_FM = 380;
    q15_t staticThreshold = STATIC_NOISE_THRESHOLD_FM; // FM az alapértelmezett érték
    if (currentBandwidthHz > 0 && currentBandwidthHz <= AM_AF_BANDWIDTH_HZ) {
        // AM tartomány (≤6kHz): magas küszöb
        staticThreshold = STATIC_NOISE_THRESHOLD_AM;
    } else if (currentBandwidthHz > AM_AF_BANDWIDTH_HZ && currentBandwidthHz < FM_AF_BANDWIDTH_HZ) {
        // Köztes sávszélesség: lineáris interpoláció STATIC_NOISE_THRESHOLD_AM (6kHz) és STATIC_NOISE_THRESHOLD_FM (15kHz) között
        staticThreshold = STATIC_NOISE_THRESHOLD_AM - ((currentBandwidthHz - AM_AF_BANDWIDTH_HZ) * (STATIC_NOISE_THRESHOLD_AM - STATIC_NOISE_THRESHOLD_FM)) /
                                                          (FM_AF_BANDWIDTH_HZ - AM_AF_BANDWIDTH_HZ);
    }
    // Ha currentBandwidthHz == 0 (nincs beállítva) vagy ≥FM_AF_BANDWIDTH_HZ, akkor marad STATIC_NOISE_THRESHOLD_FM

    // Jelerősség alapú adaptív küszöb (ha van elég erős jel)
    q15_t adaptiveThreshold = staticThreshold;
    if (preMaxValue > 2000) {
        // Adaptív: 8% a maximum-ból, de minimum a staticThreshold
        adaptiveThreshold = std::max(staticThreshold, (q15_t)(preMaxValue / 12));
    }

    // Zajküszöb alkalmazása
    for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
        if (sharedData.fftSpectrumData[i] > 0 && sharedData.fftSpectrumData[i] < adaptiveThreshold) {
            sharedData.fftSpectrumData[i] = 0;
        }
    }

    // 7. Domináns frekvencia keresése Q15 formátumban
    uint32_t maxIndex = 0;
    q15_t maxValue = 0;
    for (uint16_t i = 1; i < sharedData.fftSpectrumSize; i++) { // DC bin kihagyása -> 1-es bin-től
        // Ha a bin értéke nulla (kizárt vagy nincs jel), átugorjuk
        if (sharedData.fftSpectrumData[i] == 0)
            continue;
        if (sharedData.fftSpectrumData[i] > maxValue) {
            maxValue = sharedData.fftSpectrumData[i];
            maxIndex = i;
        }
    }
    sharedData.dominantAmplitude = maxValue;

    // Domináns frekvencia Hz-ben
    float dominantFreqHz = 0.0f;
    if (adcConfig.sampleCount > 0) {
        dominantFreqHz = ((float)adcConfig.samplingRate / (float)adcConfig.sampleCount) * (float)maxIndex;
    }
    sharedData.dominantFrequency = (uint32_t)dominantFreqHz;

#ifdef __ADPROC_DEBUG
    uint32_t endDom = micros();
    domTime_us = endDom - startDom;
    uint32_t endTotal = micros();
    uint32_t totalTime_us = endTotal - startTotal;

    // Teljesítmény kimutatás minden 200. blokkban
    static uint8_t runDebugCounter = 0;
    if (++runDebugCounter >= 200) {
        const float N = (float)adcConfig.sampleCount;
        float amp_q15 = (float)maxValue / Q15_MAX_AS_FLOAT; // Q15 → float konverzió csak debug céljára
        float amp_mV_peak = amp_q15 * ADC_LSB_VOLTAGE_MV * N;
        float amp_mV_vpp = amp_mV_peak * 2.0f;

        ADPROC_DEBUG("AudioProc-c1 [Q15]: Total=%lu us, FFT=%lu us, DomFreq=%.1f Hz, amp=%d, pk=%.1f mV, Vpp=%.1f mVpp\n", totalTime_us, fftTime_us,
                     dominantFreqHz, maxValue, amp_mV_peak, amp_mV_vpp);

        // // Spektrum statisztika: hány bin nulla/nem-nulla
        // uint16_t nonZeroBins = 0;
        // uint16_t zeroBins = 0;
        // q15_t minNonZero = 32767;
        // q15_t maxNonZero = 0;
        // for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
        //     if (sharedData.fftSpectrumData[i] == 0) {
        //         zeroBins++;
        //     } else {
        //         nonZeroBins++;
        //         if (sharedData.fftSpectrumData[i] < minNonZero) {
        //             minNonZero = sharedData.fftSpectrumData[i];
        //         }
        //         if (sharedData.fftSpectrumData[i] > maxNonZero) {
        //             maxNonZero = sharedData.fftSpectrumData[i];
        //         }
        //     }
        // }
        //
        // ADPROC_DEBUG("  Spectrum: bins=%d, nonZero=%d, zero=%d, range=[%d..%d], binWidth=%.1f Hz\n", sharedData.fftSpectrumSize, nonZeroBins, zeroBins,
        //              minNonZero, maxNonZero, currentBinWidthHz);

        runDebugCounter = 0;
    }
#endif

    return true;
}
