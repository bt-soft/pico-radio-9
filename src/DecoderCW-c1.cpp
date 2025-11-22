/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderCW-c1.cpp                                                                                              *
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
 * Last Modified: 2025.11.22, Saturday  10:38:39                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "DecoderCW-c1.h"
#include "defines.h"

extern DecodedData decodedData;

#define __CW_DEBUG
#if defined(__DEBUG) && defined(__CW_DEBUG)
#define CW_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define CW_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// CW működés debug engedélyezése (csak ha __DEBUG definiálva van)
#define __CW_DEBUG
#if defined(__DEBUG) && defined(__CW_DEBUG)
#define CW_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define CW_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// Morse szimbólum tömb inicializálása
constexpr char DecoderCW_C1::morseSymbols_[128];

/**
 * @brief CwDecoderC1 konstruktor - inicializálja az alapértelmezett értékeket
 */
DecoderCW_C1::DecoderCW_C1()
    : samplingRate_(0), targetFreq_(800.0f), goertzelCoeff_(0.0f), goertzelQ1_(0.0f), goertzelQ2_(0.0f), threshold_(2000.0f), currentFreqIndex_(4),
      toneDetected_(false), leadingEdgeTime_(0), trailingEdgeTime_(0), startReference_(200), reference_(200), toneMin_(9999), toneMax_(0), lastElement_(0),
      currentWpm_(0), toneIndex_(0), symbolIndex_(63), symbolOffset_(32), symbolCount_(0), started_(false), measuring_(false), wpmHistoryIndex_(0),
      freqHistoryCount_(0), lastPublishedWpm_(0), lastPublishedFreq_(0.0f) {

    memset(scanFrequencies_, 0, sizeof(scanFrequencies_));
    memset(scanCoeffs_, 0, sizeof(scanCoeffs_));
    memset(toneDurations_, 0, sizeof(toneDurations_));
    memset(wpmHistory_, 0, sizeof(wpmHistory_));
    // Initialize sliding buffer for frequency tracking
    memset(lastSamples_, 0, sizeof(lastSamples_));
    lastSampleCount_ = 0;
    lastSamplePos_ = 0;
}

/**
 * @brief Dekóder indítása - konfiguráció és Goertzel inicializálás
 * @param decoderConfig Dekóder konfiguráció (samplingRate, sampleCount, cwCenterFreqHz)
 * @return true ha sikerült elindítani
 */
bool DecoderCW_C1::start(const DecoderConfig &decoderConfig) {
    CW_DEBUG("CW-C1: Dekóder indítása - samplingRate: %u Hz, centerFreq: %u Hz, sampleCount: %u\n", decoderConfig.samplingRate, decoderConfig.cwCenterFreqHz,
             decoderConfig.sampleCount);

    samplingRate_ = decoderConfig.samplingRate;
    targetFreq_ = decoderConfig.cwCenterFreqHz > 0 ? (float)decoderConfig.cwCenterFreqHz : 800.0f;

    // Frekvencia tartomány inicializálása: ±x Hz, 50 Hz lépésekkel
    for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
        scanFrequencies_[i] = targetFreq_ + FREQ_STEPS[i];
        scanCoeffs_[i] = calculateGoertzelCoeff(scanFrequencies_[i]);
        CW_DEBUG("CW-C1: Scan freq[%d] = %.1f Hz, coeff = %.4f\n", i, scanFrequencies_[i], scanCoeffs_[i]);
    }

    currentFreqIndex_ = 4; // Kezdjük a középső frekvenciával (0 Hz offset)
    initGoertzel();
    resetDecoder();

    // Hann ablak inicializálása a Goertzel blokkokhoz
    windowApplier.build(GOERTZEL_N, WindowType::Hann, true);

    // Publikáljuk a kezdő állapotot
    ::decodedData.cwCurrentFreq = static_cast<uint16_t>(scanFrequencies_[currentFreqIndex_]);
    ::decodedData.cwCurrentWpm = 0;

    CW_DEBUG("CW-C1: Dekóder sikeresen elindítva\n");
    return true;
}

/**
 * @brief Dekóder leállítása
 */
void DecoderCW_C1::stop() {
    CW_DEBUG("CW-C1: Dekóder leállítva\n");
    resetDecoder();
    ::decodedData.cwCurrentWpm = 0;
    ::decodedData.cwCurrentFreq = 0;
}

