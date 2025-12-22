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
 * Last Modified: 2025.12.22, Monday  09:53:58                                                                         *
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

// CW működés debug engedélyezése (csak ha __DEBUG definiálva van)
// #define __CW_DEBUG
#if defined(__DEBUG) && defined(__CW_DEBUG)
#define CW_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define CW_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// Morse szimbólum tömb inicializálása
constexpr char DecoderCW_C1::morseSymbols_[128];

/**
 * @brief fldigi decayavg() helper függvény - exponenciális mozgó átlag
 * @param average Jelenlegi átlag érték
 * @param input Új bemenet
 * @param weight Súly (nagyobb = lassabb követés)
 * @return Új átlag érték
 */
static inline float decayavg(float average, float input, int weight) {
    if (weight <= 1)
        return input;
    return ((input - average) / static_cast<float>(weight)) + average;
}

/**
 * @brief CwDecoderC1 konstruktor - inicializálja az alapértelmezett értékeket
 */
DecoderCW_C1::DecoderCW_C1()
    : samplingRate_(0), targetFreq_(800.0f), goertzelCoeff_(0), threshold_q15(1311), currentFreqIndex_(4), toneDetected_(false), leadingEdgeTime_(0),
      trailingEdgeTime_(0), startReference_(200), reference_(200), toneMin_(9999), toneMax_(0), lastElement_(0), currentWpm_(0), toneIndex_(0),
      symbolIndex_(63), symbolOffset_(32), symbolCount_(0), started_(false), measuring_(false), wpmHistoryIndex_(0), freqHistoryCount_(0), lastPublishedWpm_(0),
      lastPublishedFreq_(0.0f), stableFreqIndex_(4), stableHoldUntilMs_(0), candidateFreqIndex_(4), candidateCount_(0), candidateFirstSeenMs_(0) {

    memset(scanFrequencies_, 0, sizeof(scanFrequencies_));
    memset(scanCoeffs_, 0, sizeof(scanCoeffs_));
    memset(toneDurations_, 0, sizeof(toneDurations_));
    memset(wpmHistory_, 0, sizeof(wpmHistory_));
    // Initialize sliding buffer for frequency tracking
    memset(lastSamples_, 0, sizeof(lastSamples_));
    // A `processSamples()` által feltöltött csúszó puffer használata
    lastSampleCount_ = 0;
    lastSamplePos_ = 0;
    // Q15 AGC initialization is in header defaults
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
#ifdef __CW_DEBUG
        // Q15 → float debug konverzió
        float coeff_f = (float)scanCoeffs_[i] / Q15_MAX_AS_FLOAT;
        CW_DEBUG("CW-C1: Scan freq[%d] = %.1f Hz, coeff[Q15] = %d (%.4f)\n", i, scanFrequencies_[i], scanCoeffs_[i], coeff_f);
#endif
    }

    currentFreqIndex_ = 4; // Kezdjük a középső frekvenciával (0 Hz offset)
    initGoertzel();
    resetDecoder();

    // fldigi WPM limitek inicializálása (min_wpm - CWrange, max_wpm + CWrange)
    wpm_lowerlimit_ = (minWpm_ > CWrange_) ? (minWpm_ - CWrange_) : 1;
    wpm_upperlimit_ = maxWpm_ + CWrange_;
    CW_DEBUG("CW-C1: WPM limits - lower=%u, upper=%u\n", wpm_lowerlimit_, wpm_upperlimit_);

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
 * @param frequency Célfrekvencia (Hz)
 * @return Goertzel együttható Q15 formátumban (32767 = 1.0)
 */
q15_t DecoderCW_C1::calculateGoertzelCoeff(float frequency) {
    float k = (GOERTZEL_N * frequency) / samplingRate_;
    float omega = (2.0f * PI * k) / GOERTZEL_N;
    float coeff_float = 2.0f * cos(omega);
    // Q15 konverzió: [-1.0, 1.0] → [-32768, 32767]
    return (q15_t)(coeff_float * Q15_MAX_AS_FLOAT);
}

/**
 * @brief Goertzel inicializálása
 */
