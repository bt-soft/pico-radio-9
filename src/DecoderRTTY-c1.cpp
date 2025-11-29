/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderRTTY-c1.cpp                                                                                            *
 * Created Date: 2025.11.07.                                                                                           *
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
 * Last Modified: 2025.11.29, Saturday  01:00:47                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

// teszt dekódolás: https://www.youtube.com/watch?v=-4UWeo-wSmA

#include <cmath>

#include "DecoderRTTY-c1.h"
#include "defines.h"

// RTTY működés debug engedélyezése, de csak ha __DEBUG definiált
#define __RTTY_DEBUG
#if defined(__DEBUG) && defined(__RTTY_DEBUG)
#define RTTY_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define RTTY_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

extern DecodedData decodedData;

// AGC kapcsoló
// #define ENABLE_AGC 1

#define BIN_SPACING_HZ 35.0f
#define TONE_BLOCK_SIZE 64 // Kisebb blokk a gyorsabb reakcióért
#define NOISE_ALPHA 0.15f
#define NOISE_DECAY_ALPHA 0.5f
#define NOISE_PEAK_RATIO 3.5f
#define MIN_NOISE_FLOOR 25.0f
#define MIN_DOMINANT_MAG 2.0f // Minimum magnitude a jel detektálásához

// Debug naplózási periódus — hány blokk után írjunk ki részletes RTTY diagnosztikát
#ifndef RTTY_DEBUG_PERIOD_BLOCKS
#define RTTY_DEBUG_PERIOD_BLOCKS 200
#endif

// envelope tracking konstansok
static constexpr float ENVELOPE_ATTACK = 0.05f; // Gyors felfutás
static constexpr float ENVELOPE_DECAY = 0.001f; // Lassú lecsengés

// Baudot LTRS (betűk) tábla - ITA2 szabvány
const char DecoderRTTY_C1::BAUDOT_LTRS_TABLE[32] = {
    '\0', 'E', '\n', 'A',  ' ', 'S', 'I', 'U', // 0-7
    '\r', 'D', 'R',  'J',  'N', 'F', 'C', 'K', // 8-15
    'T',  'Z', 'L',  'W',  'H', 'Y', 'P', 'Q', // 16-23
    'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0' // 24-31 (27=FIGS, 31=LTRS)
};

// Baudot FIGS (szimbólumok) tábla - ITA2 szabvány
const char DecoderRTTY_C1::BAUDOT_FIGS_TABLE[32] = {
    '\0', '3', '\n', '-',  ' ', '\'', '8', '7', // 0-7
    '\r', '$', '4',  '\a', ',', '!',  ':', '(', // 8-15
    '5',  '+', ')',  '2',  '#', '6',  '0', '1', // 16-23
    '9',  '?', '&',  '\0', '.', '/',  ';', '\0' // 24-31 (27=FIGS, 31=LTRS)
};

/**
 * @brief RTTY dekóder konstruktor - inicializálja az alapértelmezett értékeket
 */
DecoderRTTY_C1::DecoderRTTY_C1()
    : currentState(IDLE), markFreq(0.0f), spaceFreq(0.0f), baudRate(45.45f), samplingRate(7500.0f), toneBlockAccumulated(0), lastToneIsMark(true),
      lastToneConfidence_q15(0), markNoiseFloor_q15(0), spaceNoiseFloor_q15(0), markEnvelope_q15(0), spaceEnvelope_q15(0), pllPhase(0.0f), pllFrequency(0.0f),
      pllDPhase(0.0f), pllAlpha(0.0f), pllBeta(0.0f), pllLocked(false), pllLockCounter(0), bitsReceived(0), currentByte(0), figsShift(false),
      lastDominantMagnitude_q15(0), lastOppositeMagnitude_q15(0) {
    initializeToneDetector();
    resetDecoder();
}

/**
 * @brief RTTY dekóder destruktora
 */