/**
 * @brief Goertzel együttható számítása adott frekvenciára
 * @param frequency Célfrekvencia Hz-ben
 * @return Goertzel együttható
 */
float DecoderCW_C1::calculateGoertzelCoeff(float frequency) {
    float k = (GOERTZEL_N * frequency) / samplingRate_;
    float omega = (2.0f * PI * k) / GOERTZEL_N;
    return 2.0f * cos(omega);
}

/**
 * @brief Goertzel inicializálása
 */
void DecoderCW_C1::initGoertzel() {
    goertzelQ1_ = 0.0f;
    goertzelQ2_ = 0.0f;
    goertzelCoeff_ = scanCoeffs_[currentFreqIndex_];
}

/**
 * @brief Goertzel algoritmus futtatása egy blokkon
 * @param samples Bemeneti minták
 * @param count Minták száma
 * @param coeff Goertzel együttható
 * @return Jelszint (magnitude)
 */
float DecoderCW_C1::processGoertzelBlock(const float *samples, size_t count, float coeff) {
    float q1 = 0.0f;
    float q2 = 0.0f;

    for (size_t i = 0; i < count && i < GOERTZEL_N; i++) {
        float q0 = coeff * q1 - q2 + samples[i];
        q2 = q1;
        q1 = q0;
    }

    float magnitudeSquared = (q1 * q1) + (q2 * q2) - q1 * q2 * coeff;
    return sqrtf(magnitudeSquared);
}

/**
 * @brief Tónus detekció Goertzel filterrel
 * @param samples Bemeneti minták
 * @param count Minták száma
 * @return true ha tónus detektálva
 */
bool DecoderCW_C1::detectTone(const int16_t *samples, size_t count) {
    if (count < GOERTZEL_N) {
        return toneDetected_; // Nem elég minta, megtartjuk az előző állapotot
    }

    // Minden blokkban mérjük a legerősebb frekvenciát
    float maxMagnitude = 0.0f;
    int bestIndex = currentFreqIndex_;

    // Az ablak alkalmazása a mintákra (helyi pufferbe)
    float buf[GOERTZEL_N];
    windowApplier.apply(samples, buf, GOERTZEL_N);
    for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
        float mag = processGoertzelBlock(buf, GOERTZEL_N, scanCoeffs_[i]);
        if (mag > maxMagnitude) {
            maxMagnitude = mag;
            bestIndex = i;
        }
    }
    measuredFreqIndex_ = bestIndex;
    float magnitude = maxMagnitude;

    // --- AGC: threshold_ dinamikus optimalizálása ---
    if (useAdaptiveThreshold_) {

        // Seed AGC on first meaningful measurement to avoid huge initial agcLevel_
        if (!agcInitialized_) {
            // If magnitude is very small, don't seed yet
            if (magnitude > 1.0f) {
                agcLevel_ = magnitude;
                agcInitialized_ = true;
            }
        } else {
            agcLevel_ = (1.0f - agcAlpha_) * agcLevel_ + agcAlpha_ * magnitude;
        }

        // Threshold számítása
        threshold_ = agcLevel_ * THRESH_FACTOR;
        if (threshold_ < minThreshold_) {
            threshold_ = minThreshold_;
        }

    } else {
        // Ha az adaptív küszöb ki van kapcsolva, használjuk a fix minThreshold_-t
        threshold_ = minThreshold_;
        if (agcInitialized_) {
            agcInitialized_ = false; // ha az adaptív küszöb ki van kapcsolva, reseteljük az AGC állapotot
        }
    }

    // Tónus detekció küszöb alapján
    bool newToneState = (magnitude > threshold_);

    // Debug: kiíratás a Goertzel magnitúdóról és a használt küszöbről (ritkítva)
    static int __cw_dbg_cnt = 0;
    if (++__cw_dbg_cnt >= 10) {
        // Mért frekvencia a legjobb index alapján
        float detectedFreq = scanFrequencies_[measuredFreqIndex_];
        if (useAdaptiveThreshold_) {
            CW_DEBUG("CW-C1: detectTone: freq=%.1f Hz, mag=%.1f, agc=ON, agcLevel=%.1f, threshold=%.1f\n", detectedFreq, magnitude, agcLevel_, threshold_);
        } else {
            CW_DEBUG("CW-C1: detectTone: freq=%.1f Hz, mag=%.1f, agc=OFF\n", detectedFreq, magnitude);
        }
        __cw_dbg_cnt = 0;
    }

    // Ha tónus állapot változott, ellenőrizzük és frissítjük a frekvencia-követést
    if (newToneState != toneDetected_) {
        updateFrequencyTracking();
    }

    toneDetected_ = newToneState;
    return toneDetected_;
}

