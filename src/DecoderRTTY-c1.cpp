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
 * Last Modified: 2025.12.03, Wednesday  05:10:47                                                                      *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

// Működő RTTY dekóder (a samples/ mappából adaptálva)
// Teszt dekódolás: https://www.youtube.com/watch?v=-4UWeo-wSmA

#include <cmath>

#include "DecoderRTTY-c1.h"
#include "defines.h"

extern DecodedData decodedData;

#define BIN_SPACING_HZ 35.0f
#define TONE_BLOCK_SIZE 64     // Kisebb blokk a gyorsabb reakcióért
#define MIN_NOISE_FLOOR 10.0f  // Alacsony minimum a gyenge jelekhez
#define MIN_DOMINANT_MAG 50.0f // Nagyon alacsony küszöb a gyenge jelek fogadásához

// envelope és noise tracking konstansok (fldigi alapú, optimalizált gyenge jelekhez)
static constexpr float ENVELOPE_ATTACK_ALPHA = 1.0f / 16.0f; // gyors attack (64/4 = 16)
static constexpr float ENVELOPE_DECAY_ALPHA = 1.0f / 512.0f; // közepesen gyors decay (64*8 = 512)
static constexpr float NOISE_ATTACK_ALPHA = 1.0f / 16.0f;    // gyors attack (64/4 = 16)
static constexpr float NOISE_DECAY_ALPHA = 1.0f / 3072.0f;   // lassú decay (64*48 = 3072)
static constexpr float MIN_ENVELOPE_THRESHOLD = 20.0f;       // minimum envelope szint - gyenge jelek

// Baudot LTRS (betűk) tábla - ITA2 szabvány
const char DecoderRTTY_C1::BAUDOT_LTRS_TABLE[32] = {
    '\0', 'E', '\n', 'A',  ' ', 'S', 'I', 'U', // 0-7
    '\r', 'D', 'R',  'J',  'N', 'F', 'C', 'K', // 8-15
    'T',  'Z', 'L',  'W',  'H', 'Y', 'P', 'Q', // 16-23
    'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0' // 24-31
};

// Baudot FIGS (számok/jel) tábla - ITA2 szabvány
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
      pllDPhase(0.0f), pllAlpha(0.0f), pllBeta(0.0f), pllLocked(false), pllLockCounter(0), symbolLen(TONE_BLOCK_SIZE), bitBufferCounter(0), bitsReceived(0),
      currentByte(0), figsShift(false), lastChar('\0'), freqError(0.0f), afcEnabled(1), historyPtr(0), lastDominantMagnitude(0.0f),
      lastOppositeMagnitude(0.0f) {
    for (int i = 0; i < MAX_BIT_BUFFER_SIZE; i++)
        bitBuffer[i] = false;
    for (int i = 0; i < MAXPIPE; i++) {
        markHistory[i].real = 0.0f;
        markHistory[i].imag = 0.0f;
        spaceHistory[i].real = 0.0f;
        spaceHistory[i].imag = 0.0f;
    }
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

    // RMS előnormalizáció állapotának inicializálása
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

#                                        // Opciók / beállítható funkciók
#define ENABLE_INPUT_RMS_NORMALIZATION 0 // Kikapcsolva, az Optimal ATC jobban kezeli a zajt
#define RMS_WINDOW_SAMPLES 128           // számítsd át igény szerint (kisebb érték → gyorsabb konvergencia)
#define RMS_TARGET 12000.0f              // kívánt RMS szint (tuning) - emelve a jó log értékekhez

// Opcionális puha limitáló
#define ENABLE_SOFT_LIMITER 0 // Kikapcsolva, az Optimal ATC jobban kezeli a zajt
#define SOFT_LIMIT_THRESHOLD 30000.0f

void DecoderRTTY_C1::processToneBlock(const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        float sample = static_cast<float>(samples[i]);

#if ENABLE_INPUT_RMS_NORMALIZATION
        // RMS frissítése futás közben (egyszerű gyűjtő/accumulátor)
        inputRmsAccum += sample * sample;
        inputRmsCount++;
        if (inputRmsCount >= RMS_WINDOW_SAMPLES) {
            float mean = inputRmsAccum / static_cast<float>(inputRmsCount);
            float rms = sqrtf(mean);
            // Számítsuk a nyereséget az RMS célszinthez hozáshoz, óvatosan
            float targetGain = (rms > 1.0f) ? (RMS_TARGET / rms) : 1.0f;
            // Limit gain range (engedjünk nagyobb maximális erősítést gyors konvergenciához)
            targetGain = constrain(targetGain, 0.6f, 3.0f);
            // Smoothly update inputGain — kisebb lépések helyett gyorsabb konvergencia
            inputGain = inputGain * 0.75f + targetGain * 0.25f;
            inputRmsAccum = 0.0f;
            inputRmsCount = 0;
        }
        sample *= inputGain;
#endif

#if ENABLE_SOFT_LIMITER
        // Puha limiter: tanh-szerű görbe a kiugrások csökkentésére
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
                // fldigi módszer: bit buffer alapú dekódolás
                bool charDecoded = rxBit(isMark);

                // AFC frissítés ha karakter dekódolva
                if (afcEnabled && charDecoded) {
                    updateAFC(true);
                }

                lastToneIsMark = isMark;
                lastToneConfidence = confidence;
            }

            resetGoertzelState();
        }
    }
}

