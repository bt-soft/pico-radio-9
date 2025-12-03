/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderRTTY-c1.h                                                                                              *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.22, Saturday  10:10:26                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once
#include <array>

#include "BiquadFilter.h"
#include "IDecoder.h"
#include "RingBuffer.h"
#include "WindowApplier.h"

/**
 * @brief RTTY dekóder Core1-en (working version from samples/)
 */
class DecoderRTTY_C1 : public IDecoder {
  public:
    DecoderRTTY_C1();
    ~DecoderRTTY_C1() override;

    bool start(const DecoderConfig &decoderConfig) override;
    void stop() override;
    const char *getDecoderName() const override { return "RTTY"; }
    void processSamples(const int16_t *samples, size_t count) override;

    // Sávszűrő engedélyezése / tiltása (not used in working version)
    void enableBandpass(bool enabled) override {}

    // Dekóder resetelése
    void reset() override { this->resetDecoder(); }

  private:
    // RTTY állapotgépe
    enum RttyState { IDLE, START_BIT, DATA_BITS, STOP_BIT };
    RttyState currentState;

    // Konfiguráció és időzítés
    float markFreq;
    float spaceFreq;
    float baudRate;
    float samplingRate;

    // Tone Detector - kisebb Goertzel blokkokkal (float)
    struct GoertzelBin {
        float targetFreq;
        float coeff;
        float q1;
        float q2;
        float magnitude;
    };

    static constexpr uint8_t BINS_PER_TONE = 3;

    std::array<GoertzelBin, BINS_PER_TONE> markBins;
    std::array<GoertzelBin, BINS_PER_TONE> spaceBins;
    float markNoiseFloor;
    float spaceNoiseFloor;
    float markEnvelope;
    float spaceEnvelope;
    // RMS-based pre-normalization state
    float inputRmsAccum;
    uint16_t inputRmsCount;
    float inputGain;
    uint8_t toneBlockAccumulated;
    bool lastToneIsMark;
    float lastToneConfidence;

    // Bit Recovery PLL
    static constexpr float PLL_BANDWIDTH = 0.01f;
    static constexpr float PLL_DAMPING = 0.707f;
    static constexpr float PLL_LOOP_GAIN = 1.0f;

    float pllPhase;
    float pllFrequency;
    float pllDPhase;
    float pllAlpha;
    float pllBeta;

    bool pllLocked;
    int pllLockCounter;

    // Bit összegzés és állapot
    int bitsReceived;
    uint8_t currentByte;
    bool figsShift;

    // Debug/diagnosztika
    float lastDominantMagnitude;
    float lastOppositeMagnitude;

    static const char BAUDOT_LTRS_TABLE[32];
    static const char BAUDOT_FIGS_TABLE[32];
    char decodeBaudotCharacter(uint8_t baudotCode);

    void resetDecoder();
    void initializeToneDetector();
    void configureToneBins(float centerFreq, std::array<GoertzelBin, BINS_PER_TONE> &bins);
    void resetGoertzelState();
    void processToneBlock(const int16_t *samples, size_t count);
    bool detectTone(bool &isMark, float &confidence);

    void initializePLL();
    void updatePLL(bool currentTone, bool &bitSample, bool &bitReady);
    void processBit(bool bitValue);
};