/**
 * @brief Adaptív frekvencia követés - megkeresi a legerősebb frekvenciát
 */
void DecoderCW_C1::updateFrequencyTracking() {
    // Csak akkor követjük, ha tónus van jelen
    if (!toneDetected_) {
        return;
    }
    // Use member sliding buffer populated by processSamples()
    if (lastSampleCount_ < GOERTZEL_N) {
        return; // Még nincs elég minta
    }

    // Keressük meg a legerősebb frekvenciát
    float maxMagnitude = 0.0f;
    uint8_t bestIndex = currentFreqIndex_;

    // Az ablak alkalmazása az utolsó mintákra (helyi pufferbe)
    float buf[GOERTZEL_N];
    if (lastSamplePos_ == 0) {
        windowApplier.apply(lastSamples_, buf, GOERTZEL_N);
    } else {
        int16_t temp[GOERTZEL_N];
        size_t tail = GOERTZEL_N - lastSamplePos_;
        memcpy(temp, lastSamples_ + lastSamplePos_, tail * sizeof(int16_t));
        memcpy(temp + tail, lastSamples_, lastSamplePos_ * sizeof(int16_t));
        windowApplier.apply(temp, buf, GOERTZEL_N);
    }
    for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
        float magnitude = processGoertzelBlock(buf, GOERTZEL_N, scanCoeffs_[i]);
        if (magnitude > maxMagnitude) {
            maxMagnitude = magnitude;
            bestIndex = i;
        }
    }

    // Ha változott a frekvencia és a különbség nagyobb mint CHANGE_TONE_THRESHOLD Hz, akkor frissítjük a beállításokat
    if (bestIndex != currentFreqIndex_ || abs(scanFrequencies_[bestIndex] - scanFrequencies_[currentFreqIndex_]) > CHANGE_TONE_THRESHOLD) {
        currentFreqIndex_ = bestIndex;
        goertzelCoeff_ = scanCoeffs_[currentFreqIndex_];
        CW_DEBUG("CW-C1: Frekvencia váltás: %.1f Hz\n", scanFrequencies_[currentFreqIndex_]);
    }
}

/**
 * @brief Audio minták blokkos feldolgozása
 * @param rawAudioSamples Bemeneti audio minták tömbje (DC offset már eltávolítva)
 * @param count Minták száma
 */
