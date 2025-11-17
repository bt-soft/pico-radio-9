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
 * Last Modified: 2025.11.17, Monday  07:19:37                                                                         *
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
      agcLevel_(2000.0f),       //
      agcAlpha_(0.02f),         //
      agcTargetPeak_(20000.0f), //
      agcMinGain_(0.1f),        //
      agcMaxGain_(20.0f),       // 20.0
      currentAgcGain_(1.0f),    //

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
    ADPROC_DEBUG("core1: AudioProc-c1 start: elindítva, sampleCount=%d, samplingRate=%d Hz, useFFT=%d, is_running=%d\n", adcConfig.sampleCount, adcConfig.samplingRate, useFFT, is_running);
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

    ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() HÍVÁS - sampleCount=%d, samplingRate=%d Hz, bandwidthHz=%d Hz\n", sampleCount, samplingRate, bandwidthHz);

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
        uint32_t afBandwidth = bandwidthHz;                                                                                                   // hangfrekvenciás sávszélesség
        uint16_t bins = (sampleCount / 2);                                                                                                    // bin-ek száma
        ADPROC_DEBUG("AudioProc-c1 Arduino FFT paraméterek: afBandwidth=%u Hz, finalRate=%u Hz, sampleCount=%u, bins=%u, binWidth=%.2f Hz\n", //
                     afBandwidth, finalRate, (unsigned)sampleCount, (unsigned)bins, binWidth);
#endif

        // Eltároljuk a kiszámított bin szélességet az példányban, hogy a feldolgozás a SharedData-ba írhassa
        this->currentBinWidthHz = binWidth;
    }

    adcConfig.sampleCount = sampleCount;
    adcConfig.samplingRate = static_cast<uint16_t>(finalRate);

    ADPROC_DEBUG("AudioProc-c1::reconfigureAudioSampling() - adcConfig frissítve: sampleCount=%d, samplingRate=%d Hz\n", adcConfig.sampleCount, adcConfig.samplingRate);
    this->start();
}

/**
 * @brief Ellenőrzi, hogy a bemeneti jel meghaladja-e a küszöbértéket.
 * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
 * @return true, ha a jel meghaladja a küszöböt, különben false.
 */