void DecoderCW_C1::initGoertzel() { goertzelCoeff_ = scanCoeffs_[currentFreqIndex_]; }

/**
 * @brief Goertzel algoritmus futtatása egy blokkon - FIXPONTOS Q15 implementáció
 * @param samples Bemeneti mintak int16_t formátumban (raw audio)
 * @param count Minták száma
 * @param coeff Goertzel együttható Q15 formátumban
 * @return Jelszint (magnitude) Q15 formátumban (approximate, nincs sqrt)
 */
q15_t DecoderCW_C1::processGoertzelBlock(const int16_t *samples, size_t count, q15_t coeff) {
    // Goertzel állapotváltozók Q15 formátumban
    int32_t q1 = 0; // Q15 (de 32-bit a túlcsordulás elkerülésére)
    int32_t q2 = 0;

    // Goertzel iteráció: q0 = coeff * q1 - q2 + sample
    // Fixpontos szorzás: (Q15 * Q15) >> 15 = Q15
    for (size_t i = 0; i < count && i < GOERTZEL_N; i++) {
        // coeff (Q15) * q1 (Q15) = Q30, >> 15 = Q15
        int32_t coeff_q1 = ((int32_t)coeff * q1) >> 15;
        int32_t q0 = coeff_q1 - q2 + (int32_t)samples[i];
        q2 = q1;
        q1 = q0;
    }

    // Magnitutó számítás gyors approximációval (nincs sqrt!)
    // Használjuk: mag ≈ max(|q1|, |q2|) + 0.5*min(|q1|, |q2|)
    // Ez gyors és elég pontos Goertzel-hez
    int32_t abs_q1 = (q1 < 0) ? -q1 : q1;
    int32_t abs_q2 = (q2 < 0) ? -q2 : q2;
    int32_t max_val = (abs_q1 > abs_q2) ? abs_q1 : abs_q2;
    int32_t min_val = (abs_q1 > abs_q2) ? abs_q2 : abs_q1;
    int32_t magnitude = max_val + (min_val >> 1); // max + 0.5*min

    // Clamp Q15 tartományba
    magnitude = constrain(magnitude, -32768, 32767);

    return (q15_t)magnitude;
}

/**
 * @brief Tónus detekció fldigi módszerrel - envelope tracking + adaptive threshold
 * @param samples Bemeneti minták int16_t formátumban
 * @param count Minták száma
 * @return true ha tónus detektálva
 */