void DecoderCW_C1::processSamples(const int16_t *rawAudioSamples, size_t count) {
    if (count == 0) {
        return;
    }

    // Blokkos feldolgozás
    size_t offset = 0;
    while (offset < count) {
        size_t blockSize = min((size_t)GOERTZEL_N, count - offset);
        // A körkörös csúszó puffer frissítése a bejövő mintákkal
        for (size_t i = 0; i < blockSize; ++i) {
            lastSamples_[lastSamplePos_] = rawAudioSamples[offset + i];
            lastSamplePos_ = (lastSamplePos_ + 1) % GOERTZEL_N;
            if (lastSampleCount_ < GOERTZEL_N)
                ++lastSampleCount_;
        }

        bool tone = detectTone(rawAudioSamples + offset, blockSize);

        if (tone && freqHistoryCount_ < FREQ_HISTORY_SIZE) {
            freqHistory_[freqHistoryCount_++] = measuredFreqIndex_;
        }

        unsigned long currentTime = millis();

        // === Állapotgép ===

        // Szóköz küszöb: adaptív, gyors WPM-nél kisebb, lassúnál nagyobb, de ésszerű határok között
        auto clamp = [](unsigned long v, unsigned long lo, unsigned long hi) { return std::max(lo, std::min(v, hi)); };
        // Finomhangolás: kisebb alsó korlát, kisebb szorzó, hogy gyorsabb tempónál is legyen szóköz
        const unsigned long minWordSpace = clamp(reference_ * 3.1, 80UL, 600UL);
        static bool lastDecodeSuccess = false;

        // Kezdeti felfutó él: várunk a jel kezdetére
        if (!started_ && !measuring_ && tone) {
            leadingEdgeTime_ = currentTime;

            // Szóköz beillesztése, ha hosszú szünet volt és az előző dekódolás sikeres volt
            if (trailingEdgeTime_ > 0 && (currentTime - trailingEdgeTime_) > minWordSpace && lastDecodeSuccess) {
                ::decodedData.textBuffer.put(' ');
                CW_DEBUG("CW-C1: Szóköz\n");
                lastDecodeSuccess = false; // csak egyszer szúrjunk be szóközt
            }

            started_ = true;
            measuring_ = true;
        }

        // Lefutó él detektálása
        else if (started_ && measuring_ && !tone) {
            trailingEdgeTime_ = currentTime;
            unsigned long duration = trailingEdgeTime_ - leadingEdgeTime_;

            // dit-dah pár validáció (Lawrence Glaister, VE7IT)
            // Ellenőrizzük, hogy az előző elemmel együtt érvényes párt alkotunk-e
            if (lastElement_ > 0) {
                // Dit-Dah szekvencia ellenőrzése (jelenlegi elem = 3x előző)
                if ((duration > 2 * lastElement_) && (duration < 4 * lastElement_)) {
                    // Érvényes dit-dah pár!
                    updateTracking(lastElement_, duration);
                }
                // Dah-Dit szekvencia ellenőrzése (előző elem = 3x jelenlegi)
                else if ((lastElement_ > 2 * duration) && (lastElement_ < 4 * duration)) {
                    // Érvényes dah-dit pár!
                    updateTracking(duration, lastElement_);
                }
            }

            // A tónus időtartamának mentése a későbbi dekódoláshoz
            if (toneIndex_ < sizeof(toneDurations_) / sizeof(toneDurations_[0])) {
                toneDurations_[toneIndex_++] = duration;
            } else {
                CW_DEBUG("CW-C1: toneIndex_ overflow, reset!\n");
                resetDecoder();
                return;
            }

            // Ha még nincs referencia (első elemek), állítsunk kezdeti értékeket
            if (toneMin_ == 9999 || toneMax_ == 0) {
                toneMin_ = min(toneMin_, duration);
                toneMax_ = max(toneMax_, duration);
                if (toneMin_ != toneMax_) {
                    reference_ = (toneMin_ + toneMax_) / 2;
                }
            }

            // Mentjük az aktuális elemet a következő pár validációhoz
            lastElement_ = duration;
            measuring_ = false;
        }

        // Újabb felfutó él (folytatás)
        else if (started_ && !measuring_ && tone) {
            if ((currentTime - trailingEdgeTime_) < reference_) {
                leadingEdgeTime_ = currentTime;
                measuring_ = true;
            }
        }

        // Timeout - karakter vége vagy hosszú szünet kezelése
        else if (started_ && !measuring_ && !tone) {
            if (trailingEdgeTime_ > 0) { // Csak ha van érvényes lefutó él
                unsigned long pauseDuration = currentTime - trailingEdgeTime_;

                if (pauseDuration > 2000) { // 2 másodperces szünet -> időzítés nullázása
                    CW_DEBUG("CW-C1: Hosszú szünet, időzítés nullázása.\n");
                    // Dekódoljuk az utolsó karaktert, ha volt
                    bool decodeOk = false;
                    if (toneIndex_ > 0) {
                        decodeOk = decodeSymbol();
                    }
                    // Tegyünk egy szóközt, de csak ha az utolsó dekódolás sikeres volt ÉS a szünet elég hosszú
                    if (decodeOk && pauseDuration > minWordSpace) {
                        ::decodedData.textBuffer.put(' ');
                        lastDecodeSuccess = false;
                    }

                    // Időzítés nullázása
                    toneMin_ = 9999;
                    toneMax_ = 0;
                    lastElement_ = 0; // Pár validáció reset
                    reference_ = startReference_;

                    // WPM nullázása
                    memset(wpmHistory_, 0, sizeof(wpmHistory_));
                    wpmHistoryIndex_ = 0;
                    if (currentWpm_ != 0) {
                        currentWpm_ = 0;
                        if (lastPublishedWpm_ != 0) {
                            ::decodedData.cwCurrentWpm = 0;
                            lastPublishedWpm_ = 0;
                            CW_DEBUG("CW-C1: WPM PUBLISHED: 0\n");
                        }
                    }

                    // Állapotgép nullázása
                    started_ = false;
                    trailingEdgeTime_ = 0; // Újraindítás elkerülése

                } else if (pauseDuration > reference_ && toneIndex_ > 0) {
                    bool decodeOk = decodeSymbol();
                    lastDecodeSuccess = decodeOk;
                }
            }
        }

        offset += blockSize;
    }
}

