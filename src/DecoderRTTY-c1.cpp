/**
 * @file RttyDecoder-c1.cpp
 * @brief RTTY dekóder implementációja Core-1 számára
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */

// test decoding: https://www.youtube.com/watch?v=-4UWeo-wSmA

#include <cmath>

#include "DecoderRTTY-c1.h"
#include "defines.h"

// RTTY működés debug engedélyezése de csak DEBUG módban
// #define __RTTY_DEBUG
#if defined(__DEBUG) && defined(__RTTY_DEBUG)
#define RTTY_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define RTTY_DEBUG(fmt, ...) // Empty macro if __DEBUG is not defined
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
#define MIN_DOMINANT_MAG 380.0f // Minimum magnitude a tonális jel detektálásához !! Fontos !! (400 volt)

// envelope tracking konstansok
static constexpr float ENVELOPE_ATTACK = 0.05f; // Gyors felfutás
static constexpr float ENVELOPE_DECAY = 0.001f; // Lassú lecsengés

// Baudot LTRS (Letters) table - ITA2 standard
const char DecoderRTTY_C1::BAUDOT_LTRS_TABLE[32] = {
    '\0', 'E', '\n', 'A',  ' ', 'S', 'I', 'U', // 0-7
    '\r', 'D', 'R',  'J',  'N', 'F', 'C', 'K', // 8-15
    'T',  'Z', 'L',  'W',  'H', 'Y', 'P', 'Q', // 16-23
    'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0' // 24-31 (27=FIGS, 31=LTRS)
};

// Baudot FIGS (Figures) table - ITA2 standard
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
    : currentState(IDLE), markFreq(0.0f), spaceFreq(0.0f), baudRate(45.45f), samplingRate(7500.0f), toneBlockAccumulated(0), lastToneIsMark(true), lastToneConfidence(0.0f), markNoiseFloor(0.0f), spaceNoiseFloor(0.0f),
      markEnvelope(0.0f), spaceEnvelope(0.0f), pllPhase(0.0f), pllFrequency(0.0f), pllDPhase(0.0f), pllAlpha(0.0f), pllBeta(0.0f), pllLocked(false), pllLockCounter(0), bitsReceived(0), currentByte(0), figsShift(false),
      lastDominantMagnitude(0.0f), lastOppositeMagnitude(0.0f) {
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

    // Publikáljuk a paramétereket a DecodedData-ba
    decodedData.rttyMarkFreq = static_cast<uint16_t>(markFreq);
    decodedData.rttySpaceFreq = static_cast<uint16_t>(spaceFreq);
    decodedData.rttyBaudRate = static_cast<uint16_t>(baudRate);

    RTTY_DEBUG("RTTY-C1: RTTY dekóder elindítva: Mark=%.1f Hz, Space=%.1f Hz, Shift=%.1f Hz, Baud=%.2f, Fs=%.0f Hz, ToneBlock=%u, BinSpacing=%.1f Hz\n", markFreq, spaceFreq, fabsf(markFreq - spaceFreq), baudRate,
               samplingRate, TONE_BLOCK_SIZE, BIN_SPACING_HZ);
    return true;
}

void DecoderRTTY_C1::stop() {
    decodedData.rttyMarkFreq = 0;
    decodedData.rttySpaceFreq = 0;
    decodedData.rttyBaudRate = 0;
    RTTY_DEBUG("RTTY-C1: RTTY dekóder leállítva.\n");
}

void DecoderRTTY_C1::processSamples(const int16_t *samples, size_t count) { processToneBlock(samples, count); }

/**
 * @brief Tone detector inicializálása
 */
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
}

/**
 * @brief Konfigurálja a Goertzel bin-eket egy adott középfrekvenciára
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
        bins[i].coeff = 2.0f * cosf(omega);
        bins[i].q1 = 0.0f;
        bins[i].q2 = 0.0f;
        bins[i].magnitude = 0.0f;
    }
}

/**
 * @brief Goertzel állapot visszaállítása
 */
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