DecoderRTTY_C1::~DecoderRTTY_C1() {}

/**
 * @brief RTTY dekóder indítása a konfigurációval
 * @param decoderConfig Dekóder konfiguráció
 */
bool DecoderRTTY_C1::start(const DecoderConfig &decoderConfig) {
    markFreq = static_cast<float>(decoderConfig.rttyMarkFreqHz);
    float shiftFreq = static_cast<float>(decoderConfig.rttyShiftFreqHz);

    if (shiftFreq > 0.0f) {
        if (decoderConfig.rttyMarkFreqHz >= decoderConfig.rttyShiftFreqHz) {
            spaceFreq = markFreq - shiftFreq;
        } else {
            spaceFreq = markFreq + shiftFreq;
        }
    } else {
        spaceFreq = markFreq;
    }

    baudRate = (decoderConfig.rttyBaud > 0) ? static_cast<float>(decoderConfig.rttyBaud) : 45.45f;
    samplingRate = (decoderConfig.samplingRate > 0) ? static_cast<float>(decoderConfig.samplingRate) : 7500.0f;

    initializeToneDetector();
    initializePLL();
    resetDecoder();

    // Hann ablak előgenerálása a Goertzel blokkokhoz (ha engedélyezett)
    if (useWindow_) {
        windowApplier.build(TONE_BLOCK_SIZE, WindowType::Hann, true);
    }

    // Ha engedélyezve: inicializáljuk a mark/space bandpass-szűrőket
    if (useBandpass_) {
        bandpassMarkBWHz_ = std::max(200.0f, fabsf(markFreq - spaceFreq) * 1.2f);
        bandpassSpaceBWHz_ = std::max(200.0f, fabsf(markFreq - spaceFreq) * 1.2f);
        markBandpassFilter_.init(samplingRate, markFreq, bandpassMarkBWHz_);
        spaceBandpassFilter_.init(samplingRate, spaceFreq, bandpassSpaceBWHz_);
    }

    // Publikáljuk a paramétereket a DecodedData-ba
    decodedData.rttyMarkFreq = static_cast<uint16_t>(markFreq);
    decodedData.rttySpaceFreq = static_cast<uint16_t>(spaceFreq);
    decodedData.rttyBaudRate = baudRate;

    RTTY_DEBUG("RTTY-C1: RTTY dekóder elindítva: Mark=%.1f Hz, Space=%.1f Hz, Shift=%.1f Hz, Baud=%.2f, Fs=%.0f Hz, ToneBlock=%u, BinSpacing=%.1f Hz\n",
               markFreq, spaceFreq, fabsf(markFreq - spaceFreq), baudRate, samplingRate, TONE_BLOCK_SIZE, BIN_SPACING_HZ);
    return true;
}

void DecoderRTTY_C1::stop() {
    decodedData.rttyMarkFreq = 0;
    decodedData.rttySpaceFreq = 0;
    decodedData.rttyBaudRate = 0.0f;
    RTTY_DEBUG("RTTY-C1: RTTY dekóder leállítva.\n");
}

/**
 * @brief Minták feldolgozása
 * @param samples Pointer a mintákhoz
 * @param count A minták száma
 */
void DecoderRTTY_C1::processSamples(const int16_t *samples, size_t count) { //
    processToneBlock(samples, count);
}

/**
 * @brief Tone detector inicializálása
 */
void DecoderRTTY_C1::initializeToneDetector() {
    configureToneBins(markFreq, markBins);
    configureToneBins(spaceFreq, spaceBins);
    markNoiseFloor_q15 = 0;
    spaceNoiseFloor_q15 = 0;
    markEnvelope_q15 = 0;
    spaceEnvelope_q15 = 0;
    toneBlockAccumulated = 0;
    lastToneIsMark = true;
    lastToneConfidence_q15 = 0;
    resetGoertzelState();
}

/**
 * @brief Konfigurálja a Goertzel bin-eket egy adott középfrekvenciára (Q15)
 */