/**
 * @brief Dit feldolgozása
 */
void DecoderCW_C1::processDot() {
    symbolIndex_ -= symbolOffset_;
    symbolOffset_ /= 2;
    symbolCount_ += 2; // 1 dit + 1 szünet
}

/**
 * @brief Dah feldolgozása
 */
void DecoderCW_C1::processDash() {
    symbolIndex_ += symbolOffset_;
    symbolOffset_ /= 2;
    symbolCount_ += 4; // 3 dit + 1 szünet
}

/**
 * @brief Szimbólum dekódolása a bináris fából
 * @return true ha sikeres volt a dekódolás
 */
bool DecoderCW_C1::decodeSymbol() {
    // Frissített referencia használata ha van
    if (toneMax_ != toneMin_) {
        reference_ = (toneMin_ + toneMax_) / 2;
    }

    // Lépkedés a bináris fában
    for (uint8_t i = 0; i < toneIndex_; i++) {
        if (toneDurations_[i] < reference_) {
            processDot();
        } else {
            processDash();
        }
    }

    // Karakter kiolvasása
    bool decodeSuccess = false;
    if (symbolIndex_ < 128) {
        char decodedChar = morseSymbols_[symbolIndex_];
        if (decodedChar != ' ') {
            // Stabil frekvencia megkeresése (módusz)
            if (freqHistoryCount_ > 0) {
                uint8_t counts[FREQ_SCAN_STEPS] = {0};
                for (size_t i = 0; i < freqHistoryCount_; i++) {
                    if (freqHistory_[i] < FREQ_SCAN_STEPS) {
                        counts[freqHistory_[i]]++;
                    }
                }
                uint8_t modeIndex = 0;
                uint8_t maxCount = 0;
                for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
                    if (counts[i] > maxCount) {
                        maxCount = counts[i];
                        modeIndex = i;
                    }
                }

                // A mért frekvencia publikálása, ha változott
                float newFreq = scanFrequencies_[modeIndex];
                if (newFreq != lastPublishedFreq_) {
                    ::decodedData.cwCurrentFreq = static_cast<uint16_t>(newFreq);
                    lastPublishedFreq_ = newFreq;
                    CW_DEBUG("CW-C1: Freq PUBLISHED: %.1f Hz\n", newFreq);
                }
                // CW_DEBUG("CW-C1: Freq samples: %d, Mode Index: %d, Freq: %.1f Hz\n", freqHistoryCount_, modeIndex, newFreq);
            } else {
                // Fallback, ha nincs frekvencia előzmény
                if (lastPublishedFreq_ != scanFrequencies_[currentFreqIndex_]) {
                    ::decodedData.cwCurrentFreq = static_cast<uint16_t>(scanFrequencies_[currentFreqIndex_]);
                    lastPublishedFreq_ = ::decodedData.cwCurrentFreq;
                }
            }

            // A dekódolt karakter beillesztése a vételi pufferbe
            ::decodedData.textBuffer.put(decodedChar);
            CW_DEBUG("CW-C1: Dekódolt: %c\n", decodedChar);
            decodeSuccess = true;
        }
    }

    // Előzmények törlése a következő karakterhez
    freqHistoryCount_ = 0;

    // WPM számítása
    if (symbolCount_ > 0 && trailingEdgeTime_ > leadingEdgeTime_) {
        calculateWpm(trailingEdgeTime_ - leadingEdgeTime_);
    }

    // Pointer reset
    symbolIndex_ = 63;
    symbolOffset_ = 32;
    toneIndex_ = 0;
    symbolCount_ = 0;
    started_ = false;
    measuring_ = false;
    return decodeSuccess;
}

/**
 * @brief WPM számítása
 * @param letterDuration A karakter teljes időtartama ms-ben
 */