[[maybe_unused]] // sehol sem használjuk
bool AudioProcessorC1::checkSignalThreshold(SharedData &sharedData) {

    int32_t maxAbsRaw = 0;
    for (int i = 0; i < (int)sharedData.rawSampleCount; ++i) {
        int32_t v = (int32_t)sharedData.rawSampleData[i];
        if (v < 0) {
            v = -v;
        }
        if (v > maxAbsRaw) {
            maxAbsRaw = v;
        }
    }
    // Küszöb: ha a bemenet túl kicsi, akkor ne vegyen fel hamis spektrum-csúcsot.
    constexpr int32_t RAW_SIGNAL_THRESHOLD = 80; // nyers ADC egységben, hangolandó
    if (maxAbsRaw < RAW_SIGNAL_THRESHOLD) {
        // Ha túl kicsi a jel, rögtön jelezzük
        ADPROC_DEBUG("AudioProc-c1: nincs audió jel (maxAbsRaw=%d) -- FFT kihagyva\n", (int)maxAbsRaw);
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

    if (!useNoiseReduction_ || smoothingPoints_ == 0) {
        // Zajszűrés kikapcsolva - csak DC offset eltávolítás (gyors)
        // JAVÍTVA: Helyes típuskonverzió uint16_t -> int16_t
        // Alkalmazzuk a legacy q15 skálázást itt: balra shift (15 - ADC_BIT_DEPTH)
        // így a sharedData.rawSampleData már azonos numerikus skálán lesz, mint a régi CMSIS-q15 út.
        const int shift = (15 - ADC_BIT_DEPTH);
        if (shift <= 0) {
            for (uint16_t i = 0; i < count; i++) {
                output[i] = (int16_t)((int32_t)input[i] - ADC_MIDPOINT);
            }
        } else {
            for (uint16_t i = 0; i < count; i++) {
                int32_t val = ((int32_t)input[i] - ADC_MIDPOINT) << shift;
                // Clamp to int16_t range after shift
                if (val > 32767)
                    val = 32767;
                if (val < -32768)
                    val = -32768;
                output[i] = (int16_t)val;
            }
        }
        return;
    }

    // Zajszűrés bekapcsolva - mozgó átlag simítás
    if (smoothingPoints_ == 5) {
        // 5-pontos mozgó átlag (erősebb simítás)
        // Figyelem: Ez szélesíti az FFT bin-eket!
        for (uint16_t i = 0; i < count; i++) {
            int32_t sum = (int32_t)input[i] - ADC_MIDPOINT;
            uint8_t divisor = 1;

            // Előző 2 minta
            if (i >= 2) {
                sum += (int32_t)input[i - 2] - ADC_MIDPOINT;
                divisor++;
            }
            if (i >= 1) {
                sum += (int32_t)input[i - 1] - ADC_MIDPOINT;
                divisor++;
            }

            // Következő 2 minta
            if (i < count - 1) {
                sum += (int32_t)input[i + 1] - ADC_MIDPOINT;
                divisor++;
            }
            if (i < count - 2) {
                sum += (int32_t)input[i + 2] - ADC_MIDPOINT;
                divisor++;
            }

            // Alkalmazzuk a skálázást (shift) a simított értékre is
            const int shift = (15 - ADC_BIT_DEPTH);
            if (shift <= 0) {
                output[i] = (int16_t)(sum / divisor);
            } else {
                int32_t val = (sum / divisor) << shift;
                if (val > 32767)
                    val = 32767;
                if (val < -32768)
                    val = -32768;
                output[i] = (int16_t)val;
            }
        }
    } else {
        // 3-pontos mozgó átlag (gyorsabb, enyhébb simítás)
        // Ez is enyhén szélesíti az FFT bin-eket, de elfogadható CW/RTTY-nél
        for (uint16_t i = 0; i < count; i++) {
            int32_t sum = (int32_t)input[i] - ADC_MIDPOINT;
            uint8_t divisor = 1;

            if (i > 0) {
                sum += (int32_t)input[i - 1] - ADC_MIDPOINT;
                divisor++;
            }
            if (i < count - 1) {
                sum += (int32_t)input[i + 1] - ADC_MIDPOINT;
                divisor++;
            }

            // Alkalmazzuk a skálázást (shift) a simított értékre is
            const int shift = (15 - ADC_BIT_DEPTH);
            if (shift <= 0) {
                output[i] = (int16_t)(sum / divisor);
            } else {
                int32_t val = (sum / divisor) << shift;
                if (val > 32767)
                    val = 32767;
                if (val < -32768)
                    val = -32768;
                output[i] = (int16_t)val;
            }
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
        static uint32_t agcDebugCounter = 0;
        if (++agcDebugCounter >= 100) {
            ADPROC_DEBUG("AudioProcessorC1::applyAgc: MANUAL AGC mode, manualGain=%.2f\n", manualGain_);
            agcDebugCounter = 0;
        }
        return;
    }

    // AGC mód
    // 1. Maximum keresése a blokkban
    int32_t maxAbs = 0;
    for (uint16_t i = 0; i < count; i++) {
        int32_t abs_val = abs(samples[i]);
        if (abs_val > maxAbs) {
            maxAbs = abs_val;
        }
    }

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
        ADPROC_DEBUG("AudioProcessorC1::applyAgc: AUTO AGC mode, maxAbs=%d, agcLevel=%.1f, targetGain=%.2f, currentGain=%.2f\n", (int)maxAbs, agcLevel_, targetGain, currentAgcGain_);
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
    removeDcAndSmooth(completePingPongBufferPtr, sharedData.rawSampleData, sharedData.rawSampleCount);     // DC komponens eltávolítása és opcionális zajszűrés (mozgó átlag simítás)

    // 2. AGC (Automatikus Erősítésszabályozás) vagy manuális gain alkalmazása
    // Ez javítja a dinamikát gyenge jelek esetén, és véd a túlvezéreléstől
    applyAgc(sharedData.rawSampleData, sharedData.rawSampleCount);

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

    // 3. FFT bemeneti adatok feltöltése (FLOAT - Arduino FFT)
    // rawSampleData már int16_t DC-mentesített értékek AGC/manual gain után
    // Itt az int16_t -> float konverzió történik, nem lehet a memcpy-t használni
    // A nyers minták már DC-mentesítve és skálázva vannak a removeDcAndSmooth()-ben
    for (int i = 0; i < adcConfig.sampleCount; i++) {
        vReal[i] = (float)sharedData.rawSampleData[i]; // int16_t -> float (skálázás már megtörtént)
    }

#ifdef __ADPROC_DEBUG
    // Debug: néhány kezdeti bin érték (ritkán írjuk ki)
    static uint32_t dbgCounter = 0;
    if (++dbgCounter >= 100) {
        ADPROC_DEBUG("AudioProc-c1: firstBins=%.1f,%.1f,%.1f\n", vReal[0], vReal[1], vReal[2]);
        dbgCounter = 0;
    }
#endif
    // Imaginárius rész nullázása (gyors memset)
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

    // 6. Magnitude számítás
    FFT.complexToMagnitude();

#ifdef __ADPROC_DEBUG
    uint32_t fftTime = micros() - start;
#endif

    // 7. Spektrum másolása a megosztott pufferbe (FLOAT -> FLOAT)
#ifdef __ADPROC_DEBUG
    start = micros();
#endif

    // Arduino FFT után a vReal tömb tartalmazza a magnitude értékeket (FLOAT)
    // Átmásoljuk a SharedData-ba memcpy-val (FLOAT -> FLOAT, gyors)
    uint16_t spectrumSize = adcConfig.sampleCount / 2;
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)MAX_FFT_SPECTRUM_SIZE);
    memcpy(sharedData.fftSpectrumData, vReal.data(), sharedData.fftSpectrumSize * sizeof(float));

    // DC bin (bin[0]) nullázása, ezt úgy sem használjuk
    if (sharedData.fftSpectrumSize > 0) {
        sharedData.fftSpectrumData[0] = 0.0f;
    }

    // // 7.2 Frekvenciatartomány erősítés: 600-9000 Hz között 2x, máshol 1x
    // // Valamiért a kütyüm 5.5-8.5kHz között gyengébben látja a jeleket
    // // Ezért itt - jobb ötlet hijján - kézzel erősítem ezt a tartományt.
    // applyFftGaussianWindow(         //
    //     sharedData.fftSpectrumData, // FFT bemeneti/kimeneti adatok
    //     sharedData.fftSpectrumSize, // FFT bin-ek száma
    //     sharedData.fftBinWidthHz,   // egy bin szélessége Hz-ben
    //     5500.0f,                    // alsó frekvencia
    //     8500.0f,                    // felső frekvencia
    //     // 8.0f                        // erősítés a csúcsnál
    //     2.0f // erősítés a csúcsnál
    // );

    // // 7.5. Alacsony frekvenciás zajszűrés (300Hz alatt csillapítás)
    // // Ez javítja a spektrum megjelenítés minőségét
    // const int attenuation_cutoff_bin = static_cast<int>(LOW_FREQ_ATTENUATION_THRESHOLD_HZ / currentBinWidthHz);
    // for (int i = 1; i < sharedData.fftSpectrumSize && i < attenuation_cutoff_bin; i++) {
    //     float oldValue = sharedData.fftSpectrumData[i];
    //     sharedData.fftSpectrumData[i] /= LOW_FREQ_ATTENUATION_FACTOR;
    //     ADPROC_DEBUG("AudioProc-c1: FFT bin[%d] alacsony frekvenciás (%.2fHz, %.2fHz) csillapítás: régi érték=%.1f, új érték=%.1f\n", i, i * currentBinWidthHz, currentBinWidthHz, oldValue,
    //      sharedData.fftSpectrumData[i]);
    // }

    // Az aktuális FFT bin szélesség beállítása a sharedData-ban
    sharedData.fftBinWidthHz = this->currentBinWidthHz;

    // 8. Domináns frekvencia keresése (FLOAT)
#ifdef __ADPROC_DEBUG
    start = micros();
#endif
    uint32_t maxIndex = 0;
    float maxValue = 0.0f;

    // Keresés float tömbben (DC bin nélkül, i=1-től indítva)
    for (int i = 1; i < sharedData.fftSpectrumSize; i++) {
        if (sharedData.fftSpectrumData[i] > maxValue) {
            maxValue = sharedData.fftSpectrumData[i];
            maxIndex = i;
        }
    }

    sharedData.dominantAmplitude = maxValue; // FLOAT → FLOAT (nincs típuskonverzió!)
    sharedData.dominantFrequency = (adcConfig.samplingRate / adcConfig.sampleCount) * maxIndex;

#ifdef __ADPROC_DEBUG
    uint32_t dominantTime = micros() - start;
    uint32_t totalSum = waitTime + preprocTime + fftTime + dominantTime;
    uint32_t totalTime = (micros() >= methodStartTime) ? (micros() - methodStartTime) : 0;

    // DEBUG kimenet: időmérések olvasható formában - minden 100. iterációban
    static uint32_t debugCounter = 0;
    if (++debugCounter >= 100) {
        ADPROC_DEBUG("AudioProc-c1: Total(micros()-methodStart)=%lu usec, Total(sumParts)=%lu usec, Wait=%lu usec, PreProc=%lu usec, FFT=%lu usec, DomSearch=%lu usec, maxIndex=%d, amp=%.1f\n", //
                     totalTime, totalSum, waitTime, preprocTime, fftTime, dominantTime, maxIndex, maxValue);
        debugCounter = 0;
    }
#endif

    return true;
}