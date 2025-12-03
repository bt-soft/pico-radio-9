/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderRTTY-c1.cpp                                                                                            *
 * Created Date: 2025.12.02.                                                                                           *
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
 * Last Modified: 2025.12.02, Tuesday  08:26:55                                                                        *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

// Working RTTY decoder (adapted from samples/)
// Test decoding: https://www.youtube.com/watch?v=-4UWeo-wSmA

#include <cmath>

#include "DecoderRTTY-c1.h"
#include "defines.h"

extern DecodedData decodedData;

// AGC kapcsoló - Finomhangolt paraméterekkel
// #define ENABLE_AGC 1

#define BIN_SPACING_HZ 35.0f
#define TONE_BLOCK_SIZE 64      // Kisebb blokk a gyorsabb reakcióért
#define NOISE_ALPHA 0.2f        // Mérsékelt adaptáció
#define NOISE_DECAY_ALPHA 0.6f  // Mérsékelt decay
#define NOISE_PEAK_RATIO 3.5f   // Eredeti arány
#define MIN_NOISE_FLOOR 20.0f   // Mérsékelt minimum
#define MIN_DOMINANT_MAG 250.0f // Mérsékelt küszöb (218 alatt nem volt jó)

// envelope tracking konstansok - alapértékek, de adaptív algoritmussal
static constexpr float ENVELOPE_ATTACK = 0.05f; // Alap attack (adaptívan növekszik nagy ugrásnál)
static constexpr float ENVELOPE_DECAY = 0.002f; // Lassú decay (stabilabb)

// Baudot LTRS (Letters) table - ITA2 standard
const char DecoderRTTY_C1::BAUDOT_LTRS_TABLE[32] = {
    '\0', 'E', '\n', 'A',  ' ', 'S', 'I', 'U', // 0-7
    '\r', 'D', 'R',  'J',  'N', 'F', 'C', 'K', // 8-15
    'T',  'Z', 'L',  'W',  'H', 'Y', 'P', 'Q', // 16-23
    'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0' // 24-31
};

// Baudot FIGS (Figures) table - ITA2 standard
const char DecoderRTTY_C1::BAUDOT_FIGS_TABLE[32] = {
    '\0', '3', '\n', '-',  ' ', '\'', '8', '7', // 0-7
    '\r', '$', '4',  '\a', ',', '!',  ':', '(', // 8-15
    '5',  '+', ')',  '2',  '#', '6',  '0', '1', // 16-23
    '9',  '?', '&',  '\0', '.', '/',  ';', '\0' // 24-31
};

/**
 * @brief RTTY dekóder konstruktor
 */
DecoderRTTY_C1::DecoderRTTY_C1()
    : currentState(IDLE), markFreq(0.0f), spaceFreq(0.0f), baudRate(45.45f), samplingRate(7500.0f), toneBlockAccumulated(0), lastToneIsMark(true),
      lastToneConfidence(0.0f), markNoiseFloor(0.0f), spaceNoiseFloor(0.0f), markEnvelope(0.0f), spaceEnvelope(0.0f), pllPhase(0.0f), pllFrequency(0.0f),
      pllDPhase(0.0f), pllAlpha(0.0f), pllBeta(0.0f), pllLocked(false), pllLockCounter(0), bitsReceived(0), currentByte(0), figsShift(false),
      lastDominantMagnitude(0.0f), lastOppositeMagnitude(0.0f) {
    initializeToneDetector();
    resetDecoder();
}

DecoderRTTY_C1::~DecoderRTTY_C1() {}

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

    decodedData.rttyMarkFreq = static_cast<uint16_t>(markFreq);
    decodedData.rttySpaceFreq = static_cast<uint16_t>(spaceFreq);
    decodedData.rttyBaudRate = baudRate;

    DEBUG("RTTY dekóder elindítva: Mark=%.1f Hz, Space=%.1f Hz, Shift=%.1f Hz, Baud=%.2f, Fs=%.0f Hz, ToneBlock=%u, BinSpacing=%.1f Hz\n", markFreq, spaceFreq,
          fabsf(markFreq - spaceFreq), baudRate, samplingRate, TONE_BLOCK_SIZE, BIN_SPACING_HZ);
    return true;
}

void DecoderRTTY_C1::stop() {
    decodedData.rttyMarkFreq = 0;
    decodedData.rttySpaceFreq = 0;
    decodedData.rttyBaudRate = 0.0f;
    DEBUG("RTTY dekóder leállítva.\n");
}