void DecoderCW_C1::calculateWpm(unsigned long letterDuration) {
    if (symbolCount_ > 1 && letterDuration > 0) {
        // WPM = (szimbólumok * 1200) / időtartam
        uint8_t wpm = ((symbolCount_ - 1) * 1200) / letterDuration;

        // Korlátok ellenőrzése
        if (wpm >= minWpm_ && wpm <= maxWpm_) {
            // Előzmények frissítése
            wpmHistory_[wpmHistoryIndex_] = wpm;
            wpmHistoryIndex_ = (wpmHistoryIndex_ + 1) % WPM_HISTORY_SIZE;

            // Medián számítása (robosztusabb WPM)
            uint8_t sorted[WPM_HISTORY_SIZE];
            memcpy(sorted, wpmHistory_, sizeof(sorted));
            std::sort(sorted, sorted + WPM_HISTORY_SIZE);
            uint8_t valid = 0;
            for (size_t i = 0; i < WPM_HISTORY_SIZE; ++i) {
                if (sorted[i] > 0)
                    valid++;
            }
            if (valid > 0) {
                if (valid % 2 == 1) {
                    currentWpm_ = sorted[valid / 2];
                } else {
                    currentWpm_ = (sorted[valid / 2 - 1] + sorted[valid / 2]) / 2;
                }
            } else {
                currentWpm_ = wpm;
            }

            // Publikáljuk az aktuális WPM-et, ha változott
            if (currentWpm_ != lastPublishedWpm_) {
                ::decodedData.cwCurrentWpm = currentWpm_;
                lastPublishedWpm_ = currentWpm_;
                CW_DEBUG("CW-C1: WPM PUBLISHED: %u\n", currentWpm_);
            }
            CW_DEBUG("CW-C1: WPM (raw) = %u, WPM (med) = %u\n", wpm, currentWpm_);
        }
    }
}

/**
 * @brief Adaptív timing frissítés dit-dah pár alapján
 *
 * Ez a Lawrence Glaister (VE7IT) által kifejlesztett módszer:
 * "When you have no idea how fast the CW is, the only way to get a threshold
 * is by having BOTH code elements and setting the threshold between them,
 * knowing one is supposed to be 3x longer than the other."
 *
 * A módszer lényege:
 * - Megvárja, amíg dit-dah vagy dah-dit PÁRT kap
 * - Ellenőrzi, hogy a hosszabb elem ~3x hosszabb-e mint a rövidebb (2-4x tartomány)
 * - Csak érvényes párok esetén frissíti a referencia értéket
 * - Ez sokkal stabilabb mint minden elemet külön-külön kezelni
 *
 * @param dit Dit (rövid) elem hossza ms-ben
 * @param dah Dah (hosszú) elem hossza ms-ben
 */
void DecoderCW_C1::updateTracking(unsigned long dit, unsigned long dah) {
    // Validálás: a dah ~= 3 * dit (2-4x tartományban)
    if (dah >= 2 * dit && dah <= 4 * dit) {
        // Exponenciális mozgóátlag - simább követés
        if (toneMin_ < 9999) {
            toneMin_ = (toneMin_ + dit) / 2;
        } else {
            toneMin_ = dit;
        }

        if (toneMax_ > 0) {
            toneMax_ = (toneMax_ + dah) / 2;
        } else {
            toneMax_ = dah;
        }

        reference_ = (toneMin_ + toneMax_) / 2;

        // CW_DEBUG("CW-C1: Valid pair - dit=%lu ms, dah=%lu ms, ref=%lu ms\n", dit, dah, reference_);
    } else {
        // CW_DEBUG("CW-C1: Invalid pair ratio - dit=%lu ms, dah=%lu ms (ratio=%.2f, expected ~3.0)\n", dit, dah, (float)dah / dit);
    }
}

/**
 * @brief Dekóder reset
 */
void DecoderCW_C1::resetDecoder() {
    started_ = false;
    measuring_ = false;
    toneDetected_ = false;
    reference_ = startReference_;
    toneMin_ = 9999;
    toneMax_ = 0;
    lastElement_ = 0;
    toneIndex_ = 0;
    symbolIndex_ = 63;
    symbolOffset_ = 32;
    symbolCount_ = 0;
    currentWpm_ = 0;
    leadingEdgeTime_ = 0;
    trailingEdgeTime_ = 0;
    measuredFreqIndex_ = 4; // a középső frekvenciára állítjuk

    // Statisztikák és publikált értékek resetelése
    memset(wpmHistory_, 0, sizeof(wpmHistory_));
    wpmHistoryIndex_ = 0;
    freqHistoryCount_ = 0;
    lastPublishedWpm_ = 0;
    lastPublishedFreq_ = 0.0f;
    ::decodedData.cwCurrentWpm = 0;
    ::decodedData.cwCurrentFreq = 0;

    initGoertzel();
}
