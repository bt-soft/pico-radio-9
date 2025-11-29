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
#include "decoder_api.h"
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
// TONE_BLOCK_SIZE optimalizálás:
// - Fs = 7500 Hz (AudioProc decimált kimenet)
// - 128 sample → felbontás: 7500/128 = 58.6 Hz (jobb mint 35 Hz spacing!)
// - Blokk idő: 128/7500 = 17 ms (< 20 ms RTTY bit, OK!)
// - CW használ N=48-at, RTTY magnitude értékek kicsik 256-nál → 128 kompromisszum
#define TONE_BLOCK_SIZE 128
#define NOISE_ALPHA 0.15f
#define NOISE_DECAY_ALPHA 0.5f
#define NOISE_PEAK_RATIO 3.5f
// RTTY Q15 magnitude értékek: 174-505 (0.005-0.015 float)
// CW Q15 minThreshold: 1311 (0.04 float)
// A zajpadlót DRASZTIKUSAN csökkenteni kell, hogy ne emelje fel mindkét jelet azonos szintre!
#define MIN_NOISE_FLOOR 1.0f  // 1.0/1000 = 0.001 float (33 Q15) - SOKKAL kisebb!
#define MIN_DOMINANT_MAG 1.0f // 1.0/1000 = 0.001 float threshold