void DecoderRTTY_C1::processSamples(const int16_t *samples, size_t count) { processToneBlock(samples, count); }

void DecoderRTTY_C1::initializeToneDetector() {
    configureToneBins(markFreq, markBins);
    configureToneBins(spaceFreq, spaceBins);
    markNoiseFloor = 0.0f;
    spaceNoiseFloor = 0.0f;
    markEnvelope = 0.0f;
    spaceEnvelope = 0.0f;
    toneBlockAccumulated = 0;
    lastToneIsMark = true;
    lastToneConfidence = 0.0f;
    resetGoertzelState();

    // Initialize RMS pre-normalization state
    inputRmsAccum = 0.0f;
    inputRmsCount = 0;
    inputGain = 1.0f;
}

void DecoderRTTY_C1::configureToneBins(float centerFreq, std::array<GoertzelBin, BINS_PER_TONE> &bins) {
    int centerIndex = BINS_PER_TONE / 2;
    float centre = std::max(centerFreq, 0.0f);

    for (int i = 0; i < BINS_PER_TONE; ++i) {
        float offset = (i - centerIndex) * BIN_SPACING_HZ;
        float target = std::max(0.0f, centre + offset);
        float k = (samplingRate > 0.0f && TONE_BLOCK_SIZE > 0) ? (target * TONE_BLOCK_SIZE / samplingRate) : 0.0f;
        float omega = (TONE_BLOCK_SIZE > 0) ? ((2.0f * PI * k) / TONE_BLOCK_SIZE) : 0.0f;

        bins[i].targetFreq = target;
        bins[i].coeff = 2.0f * cosf(omega);
        bins[i].q1 = 0.0f;
        bins[i].q2 = 0.0f;
        bins[i].magnitude = 0.0f;
    }
}

void DecoderRTTY_C1::resetGoertzelState() {
    for (auto &bin : markBins) {
        bin.q1 = bin.q2 = 0.0f;
        bin.magnitude = 0.0f;
    }
    for (auto &bin : spaceBins) {
        bin.q1 = bin.q2 = 0.0f;
        bin.magnitude = 0.0f;
    }
}

// Optional features
#define ENABLE_INPUT_RMS_NORMALIZATION 1
#define RMS_WINDOW_SAMPLES 256 // számítsd át igény szerint (256 works well)
#define RMS_TARGET 12000.0f    // kívánt RMS szint (tuning) - emelve a jó log értékekhez

// Optional soft limiter
#define ENABLE_SOFT_LIMITER 1
#define SOFT_LIMIT_THRESHOLD 30000.0f

void DecoderRTTY_C1::processToneBlock(const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float sample = static_cast<float>(samples[i]);

#if ENABLE_INPUT_RMS_NORMALIZATION
        // Update running RMS (simple accumulator)
        inputRmsAccum += sample * sample;
        inputRmsCount++;
        if (inputRmsCount >= RMS_WINDOW_SAMPLES) {
            float mean = inputRmsAccum / static_cast<float>(inputRmsCount);
            float rms = sqrtf(mean);
            // Compute gain to bring RMS to target, but be gentle
            float targetGain = (rms > 1.0f) ? (RMS_TARGET / rms) : 1.0f;
            // Limit gain range
            targetGain = constrain(targetGain, 0.5f, 2.0f);
            // Smoothly update inputGain
            inputGain = inputGain * 0.9f + targetGain * 0.1f;
            inputRmsAccum = 0.0f;
            inputRmsCount = 0;
        }
        sample *= inputGain;
#endif

#if ENABLE_SOFT_LIMITER
        // Soft limiter: tanh-like curve to reduce spikes
        float absS = fabsf(sample);
        if (absS > SOFT_LIMIT_THRESHOLD) {
            float sign = (sample >= 0.0f) ? 1.0f : -1.0f;
            float exceeded = (absS - SOFT_LIMIT_THRESHOLD) / SOFT_LIMIT_THRESHOLD;
            float factor = 1.0f / (1.0f + exceeded);
            sample = sign * SOFT_LIMIT_THRESHOLD * factor;
        }
#endif

        // Goertzel számítás
        for (auto &bin : markBins) {
            float q0 = bin.coeff * bin.q1 - bin.q2 + sample;
            bin.q2 = bin.q1;
            bin.q1 = q0;
        }
        for (auto &bin : spaceBins) {
            float q0 = bin.coeff * bin.q1 - bin.q2 + sample;
            bin.q2 = bin.q1;
            bin.q1 = q0;
        }

        toneBlockAccumulated++;

        if (toneBlockAccumulated >= TONE_BLOCK_SIZE) {
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
                lastToneConfidence = confidence;
            }

            resetGoertzelState();
        }
    }
}