bool DecoderRTTY_C1::detectTone(bool &isMark, float &confidence) {
    // 1. Goertzel amplitúdó számítás ÉS complex értékek mentése (AFC-hez)
    float markPeak = 0.0f;
    float markSum = 0.0f;
    cmplx markComplex = {0.0f, 0.0f}; // központi bin complex értéke

    for (int i = 0; i < BINS_PER_TONE; i++) {
        auto &bin = markBins[i];
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        markSum += bin.magnitude;
        if (bin.magnitude > markPeak) {
            markPeak = bin.magnitude;
            // Mentsd a központi bin complex értékét AFC-hez
            if (i == BINS_PER_TONE / 2) {
                // Complex Goertzel kimenet: real = q1 * sin(omega), imag = q1 * cos(omega) - q2
                float k = bin.targetFreq * TONE_BLOCK_SIZE / samplingRate;
                float omega = (2.0f * PI * k) / TONE_BLOCK_SIZE;
                markComplex.real = bin.q1 * sinf(omega);
                markComplex.imag = bin.q1 * cosf(omega) - bin.q2;
            }
        }
    }

    float spacePeak = 0.0f;
    float spaceSum = 0.0f;
    cmplx spaceComplex = {0.0f, 0.0f};

    for (int i = 0; i < BINS_PER_TONE; i++) {
        auto &bin = spaceBins[i];
        float magSquared = (bin.q1 * bin.q1) + (bin.q2 * bin.q2) - (bin.q1 * bin.q2 * bin.coeff);
        bin.magnitude = (magSquared > 0.0f) ? sqrtf(magSquared) : 0.0f;
        spaceSum += bin.magnitude;
        if (bin.magnitude > spacePeak) {
            spacePeak = bin.magnitude;
            if (i == BINS_PER_TONE / 2) {
                float k = bin.targetFreq * TONE_BLOCK_SIZE / samplingRate;
                float omega = (2.0f * PI * k) / TONE_BLOCK_SIZE;
                spaceComplex.real = bin.q1 * sinf(omega);
                spaceComplex.imag = bin.q1 * cosf(omega) - bin.q2;
            }
        }
    }

    // Mentsd a complex értékeket a history bufferbe (AFC-hez)
    markHistory[historyPtr] = markComplex;
    spaceHistory[historyPtr] = spaceComplex;
    historyPtr = (historyPtr + 1) % MAXPIPE;

    // 2. Zajpadló követése (fldigi módszer: decayavg)
    // mark_noise = decayavg(mark_noise, mark_mag, (mark_mag < mark_noise) ? symbollen/4 : symbollen*48)
    float markNoiseSample = (BINS_PER_TONE > 1) ? ((markSum - markPeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;
    float spaceNoiseSample = (BINS_PER_TONE > 1) ? ((spaceSum - spacePeak) / static_cast<float>(BINS_PER_TONE - 1)) : 0.0f;

    // decayavg implementáció: new_val = old_val * (1-alpha) + sample * alpha
    auto decayavg = [](float oldVal, float sample, float alpha) -> float {
        if (oldVal == 0.0f)
            return sample;
        return oldVal * (1.0f - alpha) + sample * alpha;
    };

    // Noise floor követés: gyors attack ha csökken, lassú decay ha nő
    markNoiseFloor = decayavg(markNoiseFloor, markNoiseSample, (markNoiseSample < markNoiseFloor) ? NOISE_ATTACK_ALPHA : NOISE_DECAY_ALPHA);
    spaceNoiseFloor = decayavg(spaceNoiseFloor, spaceNoiseSample, (spaceNoiseSample < spaceNoiseFloor) ? NOISE_ATTACK_ALPHA : NOISE_DECAY_ALPHA);

    // Minimum zajpadló biztosítása
    markNoiseFloor = std::max(markNoiseFloor, MIN_NOISE_FLOOR);
    spaceNoiseFloor = std::max(spaceNoiseFloor, MIN_NOISE_FLOOR);

    // 3. Envelope követés (fldigi módszer, javított)
    // mark_env = decayavg(mark_env, mark_mag, (mark_mag > mark_env) ? symbollen/4 : symbollen*16)
    // Gyors attack ha nő, gyors decay ha csökken
    markEnvelope = decayavg(markEnvelope, markPeak, (markPeak > markEnvelope) ? ENVELOPE_ATTACK_ALPHA : ENVELOPE_DECAY_ALPHA);
    spaceEnvelope = decayavg(spaceEnvelope, spacePeak, (spacePeak > spaceEnvelope) ? ENVELOPE_ATTACK_ALPHA : ENVELOPE_DECAY_ALPHA);

    // Reset envelope ha nincs elég jel (gyors válasz jel hiányára)
    if (markPeak < MIN_ENVELOPE_THRESHOLD)
        markEnvelope = std::max(markEnvelope * 0.95f, markPeak);
    if (spacePeak < MIN_ENVELOPE_THRESHOLD)
        spaceEnvelope = std::max(spaceEnvelope * 0.95f, spacePeak);

    // 4. Clipping (kivágás az envelope szintre)
    float markClipped = std::min(markPeak, markEnvelope);
    float spaceClipped = std::min(spacePeak, spaceEnvelope);

    float noiseFloor = std::min(markNoiseFloor, spaceNoiseFloor);
    markClipped = std::max(markClipped, noiseFloor);
    spaceClipped = std::max(spaceClipped, noiseFloor);

    // 5. Optimal ATC metric számítás (fldigi algoritmus, normalizált)
    // v3 = (mclipped - noise) * (mark_env - noise) -
    //      (sclipped - noise) * (space_env - noise) - 0.25 * (
    //      (mark_env - noise)² - (space_env - noise)²)
    float mClipMinusNoise = markClipped - noiseFloor;
    float sClipMinusNoise = spaceClipped - noiseFloor;
    float mEnvMinusNoise = markEnvelope - noiseFloor;
    float sEnvMinusNoise = spaceEnvelope - noiseFloor;

    float metric =
        mClipMinusNoise * mEnvMinusNoise - sClipMinusNoise * sEnvMinusNoise - 0.25f * (mEnvMinusNoise * mEnvMinusNoise - sEnvMinusNoise * sEnvMinusNoise);

    // Normalizálás: osztás az átlagos envelope-val (arányosítás)
    float avgEnv = (markEnvelope + spaceEnvelope) * 0.5f;
    if (avgEnv > 10.0f) {
        metric = metric / avgEnv; // egyszerű normalizálás
    }

    isMark = (metric > 0.0f);
    confidence = fabsf(metric);

    float dominantMagnitude = std::max(markPeak, spacePeak);
    bool toneDetected = dominantMagnitude >= MIN_DOMINANT_MAG;

    // Hibakereső kiírás (debug)
    static int debugCounter = 0;
    if (++debugCounter >= 20 && toneDetected) {
        DEBUG("RTTY: M=%.0f/%.0f/%.0f, S=%.0f/%.0f/%.0f, Mc=%.0f, Sc=%.0f, nf=%.0f, metric=%.1f, %s (conf: %.1f)\n", markPeak, markEnvelope, markNoiseFloor,
              spacePeak, spaceEnvelope, spaceNoiseFloor, markClipped, spaceClipped, noiseFloor, metric, isMark ? "MARK" : "SPACE", confidence);
        debugCounter = 0;
    }

    return toneDetected;
}

void DecoderRTTY_C1::initializePLL() {
    // PLL már nem használatos (bit buffer módszer helyettesíti)
    // symbolLen = minták száma 1 bitre (TONE_BLOCK_SIZE-ban mérve)
    float samplesPerBit = samplingRate / baudRate;
    symbolLen = static_cast<int>(samplesPerBit / TONE_BLOCK_SIZE + 0.5f); // kerekítés
    if (symbolLen < 1)
        symbolLen = 1;
    if (symbolLen > MAX_BIT_BUFFER_SIZE / 2)
        symbolLen = MAX_BIT_BUFFER_SIZE / 2;

    DEBUG("RTTY: symbolLen=%d (%.1f samples/bit, %.1f blocks/bit)\n", symbolLen, samplesPerBit, samplesPerBit / TONE_BLOCK_SIZE);

    pllPhase = 0.0f;
    pllLocked = false;
    pllLockCounter = 0;
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

// Bit buffer segédfüggvények (fldigi módszer)
bool DecoderRTTY_C1::isMarkSpaceTransition(int &correction) {
    correction = 0;
    // Keresünk MARK→SPACE átmenetet (start bit detektálás)
    // bitBuffer[0] = legrégebbi, bitBuffer[symbolLen-1] = legújabb
    if (bitBuffer[0] && !bitBuffer[symbolLen - 1]) {
        // Számoljuk meg hány MARK bit van a bufferben
        for (int i = 0; i < symbolLen; i++) {
            if (bitBuffer[i])
                correction++;
        }
        // Ha kb. a buffer felében van az átmenet, akkor valid start bit
        // abs(symbolLen/2 - correction) < 6 → helyes pozíció
        if (abs(symbolLen / 2 - correction) < 6) {
            return true;
        }
    }
    return false;
}

bool DecoderRTTY_C1::isMarkAtCenter() {
    // Mintavétel a bit közepéről
    return bitBuffer[symbolLen / 2];
}

bool DecoderRTTY_C1::rxBit(bool bit) {
    bool charDecoded = false;

    // Shift bit buffer (FIFO)
    for (int i = 1; i < symbolLen; i++) {
        bitBuffer[i - 1] = bitBuffer[i];
    }
    bitBuffer[symbolLen - 1] = bit;

    int correction = 0;

    switch (currentState) {
        case IDLE:
            if (isMarkSpaceTransition(correction)) {
                currentState = START_BIT;
                bitBufferCounter = correction; // automatic timing correction!
            }
            break;

        case START_BIT:
            if (--bitBufferCounter == 0) {
                if (!isMarkAtCenter()) { // Start bit = SPACE
                    currentState = DATA_BITS;
                    bitBufferCounter = symbolLen;
                    bitsReceived = 0;
                    currentByte = 0;
                } else {
                    // False start bit
                    currentState = IDLE;
                }
            }
            break;

        case DATA_BITS:
            if (--bitBufferCounter == 0) {
                if (isMarkAtCenter()) {
                    currentByte |= (1 << bitsReceived);
                }
                bitsReceived++;
                bitBufferCounter = symbolLen;

                if (bitsReceived >= 5) {
                    currentState = STOP_BIT;
                }
            }
            break;

        case STOP_BIT:
            if (--bitBufferCounter == 0) {
                if (isMarkAtCenter()) { // Stop bit KELL MARK legyen!
                    char c = decodeBaudotCharacter(currentByte);
                    if (c != '\0') {
                        // Duplikált CR/LF szűrés (fldigi)
                        if ((c == '\r' && lastChar == '\r') || (c == '\n' && lastChar == '\n')) {
                            // Skip duplikált line ending
                        } else {
                            if (!decodedData.textBuffer.put(c)) {
                                DEBUG("RTTY: textBuffer tele (karakter='%c')\n", c);
                            }
                        }
                        lastChar = c;
                    }
                    charDecoded = true;
                }
                // Mindig vissza IDLE-ba (helyes vagy hibás stop bit után)
                currentState = IDLE;
            }
            break;

        default:
            break;
    }

    return charDecoded;
}

// AFC (Automatic Frequency Control) - fldigi módszer
void DecoderRTTY_C1::updateAFC(bool charDecoded) {
    if (!afcEnabled || !charDecoded)
        return;

    // Számítsuk a frekvencia hibát a fázis változásból
    // fldigi: freqerr = (TWOPI * samplerate / baud) * arg(conj(history[n]) * history[n-1])
    int mp0 = historyPtr - 2;
    int mp1 = historyPtr - 1;
    if (mp0 < 0)
        mp0 += MAXPIPE;
    if (mp1 < 0)
        mp1 += MAXPIPE;

    // Complex conjugate multiply: conj(a) * b = (a.real * b.real + a.imag * b.imag) + j(a.real * b.imag - a.imag * b.real)
    // arg() = atan2(imag, real)
    cmplx mark0 = markHistory[mp0];
    cmplx mark1 = markHistory[mp1];

    float mark_real = mark0.real * mark1.real + mark0.imag * mark1.imag;
    float mark_imag = mark0.real * mark1.imag - mark0.imag * mark1.real;
    float mark_phase = atan2f(mark_imag, mark_real);

    // Konvertáljuk Hz-re
    float ferr = (2.0f * PI * samplingRate / baudRate) * mark_phase;

    // Limit check
    if (fabsf(ferr) > baudRate / 2.0f)
        ferr = 0.0f;

    // decayavg - AFC sebesség: 0=lassú(8), 1=közepes(4), 2=gyors(1)
    float afcSpeed = (afcEnabled == 0) ? 8.0f : (afcEnabled == 1) ? 4.0f : 1.0f;
    float alpha = 1.0f / afcSpeed;
    freqError = freqError * (1.0f - alpha) + (ferr / 8.0f) * alpha;

    // Korrigáljuk a frekvenciákat
    float newMarkFreq = markFreq - freqError;
    float newSpaceFreq = spaceFreq - freqError;

    // Limit check - ne menjunk túl messzire
    float maxDrift = (markFreq - spaceFreq) * 0.5f; // max shift/2 drift
    if (fabsf(freqError) < maxDrift) {
        reconfigureFrequencies(newMarkFreq, newSpaceFreq);
    }

    static int debugCounter = 0;
    if (++debugCounter >= 20) {
        DEBUG("AFC: ferr=%.1f Hz, freqError=%.1f Hz, newMark=%.1f Hz, newSpace=%.1f Hz\n", ferr, freqError, newMarkFreq, newSpaceFreq);
        debugCounter = 0;
    }
}

void DecoderRTTY_C1::reconfigureFrequencies(float newMarkFreq, float newSpaceFreq) {
    markFreq = newMarkFreq;
    spaceFreq = newSpaceFreq;
    configureToneBins(markFreq, markBins);
    configureToneBins(spaceFreq, spaceBins);
}

void DecoderRTTY_C1::resetDecoder() {
    currentState = IDLE;
    bitsReceived = 0;
    currentByte = 0;
    figsShift = false;
    lastChar = '\0';
    bitBufferCounter = 0;
    freqError = 0.0f;
    historyPtr = 0;
    for (int i = 0; i < MAX_BIT_BUFFER_SIZE; i++)
        bitBuffer[i] = false;
    for (int i = 0; i < MAXPIPE; i++) {
        markHistory[i].real = 0.0f;
        markHistory[i].imag = 0.0f;
        spaceHistory[i].real = 0.0f;
        spaceHistory[i].imag = 0.0f;
    }
    lastDominantMagnitude = 0.0f;
    lastOppositeMagnitude = 0.0f;
    markEnvelope = 0.0f;
    spaceEnvelope = 0.0f;
    initializeToneDetector();
    initializePLL();
}