// Debug naplózási periódus — hány blokk után írjunk ki részletes RTTY diagnosztikát
#ifndef RTTY_DEBUG_PERIOD_BLOCKS
#define RTTY_DEBUG_PERIOD_BLOCKS 10 // Csökkentve gyorsabb debug outputhoz
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
void DecoderRTTY_C1::processSamples(const int16_t *samples, size_t count) {
    static int callCnt = 0;
    if (++callCnt == 1) {
        RTTY_DEBUG("RTTY-C1: processSamples() ELSŐ HÍVÁS - count=%zu\n", count);
    }
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
        // Q15 konverzió (mint CW): [-2.0, 2.0] → [-65536, 65536], de q15_t wrapping miatt működik
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
    static int callCnt = 0;
    if (++callCnt <= 3) {
        RTTY_DEBUG("RTTY-C1: processToneBlock() hívás #%d - count=%zu, toneBlockAccumulated=%zu, TONE_BLOCK_SIZE=%d\n", callCnt, count, toneBlockAccumulated,
                   TONE_BLOCK_SIZE);
    }

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
            // Első blokk jelzés (egyszer)
            static bool firstBlockDone = false;
            if (!firstBlockDone) {
                RTTY_DEBUG("RTTY-C1: ELSŐ TELJES TONE_BLOCK\n");
                firstBlockDone = true;
            }

            // Q15 fixpoint bufferek - direkt másolás, NINCS erősítés!
            int16_t bufMark[TONE_BLOCK_SIZE];
            int16_t bufSpace[TONE_BLOCK_SIZE];

            for (size_t i = 0; i < TONE_BLOCK_SIZE; ++i) {
                bufMark[i] = accum[i];
                bufSpace[i] = accum[i];
            }

            // Goertzel a mark-hoz a mark buf-en (Q15 fixpoint, nincs sqrt)
            static int dbgCtr = 0;
            bool doDbg = (++dbgCtr >= 100);

            for (auto &bin : markBins) {
                int32_t q1 = 0;
                int32_t q2 = 0;
                for (size_t n = 0; n < TONE_BLOCK_SIZE; ++n) {
                    // Q15 fixpontos szorzás: (coeff * q1) >> 15 (mint CW)
                    int32_t coeff_q1 = ((int32_t)bin.coeff * q1) >> 15;
                    int32_t q0 = coeff_q1 - q2 + (int32_t)bufMark[n];
                    q2 = q1;
                    q1 = q0;
                }
                // Gyors magnitúdó approximáció (nincs sqrt)
                // alpha-max-plus-beta-min algoritmus: mag ≈ max(|q1|,|q2|) + 0.5*min(|q1|,|q2|)
                // Ez sqrt(q1²+q2²) * 0.96 közelítés, ezért 2x skálázás a kompenzációhoz
                int32_t abs_q1 = (q1 < 0) ? -q1 : q1;
                int32_t abs_q2 = (q2 < 0) ? -q2 : q2;
                int32_t max_val = (abs_q1 > abs_q2) ? abs_q1 : abs_q2;
                int32_t min_val = (abs_q1 > abs_q2) ? abs_q2 : abs_q1;
                int32_t magnitude = (max_val + (min_val >> 1)) << 1; // 2x skálázás

                // Clamp Q15 tartományba
                if (magnitude > 32767)
                    magnitude = 32767;
                if (magnitude < -32768)
                    magnitude = -32768;
                bin.magnitude = (q15_t)magnitude;
            }
            if (doDbg) {
                RTTY_DEBUG("GOERTZEL: bufMark[0..4]=%d,%d,%d,%d,%d\n", bufMark[0], bufMark[1], bufMark[2], bufMark[3], bufMark[4]);
                RTTY_DEBUG("  markMag[0..2]=%d,%d,%d (%.1f,%.1f,%.1f Hz)\n", markBins[0].magnitude, markBins[1].magnitude, markBins[2].magnitude,
                           markBins[0].targetFreq, markBins[1].targetFreq, markBins[2].targetFreq);
                RTTY_DEBUG("  spaceMag[0..2]=%d,%d,%d (%.1f,%.1f,%.1f Hz)\n", spaceBins[0].magnitude, spaceBins[1].magnitude, spaceBins[2].magnitude,
                           spaceBins[0].targetFreq, spaceBins[1].targetFreq, spaceBins[2].targetFreq);
                dbgCtr = 0;
            }

            // Goertzel a space-hoz a space buf-en (Q15 fixpoint, nincs sqrt)
            for (auto &bin : spaceBins) {
                int32_t q1 = 0;
                int32_t q2 = 0;
                for (size_t n = 0; n < TONE_BLOCK_SIZE; ++n) {
                    // Q15 fixpontos szorzás: (coeff * q1) >> 15 (mint CW)
                    int32_t coeff_q1 = ((int32_t)bin.coeff * q1) >> 15;
                    int32_t q0 = coeff_q1 - q2 + (int32_t)bufSpace[n];
                    q2 = q1;
                    q1 = q0;
                }
                // Gyors magnitúdó approximáció (nincs sqrt)
                // alpha-max-plus-beta-min algoritmus: mag ≈ max(|q1|,|q2|) + 0.5*min(|q1|,|q2|)
                // Ez sqrt(q1²+q2²) * 0.96 közelítés, ezért 2x skálázás a kompenzációhoz
                int32_t abs_q1 = (q1 < 0) ? -q1 : q1;
                int32_t abs_q2 = (q2 < 0) ? -q2 : q2;
                int32_t max_val = (abs_q1 > abs_q2) ? abs_q1 : abs_q2;
                int32_t min_val = (abs_q1 > abs_q2) ? abs_q2 : abs_q1;
                int32_t magnitude = (max_val + (min_val >> 1)) << 1; // 2x skálázás
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

    // TISZTA GOERTZEL - NINCS AGC, NINCS NOISE FLOOR, NINCS ENVELOPE!
    // Csak a raw magnitude peak értékek összehasonlítása
    float markPeak = 0.0f;
    for (const auto &bin : markBins) {
        float mag_f = (float)bin.magnitude / Q15_SCALE;
        markPeak = std::max(markPeak, mag_f);
    }

    float spacePeak = 0.0f;
    for (const auto &bin : spaceBins) {
        float mag_f = (float)bin.magnitude / Q15_SCALE;
        spacePeak = std::max(spacePeak, mag_f);
    }

    // Egyszerű metric: log10(mark/space)
    // Pozitív -> MARK, Negatív -> SPACE
    float metric = log10f((markPeak + 0.001f) / (spacePeak + 0.001f));
    isMark = (metric > 0.0f);
    confidence = fabsf(metric);

    bool toneDetected = (markPeak > 0.001f) || (spacePeak > 0.001f);

    // Debug: periodikus kiírás (ritkán)
    static int debugCounter = 0;
    if (++debugCounter >= 100) {
        RTTY_DEBUG("RTTY: mark=%.3f space=%.3f met=%.3f -> %s\n", markPeak, spacePeak, metric, isMark ? "MARK" : "SPACE");
        debugCounter = 0;
    }

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
