#pragma once
#include <array>

#include "IDecoder.h"
#include "RingBuffer.h"
#include "arm_math.h"

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

  private:
    // RTTY állapotgépe
    enum RttyState { IDLE, START_BIT, DATA_BITS, STOP_BIT };
    RttyState currentState;

    // Konfiguráció és időzítés
    float markFreq;
    float spaceFreq;
    float baudRate;
    float samplingRate;

    // Tone Detector - kisebb Goertzel blokkokkal
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
    float markEnvelope;  // envelope a mark számára
    float spaceEnvelope; // envelope a space számára
    uint8_t toneBlockAccumulated;
    bool lastToneIsMark;

    // Ha a confidence érték kicsi (pl. < 0.1), akkor a dekóder bizonytalan a döntésben. Ilyenkor eldobhatod vagy figyelmen kívül hagyhatod az adott bitet/blokkot, így kevesebb hibás karakter kerül a kimenetre.
    // Jelminőség kijelzése: A confidence értéket kiírhatod a debug logba vagy a felhasználói felületre, így látható, mennyire jó a dekódolás minősége.
    // Adaptív küszöbölés: Ha a confidence tartósan alacsony, automatikusan módosíthatod a zajküszöböt, AGC-t vagy más paramétereket, hogy javítsd a dekódolás minőségét.
    // Statisztika, hibaarány mérés: Gyűjtheted a confidence értékeket, és ezekből statisztikát készíthetsz a dekódolás megbízhatóságáról, hibaarányról.
    // Soft-decision dekódolás: Ha nem csak bináris döntést hozol, hanem a confidence alapján súlyozottan dekódolsz (pl. FEC, error correction), akkor a confidence értékek segítenek a hibajavításban.
    float lastToneConfidence; // az utolsó detektált tone döntés biztonsága
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