/**
 * @brief Tone blokkok feldolgozása és PLL frissítés
 */
void DecoderRTTY_C1::processToneBlock(const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float sample = static_cast<float>(samples[i]);

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
            float confidence = 0.0f; // nem használt változó

            if (detectTone(isMark, confidence)) {
                // Tone detektálva, frissítsük a PLL-t
                bool bitSample = false;
                bool bitReady = false;

                updatePLL(isMark, bitSample, bitReady);

                if (bitReady) {
                    processBit(bitSample);
                }

                lastToneIsMark = isMark;
                lastToneConfidence = confidence; // nem használt változó
            }

            resetGoertzelState();
        }
    }
}

/**
 * @brief Tone detektálás a Goertzel eredményekből
 *
 * Implementálja a log domain ATC (Automatic Threshold Control) metódusát:
 * - Envelope tracking (gyors felfutás, lassú lecsengés)
 * - Noise floor tracking
 * - Clipping AGC (limitálja a csúcsokat)
 * - Log domain decision (v3 metódus - a legjobb zajos körülmények között)
 */
bool DecoderRTTY_C1::detectTone(bool &isMark, float &confidence) {

    // 1. Goertzel magnitude számítás

    // Mark hang amplitudók
    float markPeak = 0.0f;
    float markSum = 0.0f;
    for (auto &bin : markBins) {
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        markSum += bin.magnitude;
        markPeak = std::max(markPeak, bin.magnitude);
    }

    // Space hang amplitudók
    float spacePeak = 0.0f;
    float spaceSum = 0.0f;
    for (auto &bin : spaceBins) {
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        spaceSum += bin.magnitude;
        spacePeak = std::max(spacePeak, bin.magnitude);
    }

    // 2. Zajpadló tracking (noise floor módszer)
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

    // 3. Envelope tracking (decayavg módszer)
    // Gyors felfutás ha jel > envelope, lassú lecsengés ha jel < envelope
    auto updateEnvelope = [](float currentEnv, float magnitude) -> float {
        if (currentEnv == 0.0f) {
            return magnitude;
        }
        float alpha = (magnitude > currentEnv) ? ENVELOPE_ATTACK : ENVELOPE_DECAY;
        return currentEnv * (1.0f - alpha) + magnitude * alpha;
    };

    markEnvelope = updateEnvelope(markEnvelope, markPeak);
    spaceEnvelope = updateEnvelope(spaceEnvelope, spacePeak);

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

    float dominantMagnitude = std::max(markPeak, spacePeak);
    bool toneDetected = dominantMagnitude >= MIN_DOMINANT_MAG;

    // Debug: periodikus kiírás
    static int debugCounter = 0;
    // Csak akkor logolunk, ha van értelmes jel (toneDetected) és csak minden 20. blokkban logolunk
    if (++debugCounter >= 20 && toneDetected) {

#if ENABLE_AGC
        RTTY_DEBUG("RTTY-C1: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, gain=%.2f/%.2f, AGC=%.0f/%.0f, metric=%.3f, %s\n", markPeak, markEnvelope, spacePeak, spaceEnvelope, markClipped, spaceClipped, markGain,
                   spaceGain, markAgc, spaceAgc, metric, isMark ? "MARK" : "SPACE");
#else
        RTTY_DEBUG("RTTY-C1: M=%.0f/%.0f, S=%.0f/%.0f, Mc=%.0f, Sc=%.0f, metric=%.3f, %s (confidence: %.2f)\n", markPeak, markEnvelope, spacePeak, spaceEnvelope, markClipped, spaceClipped, metric,
                   isMark ? "MARK" : "SPACE", confidence);
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
    lastDominantMagnitude = 0.0f;
    lastOppositeMagnitude = 0.0f;
    markEnvelope = 0.0f;
    spaceEnvelope = 0.0f;
    initializeToneDetector();
    initializePLL();
}
