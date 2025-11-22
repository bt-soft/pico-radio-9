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
 * Last Modified: 2025.11.22, Saturday  02:53:31                                                                       *
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

    adcConfig.sampleCount = sampleCount;
    adcConfig.samplingRate = static_cast<uint16_t>(finalRate);

    ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() - adcConfig frissítve: sampleCount=%d, samplingRate=%d Hz\n", adcConfig.sampleCount,
                 adcConfig.samplingRate);
    this->start();
}

/**
 * @brief Ellenőrzi, hogy a bemeneti jel meghaladja-e a küszöbértéket.
 * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
 * @return true, ha a jel meghaladja a küszöböt, különben false.
 */
//[[maybe_unused]] // sehol sem használjuk
bool AudioProcessorC1::checkSignalThreshold(int16_t *samples, uint16_t count) {

    // Kikeressük a max abszolút értéket a bemeneti minták között
    int16_t maxAbsRaw = std::abs(*std::max_element( //
        samples, samples + count,                   //
        [](int16_t a, int16_t b) {                  //
            return std::abs(a) < std::abs(b);       //
        }));

    // Küszöb: ha a bemenet túl kicsi, akkor ne vegyen fel hamis spektrum-csúcsot.
    constexpr int16_t RAW_SIGNAL_THRESHOLD = 80; // nyers ADC egységben, hangolandó
    if (maxAbsRaw < RAW_SIGNAL_THRESHOLD) {
#ifdef __ADPROC_DEBUG
        // Ha túl kicsi a jel akkor azt jelezzük
        static uint8_t tresholdDebugCounter = 0;
        if (++tresholdDebugCounter >= 100) {
            ADPROC_DEBUG("AudioProc-c1: nincs audió jel (maxAbsRaw=%d) -- FFT kihagyva\n", (int)maxAbsRaw);
            tresholdDebugCounter = 0;
        }
#endif
        return false;
    }

    // Jel meghaladja a küszöböt
    return true;
}

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

    // FFT magnutúdó adatok erősítése bizonyos frekvenciatartományokban
    //  Egyszerű high-frequency boost + extra 5-8kHz erősítés
    //  - Lineáris, ésszerű maximum: 3x (konzervatív)
    //  Valamiért a kütyüm 5.5-8.5kHz között gyengébben látja a jeleket
    //  - 5-8 kHz tartományban további, centrikus 2x (triangular profil)
    constexpr float BOOST_START_HZ = 2500.0f;      // itt kezdődik az általános erősítés
    constexpr float BOOST_MAX_GAIN_LINEAR = 20.0f; // konstans max lineáris erősítés (60x)
    constexpr float EXTRA_MIN_HZ = 7000.0f;        // extra boost alsó határa
    constexpr float EXTRA_MAX_HZ = 9000.0f;        // extra boost felső határa
    constexpr float EXTRA_FACTOR = 10.0f;          // extra boost a tartomány közepén (4x)

    float nyquist = (float)adcConfig.samplingRate / 2.0f;
    if (sharedData.fftBinWidthHz > 0.0f && nyquist > BOOST_START_HZ) {
        for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
            float freq = i * sharedData.fftBinWidthHz;
            if (freq <= BOOST_START_HZ) {
                continue;
            }

            // Lineáris, normalizált tényező 0..1
            float t = (freq - BOOST_START_HZ) / (nyquist - BOOST_START_HZ);
            constrain(t, 0.0f, 1.0f);
            float linearGain = 1.0f + t * (BOOST_MAX_GAIN_LINEAR - 1.0f);

            // Extra 5-8 kHz centrikus háromszög profil (center kapja a teljes EXTRA_FACTOR-t)
            float extraGain = 1.0f;
            if (freq >= EXTRA_MIN_HZ && freq <= EXTRA_MAX_HZ) {
                float center = (EXTRA_MIN_HZ + EXTRA_MAX_HZ) * 0.5f;
                float halfWidth = (EXTRA_MAX_HZ - EXTRA_MIN_HZ) * 0.5f;
                float triangular = 0.0f;
                if (halfWidth > 0.0f) {
                    triangular = 1.0f - (fabsf(freq - center) / halfWidth); // 0..1
                    if (triangular < 0.0f)
                        triangular = 0.0f;
                }
                extraGain = 1.0f + (EXTRA_FACTOR - 1.0f) * triangular;
            }

            float totalGain = linearGain * extraGain;
            sharedData.fftSpectrumData[i] *= totalGain;
        }
    }

    // Végső, globális erősítés: +12 dB minden FFT magnitúdóra (± kalibrációs igény)
    // +12 dB ≈ linear factor 10^(12/20) = ~3.9810717055
    constexpr float GLOBAL_GAIN_DB = 12.0f;
    const float linearGain = powf(10.0f, GLOBAL_GAIN_DB / 20.0f);
    for (uint16_t i = 0; i < sharedData.fftSpectrumSize; ++i) {
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
    // Átmásoljuk a SharedData-ba memcpy-val
    uint16_t spectrumSize = adcConfig.sampleCount / 2;
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)MAX_FFT_SPECTRUM_SIZE);
    memcpy(sharedData.fftSpectrumData, vReal.data(), sharedData.fftSpectrumSize * sizeof(float));

    // DC bin (bin[0]) nullázása, ezt úgy sem használjuk
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
    static uint8_t debugCounter = 0;
    if (++debugCounter >= 100) {

        const float N = (float)adcConfig.sampleCount;
        float amp_counts = (N > 0.0f) ? ((2.0f / N) * maxValue) : 0.0f; // egyszerű közelítés a csúcs amplitúdóra (ADC counts)
        float amp_mV_peak = amp_counts * ADC_LSB_VOLTAGE_MV;            // csúcs amplitúdó mV-ban

        ADPROC_DEBUG(
            "AudioProc-c1: Total=%lu us, Wait=%lu us, PreProc=%lu us, FFT=%lu us, DomSearch=%lu us, DomFreq=%.1f Hz, amp=%.3f (counts), peak=%.3f mV\n",
            totalTime, waitTime, preprocTime, fftTime, dominantTime, dominantFreqHz, amp_counts, amp_mV_peak);

        debugCounter = 0;
    }
#endif

    // Még a végén ráküldünk egy spektrum erősítést, ha nem AGC módban vagyunk
    // if (!isAgcEnabled()) {
        this->gainFttMagnitudeValues(sharedData);
    // }

    return true;
}