void DecoderRTTY_C1::configureToneBins(float centerFreq, std::array<GoertzelBin, BINS_PER_TONE> &bins) {
    int centerIndex = BINS_PER_TONE / 2;
    float centre = std::max(centerFreq, 0.0f);

    for (int i = 0; i < BINS_PER_TONE; ++i) {
        float offset = (i - centerIndex) * BIN_SPACING_HZ;
        float target = std::max(0.0f, centre + offset);
        float k = (samplingRate > 0.0f && TONE_BLOCK_SIZE > 0) ? (target * TONE_BLOCK_SIZE / samplingRate) : 0.0f;
        float omega = (TONE_BLOCK_SIZE > 0) ? ((2.0f * PI * k) / TONE_BLOCK_SIZE) : 0.0f;

        bins[i].targetFreq = target;
        // Q15 konverzió: 2*cos(omega) → Q15
        float coeff_float = 2.0f * cosf(omega);
        bins[i].coeff = (q15_t)(coeff_float * Q15_MAX_AS_FLOAT);
        bins[i].q1 = 0;
        bins[i].q2 = 0;
        bins[i].magnitude = 0;
    }
}

/**
 * @brief Goertzel állapot visszaállítása
 */
void DecoderRTTY_C1::resetGoertzelState() {
    for (auto &bin : markBins) {
        bin.q1 = bin.q2 = 0;
        bin.magnitude = 0;
    }
    for (auto &bin : spaceBins) {
        bin.q1 = bin.q2 = 0;
        bin.magnitude = 0;
    }
}

/**
 * @brief Tone blokkok feldolgozása és PLL frissítés
 */