bool DecoderRTTY_C1::detectTone(bool &isMark, float &confidence) {
    // 1. Goertzel magnitude számítás
    float markPeak = 0.0f;
    float markSum = 0.0f;
    for (auto &bin : markBins) {
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        markSum += bin.magnitude;
        markPeak = std::max(markPeak, bin.magnitude);
    }

    float spacePeak = 0.0f;
    float spaceSum = 0.0f;
    for (auto &bin : spaceBins) {
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        spaceSum += bin.magnitude;
        spacePeak = std::max(spacePeak, bin.magnitude);
    }

    // 2. Zajpadló tracking
    float markNoiseSample = (BINS_PER_TONE > 1) ? ((markSum - markPeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;
    float spaceNoiseSample = (BINS_PER_TONE > 1) ? ((spaceSum - spacePeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;

    auto updateNoiseFloor = [](float currentFloor, float noiseSample, float peak) -> float {
        float sample = std::max(noiseSample, 0.0f);
        if (currentFloor == 0.0f) {
            return std::max(sample, MIN_NOISE_FLOOR);
        }

        bool strongSignal = (peak > currentFloor * NOISE_PEAK_RATIO) && (peak > (MIN_DOMINANT_MAG * 0.6f));
        if (strongSignal) {
            sample = std::min(sample, currentFloor * NOISE_PEAK_RATIO);
        }

        float alpha = (sample < currentFloor) ? NOISE_DECAY_ALPHA : NOISE_ALPHA;
        alpha = constrain(alpha, 0.01f, 0.95f);
        float blended = currentFloor * (1.0f - alpha) + sample * alpha;
        return std::max(blended, MIN_NOISE_FLOOR);
    };

    markNoiseFloor = updateNoiseFloor(markNoiseFloor, markNoiseSample, markPeak);
    spaceNoiseFloor = updateNoiseFloor(spaceNoiseFloor, spaceNoiseSample, spacePeak);

    // 3. Envelope tracking - adaptív alpha az amplitúdó változás alapján
    auto updateEnvelope = [](float currentEnv, float magnitude) -> float {
        if (currentEnv == 0.0f) {
            return magnitude;
        }
        // Nagyobb amplitúdó változás → gyorsabb követés
        float diff = fabsf(magnitude - currentEnv);
        float diffRatio = (currentEnv > 0.0f) ? (diff / currentEnv) : 1.0f;

        if (magnitude > currentEnv) {
            // Attack: gyors ha nagy a változás
            float alpha = ENVELOPE_ATTACK * (1.0f + diffRatio * 2.0f);
            alpha = constrain(alpha, ENVELOPE_ATTACK, 0.5f);
            return currentEnv * (1.0f - alpha) + magnitude * alpha;
        } else {
            // Decay: normál sebesség
            return currentEnv * (1.0f - ENVELOPE_DECAY) + magnitude * ENVELOPE_DECAY;
        }
    };

    markEnvelope = updateEnvelope(markEnvelope, markPeak);
    spaceEnvelope = updateEnvelope(spaceEnvelope, spacePeak);

    // 4. Clipping AGC
    float markClipped = std::min(markPeak, markEnvelope);
    float spaceClipped = std::min(spacePeak, spaceEnvelope);

    float noiseFloor = std::min(markNoiseFloor, spaceNoiseFloor);
    markClipped = std::max(markClipped, noiseFloor);
    spaceClipped = std::max(spaceClipped, noiseFloor);

    float markAgc = markClipped;
    float spaceAgc = spaceClipped;

#if ENABLE_AGC
    // Magasabb target és szűkebb gain tartomány a lágyabb normalizálásért
    constexpr float AGC_TARGET = 3500.0f; // Optimalizált target
    constexpr float MIN_GAIN = 0.7f;      // Ne vágjon túl sokat gyenge jelnél
    constexpr float MAX_GAIN = 2.5f;      // Mérsékelt maximális erősítés

    float markGain = (markEnvelope > 10.0f) ? (AGC_TARGET / markEnvelope) : 1.0f;
    float spaceGain = (spaceEnvelope > 10.0f) ? (AGC_TARGET / spaceEnvelope) : 1.0f;

    markGain = constrain(markGain, MIN_GAIN, MAX_GAIN);
    spaceGain = constrain(spaceGain, MIN_GAIN, MAX_GAIN);

    markAgc = markClipped * markGain;
    spaceAgc = spaceClipped * spaceGain;
#endif

    // 5. Log domain ATC
    float metric = log10f((markAgc + 1.0f) / (spaceAgc + 1.0f));
    isMark = (metric > 0.0f);
    confidence = fabsf(metric);

    float dominantMagnitude = std::max(markPeak, spacePeak);
    bool toneDetected = dominantMagnitude >= MIN_DOMINANT_MAG;

    // Debug
    static int debugCounter = 0;
    if (++debugCounter >= 20 && toneDetected) {
#if ENABLE_AGC
        DEBUG("RTTY: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, gain=%.2f/%.2f, AGC=%.0f/%.0f, metric=%.3f, %s\n", markPeak, markEnvelope, spacePeak,
              spaceEnvelope, markClipped, spaceClipped, markGain, spaceGain, markAgc, spaceAgc, metric, isMark ? "MARK" : "SPACE");
#else
        DEBUG("RTTY: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, metric=%.3f, %s (conf: %.2f)\n", markPeak, markEnvelope, spacePeak, spaceEnvelope, markClipped,
              spaceClipped, metric, isMark ? "MARK" : "SPACE", confidence);
#endif
        debugCounter = 0;
    }

    return toneDetected;
}

void DecoderRTTY_C1::initializePLL() {
    float omega_n = 2.0f * PI * PLL_BANDWIDTH * baudRate / samplingRate;
    pllAlpha = 2.0f * PLL_DAMPING * omega_n;
    pllBeta = omega_n * omega_n;

    pllFrequency = baudRate / samplingRate;
    pllDPhase = pllFrequency * TONE_BLOCK_SIZE;
    pllPhase = 0.0f;
    pllLocked = false;
    pllLockCounter = 0;

    DEBUG("PLL inicializálva: freq=%.6f, dPhase=%.6f, alpha=%.6f, beta=%.6f\n", pllFrequency, pllDPhase, pllAlpha, pllBeta);
}

void DecoderRTTY_C1::updatePLL(bool currentTone, bool &bitSample, bool &bitReady) {
    bitReady = false;

    bool edgeDetected = (currentTone != lastToneIsMark);

    if (edgeDetected && pllLockCounter > 5) {
        float phaseError = 0.0f;

        if (pllPhase < 0.5f) {
            phaseError = pllPhase;
        } else {
            phaseError = pllPhase - 1.0f;
        }

        pllDPhase += pllBeta * phaseError;
        pllPhase += pllAlpha * phaseError;

        if (!pllLocked) {
            pllLockCounter++;
            if (pllLockCounter > 10) {
                pllLocked = true;
                DEBUG("PLL locked!\n");
            }
        }
    }

    pllPhase += pllDPhase;

    if (pllPhase >= 1.0f) {
        pllPhase -= 1.0f;
        bitSample = currentTone;
        bitReady = true;
        pllLockCounter++;
    }

    float minFreq = (baudRate * 0.98f) / samplingRate * TONE_BLOCK_SIZE;
    float maxFreq = (baudRate * 1.02f) / samplingRate * TONE_BLOCK_SIZE;
    pllDPhase = constrain(pllDPhase, minFreq, maxFreq);
}

void DecoderRTTY_C1::processBit(bool bitValue) {
    bool isMark = bitValue;

    if (!pllLocked) {
        return;
    }

    switch (currentState) {
        case IDLE:
            if (!isMark) { // Start bit (Space)
                currentState = DATA_BITS;
                bitsReceived = 0;
                currentByte = 0;
            }
            break;

        case DATA_BITS:
            if (isMark) {
                currentByte |= (1 << bitsReceived);
            }
            bitsReceived++;

            if (bitsReceived >= 5) {
                currentState = STOP_BIT;
            }
            break;

        case STOP_BIT: {
            char decoded = decodeBaudotCharacter(currentByte);
            if (decoded != '\0') {
                if (!decodedData.textBuffer.put(decoded)) {
                    DEBUG("RTTY: textBuffer tele (karakter='%c')\n", decoded);
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
    return result;
}

void DecoderRTTY_C1::resetDecoder() {
    currentState = IDLE;
    bitsReceived = 0;
    currentByte = 0;
    figsShift = false;
    lastDominantMagnitude = 0.0f;
    lastOppositeMagnitude = 0.0f;
    markEnvelope = 0.0f;
    spaceEnvelope = 0.0f;
    initializeToneDetector();
    initializePLL();
}