bool DecoderCW_C1::detectTone(const int16_t *samples, size_t count) {

    // Ha nem elég a minta, akkor megtartjuk az előző állapotot
    if (count < GOERTZEL_N) {
        return toneDetected_;
    }

    // Minden blokkban mérjük a legerősebb frekvenciát (Q15 magnitúdók)
    q15_t maxMagnitude = 0;
    int bestIndex = currentFreqIndex_;

    for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
        q15_t mag = processGoertzelBlock(samples, GOERTZEL_N, scanCoeffs_[i]);
        if (mag > maxMagnitude) {
            maxMagnitude = mag;
            bestIndex = i;
        }
    }
    measuredFreqIndex_ = bestIndex;

    // Q15 → float konverzió (0-1000 tartomány, mint fldigi-nél)
    float value = static_cast<float>(maxMagnitude) / Q15_SCALE * 1000.0f;

    // --- fldigi envelope tracking algoritmus ---
    if (useAdaptiveThreshold_) {
        // sig_avg követés (decay)
        sig_avg_ = decayavg(sig_avg_, value, decay_weight_);

        // noise_floor követés (attack ha csökken, decay ha nő)
        if (value < sig_avg_) {
            if (value < noise_floor_)
                noise_floor_ = decayavg(noise_floor_, value, attack_weight_);
            else
                noise_floor_ = decayavg(noise_floor_, value, decay_weight_);
        }

        // agc_peak követés (attack ha nő, decay ha csökken)
        if (value > sig_avg_) {
            if (value > agc_peak_)
                agc_peak_ = decayavg(agc_peak_, value, attack_weight_);
            else
                agc_peak_ = decayavg(agc_peak_, value, decay_weight_);
        }

        // Normalizálás agc_peak-hez
        float norm_noise = 0.0f;
        float norm_sig = 0.0f;
        float norm_value = 0.0f;

        if (agc_peak_ > 1e-4f) {
            norm_noise = noise_floor_ / agc_peak_;
            norm_sig = sig_avg_ / agc_peak_;
            norm_value = value / agc_peak_;
        }

        // SNR metric számítás (dB-ben, 0-100 tartomány)
        metric_ = 0.8f * metric_; // decay
        if (noise_floor_ > 1e-4f && noise_floor_ < sig_avg_) {
            float snr_db = 20.0f * log10f(sig_avg_ / noise_floor_);
            metric_ += 0.2f * constrain(2.5f * snr_db, 0.0f, 100.0f);
        }

        // Adaptive threshold számítás (fldigi módszer)
        float diff = norm_sig - norm_noise;
        cw_upper_ = norm_sig - 0.2f * diff;
        cw_lower_ = norm_noise + 0.7f * diff;

        // Tónus detekció hysteresis-szel
        bool rawToneState = false;
        if (!toneDetected_ && norm_value > cw_upper_) {
            rawToneState = true; // Felfutó él
        } else if (toneDetected_ && norm_value < cw_lower_) {
            rawToneState = false; // Lefutó él
        } else {
            rawToneState = toneDetected_; // Tartjuk az előző állapotot
        }

        // Debug kimenet (ritkítva)
        static int __cw_dbg_cnt = 0;
        if (++__cw_dbg_cnt >= 20) {
            CW_DEBUG("CW: val=%.1f, sig=%.1f, noise=%.1f, peak=%.1f, upper=%.3f, lower=%.3f, SNR=%.1fdB, %s\n", value, sig_avg_, noise_floor_, agc_peak_,
                     cw_upper_, cw_lower_, metric_, rawToneState ? "TONE" : "IDLE");
            __cw_dbg_cnt = 0;
        }

        toneDetected_ = rawToneState;

    } else {
        // Fix küszöb mód (régi Q15 módszer - kompatibilitás)
        threshold_q15 = minThreshold_q15;
        bool rawToneState = (maxMagnitude > threshold_q15);

        // Egyszerű debounce
        const uint8_t REQUIRED_CONSECUTIVE = 1;
        if (rawToneState) {
            consecutiveAboveCount_ = std::min<int>(REQUIRED_CONSECUTIVE, consecutiveAboveCount_ + 1);
            consecutiveBelowCount_ = 0;
        } else {
            consecutiveBelowCount_ = std::min<int>(REQUIRED_CONSECUTIVE, consecutiveBelowCount_ + 1);
            consecutiveAboveCount_ = 0;
        }

        bool newToneState = toneDetected_;
        if (!toneDetected_ && consecutiveAboveCount_ >= REQUIRED_CONSECUTIVE) {
            newToneState = true;
        } else if (toneDetected_ && consecutiveBelowCount_ >= REQUIRED_CONSECUTIVE) {
            newToneState = false;
        }

        toneDetected_ = newToneState;
    }

    // Frekvencia követés frissítése állapot változáskor
    if (toneDetected_) {
        updateFrequencyTracking();
        lastGoodToneMs_ = millis();
    }

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
    // A processSamples() által feltöltött csúszó pufferét használjuk
    if (lastSampleCount_ < GOERTZEL_N) {
        return; // Még nincs elég minta
    }

    // Keressük meg a legerősebb frekvenciát (minden index magnitúdójának meghatározása)
    // Q15 fixpoint használat - nincs szükség ablakfüggvényre (direkt int16_t minták)
    q15_t maxMagnitude = 0;
    uint8_t bestIndex = currentFreqIndex_;

    // Ideiglenesen rendezett puffer az utolsó GOERTZEL_N mintához
    int16_t orderedSamples[GOERTZEL_N];
    if (lastSamplePos_ == 0) {
        // Ha a pozíció 0, a teljes puffer lineáris
        memcpy(orderedSamples, lastSamples_, GOERTZEL_N * sizeof(int16_t));
    } else {
        // Körkörös puffer: összefűzzük a farkot és a fejét
        size_t tail = GOERTZEL_N - lastSamplePos_;
        memcpy(orderedSamples, lastSamples_ + lastSamplePos_, tail * sizeof(int16_t));
        memcpy(orderedSamples + tail, lastSamples_, lastSamplePos_ * sizeof(int16_t));
    }

    // Tároljuk az egyes frekvenciaindexek magnitúdóit, hogy összehasonlíthassuk az aktuálissal
    q15_t mags[FREQ_SCAN_STEPS] = {0};
    for (size_t i = 0; i < FREQ_SCAN_STEPS; i++) {
        q15_t magnitude = processGoertzelBlock(orderedSamples, GOERTZEL_N, scanCoeffs_[i]);
        mags[i] = magnitude;
        if (magnitude > maxMagnitude) {
            maxMagnitude = magnitude;
            bestIndex = i;
        }
    }

    // Döntési logika: türelmes váltás. Több feltétel együtt:
    // - Az új frekvencia magnitúdója elég nagy
    // - Nem váltunk azonnal; először jelöltként számoljuk, és ha ez 10 egymás utáni mérésben
    //   (vagy elegendő időn át) fennáll, akkor véglegesítjük a váltást.

    // Q15 küszöb konstansok (statikus értékek Q15 formátumban)
    // CHANGE_TONE_MAG_THRESHOLD = 10.0f → Q15: 10.0f * 32768 / 1000 ≈ 327
    const q15_t MAG_THRESHOLD_Q15 = 327;
    // MAG_RATIO = 1.5x: newMag > curMag * 1.5 → newMag * 2 > curMag * 3
    q15_t newMag = mags[bestIndex];
    q15_t curMag = mags[currentFreqIndex_];
    float freqDiff = fabsf(scanFrequencies_[bestIndex] - scanFrequencies_[currentFreqIndex_]);

    bool meetsBasicCriteria = false;
    if (bestIndex != currentFreqIndex_) {
        // newMag >= MAG_THRESHOLD_Q15 && newMag * 2 > curMag * 3 && freqDiff > CHANGE_TONE_THRESHOLD
        int32_t newMagx2 = (int32_t)newMag * 2;
        int32_t curMagx3 = (int32_t)curMag * 3;
        if (newMag >= MAG_THRESHOLD_Q15 && newMagx2 > curMagx3 && freqDiff > CHANGE_TONE_THRESHOLD) {
            meetsBasicCriteria = true;
        }
    }

    unsigned long now = millis();

    if (meetsBasicCriteria) {
        // Ha ugyanaz a jelölt, növeljük a számlálót
        if (candidateFreqIndex_ == bestIndex) {
            candidateCount_ = std::min<uint8_t>(candidateCount_ + 1, 255);
        } else {
            // Új jelöltet találtunk: inicializáljuk
            candidateFreqIndex_ = bestIndex;
            candidateCount_ = 1;
            candidateFirstSeenMs_ = now;
        }

        // Ha elégszer egymás után mérjük ugyanazt, vagy elég ideje folyamatos, véglegesítjük
        bool timeSatisfied = (now - candidateFirstSeenMs_) >= REQUIRED_DURATION_TO_SWITCH_MS;
        bool countSatisfied = (candidateCount_ >= REQUIRED_CONSECUTIVE_TO_SWITCH);

        if (countSatisfied || timeSatisfied) {
            // Ha van már egy stabil frekvencia és még a tartási időn belül vagyunk, akkor NE váltson
            if (stableHoldUntilMs_ == 0 || now >= stableHoldUntilMs_) {
                // Váltás engedélyezett
                currentFreqIndex_ = candidateFreqIndex_;
                goertzelCoeff_ = scanCoeffs_[currentFreqIndex_];
                // Stabil státusz beállítása: megtartjuk legalább STABLE_HOLD_MS-ig
                stableFreqIndex_ = currentFreqIndex_;
                stableHoldUntilMs_ = now + STABLE_HOLD_MS;
#ifdef __CW_DEBUG
                // Q15 → float debug konverzió
                float newMag_f = (float)newMag / Q15_SCALE;
                float curMag_f = (float)curMag / Q15_SCALE;
                CW_DEBUG("CW-C1: Frekvencia végleges váltás: %.1f Hz (mag[Q15]=%d(%.4f), cur[Q15]=%d(%.4f))\n", //
                         scanFrequencies_[currentFreqIndex_], newMag, newMag_f, curMag, curMag_f);
#endif
            } else {
                CW_DEBUG("CW-C1: Frekvencia váltás elhalasztva, tartási idő alatt: %.1f Hz\n", scanFrequencies_[stableFreqIndex_]);
            }
            // Jelölt reset
            candidateCount_ = 0;
            candidateFreqIndex_ = currentFreqIndex_;
            candidateFirstSeenMs_ = 0;
        }
    } else {
        // Ha nem teljesül a feltétel, töröljük a jelöltet
        candidateCount_ = 0;
        candidateFreqIndex_ = currentFreqIndex_;
        candidateFirstSeenMs_ = 0;
    }

    // Ritka debug: mi történik a váltási logikával
    static int __cw_fs_dbg = 0;
    if (++__cw_fs_dbg >= 500) {
        CW_DEBUG("CW-C1: Váltási állapot: cur=%.1fHz stableUntil=%lu candidate=%d count=%u\n", //
                 scanFrequencies_[currentFreqIndex_], stableHoldUntilMs_, candidateFreqIndex_, candidateCount_);
        __cw_fs_dbg = 0;
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

        // Darabszám-alapú 'nincs tónus' logika:
        // Ha van tónus, reseteljük a nem-tónus számlálót és tároljuk a mért indexet.
        if (tone) {
            noToneConsecutiveCount_ = 0;
            if (freqHistoryCount_ < FREQ_HISTORY_SIZE) {
                freqHistory_[freqHistoryCount_++] = measuredFreqIndex_;
            }
        } else {
            // Nincs tónus ebben a blokkban: növeljük a nem-tónus számlálót (csak információként)
            noToneConsecutiveCount_ = std::min<uint8_t>(noToneConsecutiveCount_ + 1, 255);
        }

        // Ha 1 percig nem volt JÓ tónus, töröljük a publikált frekit és a WPM-et
        if (lastGoodToneMs_ != 0 && (millis() - lastGoodToneMs_) > NO_GOOD_TONE_TIMEOUT_MS) {
            if (::decodedData.cwCurrentFreq != 0 || ::decodedData.cwCurrentWpm != 0) {
                ::decodedData.cwCurrentFreq = 0;
                ::decodedData.cwCurrentWpm = 0;
                lastPublishedFreq_ = 0.0f;
                lastPublishedWpm_ = 0;
                CW_DEBUG("CW-C1: 1 percig nem volt jó tónus - frekvencia és WPM törölve\n");
            }
            // Reseteljük a stabil tartást
            stableHoldUntilMs_ = 0;
            stableFreqIndex_ = currentFreqIndex_;
            // Reseteljük a no-tone számlálót is
            noToneConsecutiveCount_ = 0;
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

            // fldigi noise spike threshold: kiszűrjük a túl rövid jeleket (dot_length / 2)
            if (noise_spike_threshold_ > 0 && duration < noise_spike_threshold_) {
                // CW_DEBUG("CW-C1: Noise spike rejected - duration=%lu ms < threshold=%lu ms\n", duration, noise_spike_threshold_);
                measuring_ = false;
                continue; // Ezt a jelet eldobjuk
            }

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

                // A mért frekvencia publikálása, de tiszteletben tartva a stabil tartási időt
                float newFreq = scanFrequencies_[modeIndex];
                unsigned long now = millis();
                // Ha van aktív stabil tartás és még nem járt le, ne publikáljunk más frekvenciát
                if (stableHoldUntilMs_ != 0 && now < stableHoldUntilMs_) {
                    // Ha a módusz megegyezik a stabilként tárolttal, rendben van
                    if (modeIndex == stableFreqIndex_) {
                        if (newFreq != lastPublishedFreq_) {
                            ::decodedData.cwCurrentFreq = static_cast<uint16_t>(newFreq);
                            lastPublishedFreq_ = newFreq;
                            CW_DEBUG("CW-C1: Freq PUBLISHED (stable): %.1f Hz\n", newFreq);
                        }
                    } else {
                        // Módusz eltér a stabiltól, de mivel tartási idő van, még ne publikáljunk
                        CW_DEBUG("CW-C1: Freq váltás elnyomva (stabil tartás alatt): %.1f -> %.1f Hz\n", scanFrequencies_[stableFreqIndex_], newFreq);
                    }
                } else {
                    // Nincs tartás vagy lejárt: publikáljunk normálisan
                    if (newFreq != lastPublishedFreq_) {
                        ::decodedData.cwCurrentFreq = static_cast<uint16_t>(newFreq);
                        lastPublishedFreq_ = newFreq;
                        CW_DEBUG("CW-C1: Freq PUBLISHED: %.1f Hz\n", newFreq);
                    }
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
    // fldigi update_tracking() - teljes validáció + tracking filter

    // 1. Ratio validáció: dah/dit arány max 4x lehet (fldigi logic)
    if (dah > 4 * dit || dit > 4 * dah) {
        // CW_DEBUG("CW-C1: Invalid ratio - dit=%lu ms, dah=%lu ms (ratio=%.2f)\n", dit, dah, (float)dah / dit);
        return;
    }

    // 2. WPM számítás a two_dots alapján (dit + dah = ~2 dots)
    float two_dots_measured = (float)(dit + dah);

    // 3. Tracking filter alkalmazása (simple low-pass filter mint az fldigi trackingfilter->run())
    if (two_dots_ == 0.0f) {
        two_dots_ = two_dots_measured;
    } else {
        // Exponenciális mozgóátlag (alpha = 0.25 -> gyorsabb követés, ~4 minta átlag)
        two_dots_ = 0.75f * two_dots_ + 0.25f * two_dots_measured;
    }

    // 4. Min/max limitek számítása az aktuális two_dots alapján
    // WPM = 1200 / dot_length_ms, tehát dot_length_ms = 1200 / WPM
    // two_dots = 2 * dot_length, tehát dot_length = two_dots / 2
    float dot_length = two_dots_ / 2.0f;
    float estimated_wpm = 1200.0f / dot_length;

    // fldigi: min_dot = KWPM / 200, max_dash = 3 * KWPM / 5
    // KWPM = dot_length (ms), tehát:
    min_dot_length_ = (unsigned long)(dot_length / 2.0f);         // Minimum fél dot hossz
    max_dash_length_ = (unsigned long)(dot_length * 3.0f * 1.5f); // Maximum 4.5 dot hossz

    // Noise spike threshold: negyed dot hossz (engedékenyebb mint fldigi)
    noise_spike_threshold_ = (unsigned long)(dot_length / 4.0f);

    // 5. WPM limit ellenőrzés (lowerwpm/upperwpm)
    if (estimated_wpm < wpm_lowerlimit_ || estimated_wpm > wpm_upperlimit_) {
        // CW_DEBUG("CW-C1: WPM out of range - estimated=%.1f, range=%u-%u\n", estimated_wpm, wpm_lowerlimit_, wpm_upperlimit_);
        return;
    }

    // 6. Érvényes mérés - frissítjük a referenciát
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

    // CW_DEBUG("CW-C1: Valid pair - dit=%lu ms, dah=%lu ms, two_dots=%.1f, est_wpm=%.1f, ref=%lu ms\n",
    //          dit, dah, two_dots_, estimated_wpm, reference_);
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

    // fldigi tracking filter változók resetése
    two_dots_ = 0.0f;
    min_dot_length_ = 0;
    max_dash_length_ = 0;
    noise_spike_threshold_ = 0;

    // Új türelmes váltási állapotok resetése
    stableFreqIndex_ = 4;
    stableHoldUntilMs_ = 0;
    candidateFreqIndex_ = 4;
    candidateCount_ = 0;
    candidateFirstSeenMs_ = 0;

    initGoertzel();
}