void DecoderRTTY_C1::processToneBlock(const int16_t *samples, size_t count) {
    // Bejövő mintákat TONE_BLOCK_SIZE blokkokban dolgozunk fel.
    // Minden teljes blokkot float típusúvá alakítunk, opcionálisan alkalmazzuk a
    // előre kiszámolt ablakot, majd bin-enként lefuttatjuk a Goertzel algoritmust
    // a windowolt float pufferre.
    size_t idx = 0;
    while (idx < count) {
        size_t take = std::min((size_t)TONE_BLOCK_SIZE - toneBlockAccumulated, count - idx);

        // A bejövő minták másolása egy statikus összegző pufferbe
        static int16_t accum[TONE_BLOCK_SIZE];
        for (size_t i = 0; i < take; ++i) {
            accum[toneBlockAccumulated + i] = samples[idx + i];
        }
        toneBlockAccumulated += take;
        idx += take;

        if (toneBlockAccumulated >= TONE_BLOCK_SIZE) {
            // Float puffer előkészítése és ablak alkalmazása, ha engedélyezett
            float buf[TONE_BLOCK_SIZE];
            if (useWindow_) {
                windowApplier.apply(accum, buf, TONE_BLOCK_SIZE);
            } else {
                for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i)
                    buf[i] = static_cast<float>(accum[i]);
            }

            // Két külön bandpass szűrőt használunk, alkalmazzuk őket és külön Goertzel-t futtatunk
            static int16_t markFiltered[TONE_BLOCK_SIZE];
            static int16_t spaceFiltered[TONE_BLOCK_SIZE];

            if (useBandpass_ && markBandpassFilter_.isInitialized()) {
                markBandpassFilter_.processInPlace(accum, markFiltered, TONE_BLOCK_SIZE);
            }
            if (useBandpass_ && spaceBandpassFilter_.isInitialized()) {
                spaceBandpassFilter_.processInPlace(accum, spaceFiltered, TONE_BLOCK_SIZE);
            }

            // Float bufferek a windowolt adatokhoz
            float bufMark[TONE_BLOCK_SIZE];
            float bufSpace[TONE_BLOCK_SIZE];

            if (useBandpass_ && markBandpassFilter_.isInitialized()) {
                if (useWindow_) {
                    windowApplier.apply(markFiltered, bufMark, TONE_BLOCK_SIZE);
                } else {
                    for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i)
                        bufMark[i] = static_cast<float>(markFiltered[i]);
                }
            } else {
                // ha nincs mark szűrő, használjuk az eredeti blokkot
                if (useWindow_) {
                    windowApplier.apply(accum, bufMark, TONE_BLOCK_SIZE);
                } else {
                    for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i)
                        bufMark[i] = static_cast<float>(accum[i]);
                }
            }

            if (useBandpass_ && spaceBandpassFilter_.isInitialized()) {
                if (useWindow_) {
                    windowApplier.apply(spaceFiltered, bufSpace, TONE_BLOCK_SIZE);
                } else {
                    for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i)
                        bufSpace[i] = static_cast<float>(spaceFiltered[i]);
                }
            } else {
                if (useWindow_) {
                    windowApplier.apply(accum, bufSpace, TONE_BLOCK_SIZE);
                } else {
                    for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i)
                        bufSpace[i] = static_cast<float>(accum[i]);
                }
            }

            // Goertzel a mark-hoz a mark buf-en (Q15 fixpoint, nincs sqrt)
            for (auto &bin : markBins) {
                int32_t q1 = 0;
                int32_t q2 = 0;
                for (size_t n = 0; n < TONE_BLOCK_SIZE; ++n) {
                    // Q15 fixpontos szorzás: (coeff * q1) >> 15
                    int32_t coeff_q1 = ((int32_t)bin.coeff * q1) >> 15;
                    int32_t q0 = coeff_q1 - q2 + (int32_t)bufMark[n];
                    q2 = q1;
                    q1 = q0;
                }
                // Gyors magnitúdó approximáció (nincs sqrt)
                int32_t abs_q1 = (q1 < 0) ? -q1 : q1;
                int32_t abs_q2 = (q2 < 0) ? -q2 : q2;
                int32_t max_val = (abs_q1 > abs_q2) ? abs_q1 : abs_q2;
                int32_t min_val = (abs_q1 > abs_q2) ? abs_q2 : abs_q1;
                int32_t magnitude = max_val + (min_val >> 1); // max + 0.5*min
                // Clamp Q15 tartományba
                if (magnitude > 32767)
                    magnitude = 32767;
                if (magnitude < -32768)
                    magnitude = -32768;
                bin.magnitude = (q15_t)magnitude;
            }

            // Goertzel a space-hoz a space buf-en (Q15 fixpoint, nincs sqrt)
            for (auto &bin : spaceBins) {
                int32_t q1 = 0;
                int32_t q2 = 0;
                for (size_t n = 0; n < TONE_BLOCK_SIZE; ++n) {
                    int32_t coeff_q1 = ((int32_t)bin.coeff * q1) >> 15;
                    int32_t q0 = coeff_q1 - q2 + (int32_t)bufSpace[n];
                    q2 = q1;
                    q1 = q0;
                }
                // Gyors magnitúdó approximáció
                int32_t abs_q1 = (q1 < 0) ? -q1 : q1;
                int32_t abs_q2 = (q2 < 0) ? -q2 : q2;
                int32_t max_val = (abs_q1 > abs_q2) ? abs_q1 : abs_q2;
                int32_t min_val = (abs_q1 > abs_q2) ? abs_q2 : abs_q1;
                int32_t magnitude = max_val + (min_val >> 1);
                if (magnitude > 32767)
                    magnitude = 32767;
                if (magnitude < -32768)
                    magnitude = -32768;
                bin.magnitude = (q15_t)magnitude;
            }

            // A magnitúdók kiszámítása után: tónus detektálása és PLL előreléptetése
            toneBlockAccumulated = 0;

            bool isMark = false;
            float confidence = 0.0f;

            if (detectTone(isMark, confidence)) {
                bool bitSample = false;
                bool bitReady = false;
                updatePLL(isMark, bitSample, bitReady);
                if (bitReady) {
                    processBit(bitSample);
                }
                lastToneIsMark = isMark;
                lastToneConfidence_q15 = (q15_t)(confidence * Q15_MAX_AS_FLOAT);
            }

            resetGoertzelState();
        }
    }
}

