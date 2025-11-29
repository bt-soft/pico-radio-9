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
 * @brief RTTY dekóder Core1-en
 */
class DecoderRTTY_C1 : public IDecoder {
  public:
    DecoderRTTY_C1();
    ~DecoderRTTY_C1() override;

    bool start(const DecoderConfig &decoderConfig) override;
    void stop() override;
    const char *getDecoderName() const override { return "RTTY"; }
    void processSamples(const int16_t *samples, size_t count) override;

    // Sávszűrő engedélyezése / tiltása
    void enableBandpass(bool enabled) override { useBandpass_ = enabled; }

    // Dekóder resetelése
    void reset() override { this->resetDecoder(); };

  private:
    // RTTY állapotgépe
    enum RttyState { IDLE, START_BIT, DATA_BITS, STOP_BIT };
    RttyState currentState;

    // Konfiguráció és időzítés
    float markFreq;
    float spaceFreq;
    float baudRate;
    float samplingRate;

    // Tone Detector - kisebb Goertzel blokkokkal (Q15 fixpoint)
    struct GoertzelBin {
        float targetFreq; // Megmarad float (csak referencia)
        q15_t coeff;      // Goertzel együttható (Q15)
        int32_t q1;       // Goertzel állapot (Q15 extended)
        int32_t q2;       // Goertzel állapot (Q15 extended)
        q15_t magnitude;  // Magnitúdó (Q15)
    };

    static constexpr uint8_t BINS_PER_TONE = 3;

    std::array<GoertzelBin, BINS_PER_TONE> markBins;
    std::array<GoertzelBin, BINS_PER_TONE> spaceBins;
    q15_t markNoiseFloor_q15;      // Zajpadló Q15
    q15_t spaceNoiseFloor_q15;     // Zajpadló Q15
    q15_t markEnvelope_q15;        // Envelope a mark számára (Q15)
    q15_t spaceEnvelope_q15;       // Envelope a space számára (Q15)
    uint16_t toneBlockAccumulated; // TONE_BLOCK_SIZE=256, ezért uint16_t kell!
    bool lastToneIsMark;

    // Ha a confidence érték kicsi (pl. < 0.1), akkor a dekóder bizonytalan a döntésben.
    // Ilyenkor el lehet dobni vagy figyelmen kívül hagyni az adott bitet/blokkot,
    // így kevesebb hibás karakter kerül a kimenetre.
    // -- Jelminőség kijelzése: A confidence értéket ki lehet írni a debug logba vagy a felhasználói
    //    felületre, így látható, mennyire jó a dekódolás minősége.
    // -- Adaptív küszöbölés: Ha a confidence tartósan alacsony, automatikusan módosítható a zajküszöb,
    //    az AGC vagy más paraméter, hogy javítható legyen a dekódolás minőségét.
    // -- Statisztika, hibaarány mérés: Gyűjthető a confidence értékek, és ezekből statisztikát lehet készíteni
    //    a dekódolás megbízhatóságáról, hibaarányról.
    // -- Soft-decision dekódolás: Ha nem csak bináris döntés a cél, hanem a confidence alapján súlyozottan történik
    //    a dekódolás (pl. FEC, error correction), akkor a confidence értékek segítenek a hibajavításban.
    //
    q15_t lastToneConfidence_q15; // Az utolsó detektált tone döntés biztonsága (Q15)

    // Hibás vagy bizonytalan blokkok szűrése:
    // Bit Recovery PLL
    static constexpr float PLL_BANDWIDTH = 0.01f; // PLL sávszélesség
    static constexpr float PLL_DAMPING = 0.707f;  // Kritikus csillapítás
    static constexpr float PLL_LOOP_GAIN = 1.0f;

    float pllPhase;     // PLL fázis (0.0 - 1.0, egy bit periódus)
    float pllFrequency; // PLL frekvencia (baud alapú)
    float pllDPhase;    // Fázis inkrement mintánként
    float pllAlpha;     // PLL loop filter koeff
    float pllBeta;      // PLL loop filter koeff

    bool pllLocked;
    int pllLockCounter;

    // Bit összegzés és állapot
    int bitsReceived;
    uint8_t currentByte;
    bool figsShift;

    // Debug/diagnosztika (Q15)
    q15_t lastDominantMagnitude_q15;
    q15_t lastOppositeMagnitude_q15;

    // Windowing helper for Goertzel blocks
    WindowApplier windowApplier;
    bool useWindow_ = false; // Windowing kikapcsolva - elrontja a Q15 Goertzel-t

    // Két külön bandpass szűrő: egy mark-hoz és egy space-hez
    BiquadBandpass markBandpassFilter_;
    BiquadBandpass spaceBandpassFilter_;
    bool useBandpass_ = false; // alapértelmezetten kikapcsolva
    float bandpassMarkBWHz_;   // a start() számolja ki
    float bandpassSpaceBWHz_;  // a start() számolja ki

  private:
    static const char BAUDOT_LTRS_TABLE[32];
    static const char BAUDOT_FIGS_TABLE[32];
    char decodeBaudotCharacter(uint8_t baudotCode);

    void initializeToneDetector();
    void configureToneBins(float centerFreq, std::array<GoertzelBin, BINS_PER_TONE> &bins);
    void resetGoertzelState();
    void processToneBlock(const int16_t *samples, size_t count);
    bool detectTone(bool &isMark, float &confidence);

    void initializePLL();
    void updatePLL(bool currentTone, bool &bitSample, bool &bitReady);
    void processBit(bool bitValue);
    void resetDecoder();
};