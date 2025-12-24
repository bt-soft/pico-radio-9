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
 * Last Modified: 2025.12.24, Wednesday  03:22:08                                                                      *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once
#include <array>

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

    // Bit buffer
    static constexpr int MAX_BIT_BUFFER_SIZE = 512; // max symbollen
    bool bitBuffer[MAX_BIT_BUFFER_SIZE];
    int symbolLen;        // symbollen = TONE_BLOCK_SIZE
    int bitBufferCounter; // counter az rx() state machine-hez

    // AFC (Automatic Frequency Control)
    float freqError;                   // jelenlegi frekvencia hiba (Hz)
    int afcEnabled;                    // AFC engedélyezve (0=ki, 1=lassabb, 2=gyorsabb)
    static constexpr int MAXPIPE = 16; // history buffer méret
    struct cmplx {
        float real;
        float imag;
    };
    cmplx markHistory[16];  // mark tónus complex history
    cmplx spaceHistory[16]; // space tónus complex history
    int historyPtr;         // history pointer

    // Bit összegzés és állapot
    int bitsReceived;
    uint8_t currentByte;
    bool figsShift;
    char lastChar; // duplikált CR/LF szűréshez

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

    // Bit buffer módszerek
    bool isMarkSpaceTransition(int &correction);
    bool isMarkAtCenter();
    bool rxBit(bool bit); // rx() metódus

    // AFC (Automatic Frequency Control)
    void updateAFC(bool charDecoded);
    void reconfigureFrequencies(float newMarkFreq, float newSpaceFreq);
};