/**
 * @brief Tone detektálás a Goertzel eredményekből
 *
 * Implementálja a log domain ATC (Automatikus Küszöbvezérlés) metódusát:
 * - Envelope követés (gyors felfutás, lassú lecsengés)
 * - Zajpadló követés
 * - Clipping AGC (a csúcsok korlátozása)
 * - Log domain döntés (v3 metódus - zajos körülményekhez optimalizált)
 */
bool DecoderRTTY_C1::detectTone(bool &isMark, float &confidence) {

    // 1. Q15 magnitúdók konvertálása float-ra feldolgozáshoz
    // (A zajpadló/envelope tracking marad float - kevésbé kritikus)
    float markPeak = 0.0f;
    float markSum = 0.0f;
    for (const auto &bin : markBins) {
        float mag_f = (float)bin.magnitude / Q15_SCALE; // Q15 → float normalizálás
        markSum += mag_f;
        markPeak = std::max(markPeak, mag_f);
    }

    float spacePeak = 0.0f;
    float spaceSum = 0.0f;
    for (const auto &bin : spaceBins) {
        float mag_f = (float)bin.magnitude / Q15_SCALE;
        spaceSum += mag_f;
        spacePeak = std::max(spacePeak, mag_f);
    }

    // 2. Zajpadló tracking (noise floor módszer)
    float markNoiseSample = (BINS_PER_TONE > 1) ? ((markSum - markPeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;
    float spaceNoiseSample = (BINS_PER_TONE > 1) ? ((spaceSum - spacePeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;

    auto updateNoiseFloor_q15 = [](q15_t currentFloor_q15, float noiseSample, float peak) -> q15_t {
        float currentFloor = (float)currentFloor_q15 / Q15_SCALE;
        float sample = std::max(noiseSample, 0.0f);
        if (currentFloor == 0.0f) {
            float result = std::max(sample, MIN_NOISE_FLOOR / 1000.0f); // MIN_NOISE_FLOOR skálázva
            return (q15_t)(result * Q15_SCALE);
        }

        float minDomScaled = MIN_DOMINANT_MAG / 1000.0f;
        bool strongSignal = (peak > currentFloor * NOISE_PEAK_RATIO) && (peak > (minDomScaled * 0.6f));
        if (strongSignal) {
            sample = std::min(sample, currentFloor * NOISE_PEAK_RATIO);
        }

        float alpha = (sample < currentFloor) ? NOISE_DECAY_ALPHA : NOISE_ALPHA;
        alpha = constrain(alpha, 0.01f, 0.95f);
        float blended = currentFloor * (1.0f - alpha) + sample * alpha;
        float result = std::max(blended, MIN_NOISE_FLOOR / 1000.0f);
        return (q15_t)(result * Q15_SCALE);
    };

    markNoiseFloor_q15 = updateNoiseFloor_q15(markNoiseFloor_q15, markNoiseSample, markPeak);
    spaceNoiseFloor_q15 = updateNoiseFloor_q15(spaceNoiseFloor_q15, spaceNoiseSample, spacePeak);

    // 3. Envelope tracking (decayavg módszer) - Q15
    auto updateEnvelope_q15 = [](q15_t currentEnv_q15, float magnitude) -> q15_t {
        float currentEnv = (float)currentEnv_q15 / Q15_SCALE;
        if (currentEnv == 0.0f) {
            return (q15_t)(magnitude * Q15_SCALE);
        }
        float alpha = (magnitude > currentEnv) ? ENVELOPE_ATTACK : ENVELOPE_DECAY;
        float result = currentEnv * (1.0f - alpha) + magnitude * alpha;
        return (q15_t)(result * Q15_SCALE);
    };

    markEnvelope_q15 = updateEnvelope_q15(markEnvelope_q15, markPeak);
    spaceEnvelope_q15 = updateEnvelope_q15(spaceEnvelope_q15, spacePeak);

    float markEnvelope = (float)markEnvelope_q15 / Q15_SCALE;
    float spaceEnvelope = (float)spaceEnvelope_q15 / Q15_SCALE;
    float markNoiseFloor = (float)markNoiseFloor_q15 / Q15_SCALE;
    float spaceNoiseFloor = (float)spaceNoiseFloor_q15 / Q15_SCALE;

    // 4. Clipping AGC
    // Ha a jel meghaladja az envelope-ot, clipeljük
    float markClipped = std::min(markPeak, markEnvelope);
    float spaceClipped = std::min(spacePeak, spaceEnvelope);

    // Ha a clippelt jel a zajpadló alá esne, emelj fel zajpadlóra
    float noiseFloor = std::min(markNoiseFloor, spaceNoiseFloor);
    markClipped = std::max(markClipped, noiseFloor);
    spaceClipped = std::max(spaceClipped, noiseFloor);

    // 4b. Automatikus erősítés (AGC gain)
    float markAgc = markClipped;
    float spaceAgc = spaceClipped;

#if ENABLE_AGC
    constexpr float AGC_TARGET = 1500.0f;

    // Számítsuk ki a szükséges gain-t az envelope alapján
    float markGain = (markEnvelope > 0.01f) ? (AGC_TARGET / markEnvelope) : 1.0f;
    float spaceGain = (spaceEnvelope > 0.01f) ? (AGC_TARGET / spaceEnvelope) : 1.0f;

    // Limitáljuk a gain-t, hogy ne legyen túl nagy
    markGain = constrain(markGain, 0.5f, 10.0f);
    spaceGain = constrain(spaceGain, 0.5f, 10.0f);

    // Erősítés/csillapítás a jeleken
    markAgc = markClipped * markGain;
    spaceAgc = spaceClipped * spaceGain;
#endif

    // 5. ATC - LEGJOBB MÓDSZER zajos körülmények között!
    // metric = log10((mark + 1) / (space + 1))
    //      Pozitív -> mark dominál
    //      Negatív -> space dominál
    float metric = log10f((markAgc + 1.0f) / (spaceAgc + 1.0f));
    isMark = (metric > 0.0f);
    confidence = fabsf(metric); // Abszolút érték -> azt mutatja, mennyire egyértelmű a döntés a mark/space között.
                                // Ha közel 0, akkor bizonytalan a dekóder, ha nagy, akkor biztos a döntés.

    float minDomScaled = MIN_DOMINANT_MAG / 1000.0f;
    float dominantMagnitude = std::max(markPeak, spacePeak);
    bool toneDetected = dominantMagnitude >= minDomScaled;

    // Debug: periodikus kiírás
    static int debugCounter = 0;
    // Naplózzunk minden 20. blokkban (korábban csak toneDetected esetén), hogy lássuk ha a jel túl gyenge
    if (++debugCounter >= RTTY_DEBUG_PERIOD_BLOCKS) {

#if ENABLE_AGC
        RTTY_DEBUG("RTTY-C1: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, gain=%.2f/%.2f, AGC=%.0f/%.0f, metric=%.3f, %s\n", markPeak, markEnvelope, spacePeak,
                   spaceEnvelope, markClipped, spaceClipped, markGain, spaceGain, markAgc, spaceAgc, metric, isMark ? "MARK" : "SPACE");
#else
        RTTY_DEBUG("RTTY-C1: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, metric=%.3f, %s (confidence: %.2f)\n", markPeak, markEnvelope, spacePeak,
                   spaceEnvelope, markClipped, spaceClipped, metric, isMark ? "MARK" : "SPACE", confidence);

        // Részletesebb diagnosztika: keressük meg a legjobb bin frekvenciákat mindkét oldalon
        float bestMarkFreq = 0.0f;
        float bestMarkMag = 0.0f;
        for (size_t i = 0; i < markBins.size(); ++i) {
            if (markBins[i].magnitude > bestMarkMag) {
                bestMarkMag = markBins[i].magnitude;
                bestMarkFreq = markBins[i].targetFreq;
            }
        }

        float bestSpaceFreq = 0.0f;
        float bestSpaceMag = 0.0f;
        for (size_t i = 0; i < spaceBins.size(); ++i) {
            if (spaceBins[i].magnitude > bestSpaceMag) {
                bestSpaceMag = spaceBins[i].magnitude;
                bestSpaceFreq = spaceBins[i].targetFreq;
            }
        }

        RTTY_DEBUG("RTTY-C1: bestMark=%.1fHz(%.1f) bestSpace=%.1fHz(%.1f)\n", bestMarkFreq, bestMarkMag, bestSpaceFreq, bestSpaceMag);

        // Opció: részletes bin lista (kis BINS_PER_TONE érték esetén hasznos)
        for (size_t i = 0; i < markBins.size(); ++i) {
            RTTY_DEBUG("  MarkBin[%02u] f=%.1fHz mag=%.1f\n", (unsigned)i, markBins[i].targetFreq, markBins[i].magnitude);
        }
        for (size_t i = 0; i < spaceBins.size(); ++i) {
            RTTY_DEBUG("  SpaceBin[%02u] f=%.1fHz mag=%.1f\n", (unsigned)i, spaceBins[i].targetFreq, spaceBins[i].magnitude);
        }
#endif
        debugCounter = 0;
    }

    // Validáció: csak akkor dekódoljunk, ha van értelmes jel
    return toneDetected;
}

/**
 * @brief PLL inicializálása
 */
void DecoderRTTY_C1::initializePLL() {
    // PLL loop filter koefficiensek számítása
    float omega_n = 2.0f * PI * PLL_BANDWIDTH * baudRate / samplingRate;
    pllAlpha = 2.0f * PLL_DAMPING * omega_n;
    pllBeta = omega_n * omega_n;

    pllFrequency = baudRate / samplingRate;     // Normalizált frekvencia (0-1)
    pllDPhase = pllFrequency * TONE_BLOCK_SIZE; // Fázis növekmény BLOKKÖNKÉNT!
    pllPhase = 0.0f;
    pllLocked = false;
    pllLockCounter = 0;

    RTTY_DEBUG("PLL inicializálva: freq=%.6f, dPhase=%.6f, alpha=%.6f, beta=%.6f\n", pllFrequency, pllDPhase, pllAlpha, pllBeta);
}

/**
 * @brief PLL frissítése és bit mintavételezés
 */
void DecoderRTTY_C1::updatePLL(bool currentTone, bool &bitSample, bool &bitReady) {
    bitReady = false;

    // Él detektálás
    bool edgeDetected = (currentTone != lastToneIsMark);

    if (edgeDetected && pllLockCounter > 5) {
        // Fázis hiba: mennyire van az él eltolva a bit közepétől
        // Ha fázis = 0.5, akkor pont a bit közepén vagyunk (ideális)
        // Ha él van, akkor fázis kellene ~0 vagy ~1 legyen
        float phaseError = 0.0f;

        if (pllPhase < 0.5f) {
            phaseError = pllPhase; // Túl korán jött az él
        } else {
            phaseError = pllPhase - 1.0f; // Túl későn jött
        }

        // PLL loop filter
        pllDPhase += pllBeta * phaseError;
        pllPhase += pllAlpha * phaseError;

        if (!pllLocked) {
            pllLockCounter++;
            if (pllLockCounter > 10) {
                pllLocked = true;
                RTTY_DEBUG("PLL locked!\n");
            }
        }
    }

    // Fázis előreléptetés
    pllPhase += pllDPhase;

    // Bit mintavételezés a fázis tetőpontján
    if (pllPhase >= 1.0f) {
        pllPhase -= 1.0f;
        bitSample = currentTone;
        bitReady = true;
        pllLockCounter++;
    }

    // Frekvencia korlátozás (blokkos egységben!)
    float minFreq = (baudRate * 0.98f) / samplingRate * TONE_BLOCK_SIZE;
    float maxFreq = (baudRate * 1.02f) / samplingRate * TONE_BLOCK_SIZE;
    pllDPhase = constrain(pllDPhase, minFreq, maxFreq);
}

/**
 * @brief Bit feldolgozása az állapotgéppel
 */
void DecoderRTTY_C1::processBit(bool bitValue) {
    bool isMark = bitValue;

    if (!pllLocked) {
        // PLL még nincs lockolva, várunk
        return;
    }

    // RTTY_DEBUG("PLL Bit=%s, phase=%.3f, freq=%.6f\n", isMark ? "Mark" : "Space", pllPhase, (pllDPhase / TONE_BLOCK_SIZE) * samplingRate);

    switch (currentState) {
        case IDLE:
            if (!isMark) { // Start bit (Space)
                currentState = DATA_BITS;
                bitsReceived = 0;
                currentByte = 0;
                // RTTY_DEBUG("RTTY-C1: Start bit detektálva\n");
            }
            break;

        case DATA_BITS:
            if (isMark) {
                currentByte |= (1 << bitsReceived);
            }
            bitsReceived++;
            // RTTY_DEBUG("RTTY-C1: Data bit %d = %d, byte=0x%02X\n", bitsReceived, isMark ? 1 : 0, currentByte);

            if (bitsReceived >= 5) {
                currentState = STOP_BIT;
            }
            break;

        case STOP_BIT: {
            // Stop bit kellene Mark legyen, de toleráljuk ha Space
            // RTTY_DEBUG("RTTY-C1: Stop bit = %s\n", isMark ? "Mark (OK)" : "Space (tolerálva)");

            char decoded = decodeBaudotCharacter(currentByte);
            if (decoded != '\0') {
                if (!decodedData.textBuffer.put(decoded)) {
                    RTTY_DEBUG("RTTY-C1: textBuffer tele (karakter='%c')\n", decoded);
                }
            }

            currentState = IDLE;
            bitsReceived = 0;
            currentByte = 0;
        } break;

        default:
            break;
    }
}

/**
 * @brief Baudot karakter dekódolása
 */
char DecoderRTTY_C1::decodeBaudotCharacter(uint8_t baudotCode) {
    if (baudotCode >= 32) {
        return '\0';
    }

    if (baudotCode == 0x1B) { // FIGS
        figsShift = true;
        return '\0';
    }

    if (baudotCode == 0x1F) { // LTRS
        figsShift = false;
        return '\0';
    }

    char result = figsShift ? BAUDOT_FIGS_TABLE[baudotCode] : BAUDOT_LTRS_TABLE[baudotCode];
    if (result != '\0') {
        // RTTY_DEBUG("RTTY-C1: Dekódolt karakter: '%c' (code=0x%02X, shift=%s)\n", result, baudotCode, figsShift ? "FIGS" : "LTRS");
    }
    return result;
}

/**
 * @brief Dekóder állapotának visszaállítása
 */
void DecoderRTTY_C1::resetDecoder() {
    currentState = IDLE;
    bitsReceived = 0;
    currentByte = 0;
    figsShift = false;
    lastDominantMagnitude_q15 = 0;
    lastOppositeMagnitude_q15 = 0;
    // markEnvelope_q15 és spaceEnvelope_q15 resetelődik initializeToneDetector()-ban
    initializeToneDetector();
    initializePLL();
}
