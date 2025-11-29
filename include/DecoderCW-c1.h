/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderCW-c1.h                                                                                                *
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
 * Last Modified: 2025.11.23, Sunday  09:47:37                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once
#include <Arduino.h>

#include "IDecoder.h"
#include "WindowApplier.h"
#include "defines.h"

/**
 * @brief Adaptív CW (Morse kód) dekóder osztály Core1-hez.
 *
 * Goertzel algoritmussal detektálja a CW tónust, adaptívan követi a frekvenciát ±200 Hz tartományban,
 * méri a dit/dah hosszakat és dekódolja a Morse karaktereket bináris fa alapján.
 *
 * Jellemzők:
 * - Adaptív frekvencia követés: ±200 Hz tartomány a célfrekvencia körül (600-1000 Hz)
 * - Adaptív WPM (szavak/perc) tanulás: 5-40 WPM tartomány
 * - Goertzel filter a célfrekvencián
 * - Morse dekódolás bináris fa alapján
 * - Publikálja a detektált frekvenciát és WPM-et
 */
class DecoderCW_C1 : public IDecoder {
  public:
    uint8_t measuredFreqIndex_ = 4; // Utolsó mért legerősebb frekvencia indexe
    DecoderCW_C1();
    ~DecoderCW_C1() override = default;
    const char *getDecoderName() const override { return "CW"; };
    bool start(const DecoderConfig &decoderConfig) override;
    void stop() override;

    // Minták feldolgozása Goertzel algoritmussal és Morse dekódolással
    void processSamples(const int16_t *rawAudioSamples, size_t count) override;

    // Dekóder adaptív küszöb használatának beállítása/lekérdezése
    inline void setUseAdaptiveThreshold(bool use) override {
        useAdaptiveThreshold_ = use;
        if (!use)
            agcInitialized_ = false;
    }
    inline bool getUseAdaptiveThreshold() const override { return useAdaptiveThreshold_; }

  private:
    // --- Konfiguráció ---
    uint32_t samplingRate_; // Mintavételezési sebesség (Hz)
    float targetFreq_;      // Cél frekvencia (Hz)
    const uint16_t minWpm_ = 5;
    const uint16_t maxWpm_ = 40;

    // --- Goertzel filter paraméterek ---
    static constexpr size_t GOERTZEL_N = 48; // Minták száma Goertzel blokkhoz
    q15_t goertzelCoeff_;                    // Goertzel együttható (Q15)
    q15_t threshold_q15;                     // Jelszint küszöb (Q15)

    // --- AGC paraméterek ---
    // Ha true, a dekóder adaptív küszöböt számol (agc-szerű viselkedés)
    // Ha false, csak a `minThreshold_q15` értéket használjuk (fix küszöb)
    bool useAdaptiveThreshold_ = false;

    // AGC runtime paraméterek (Q15 fixpoint)
    // Kezdeti AGC értékek a gyakoribb mért magnitúdókhoz igazítva
    q15_t agcLevel_q15 = 492;          // AGC szint (mozgó átlag) Q15: 15.0f × 32768 / 1000 ≈ 492
    q15_t agcAlpha_q15 = 655;          // AGC szűrési állandó (lassabb követés) Q15: 0.02f × 32768 ≈ 655
    q15_t minThreshold_q15 = 1311;     // Minimális threshold_q15 érték Q15: 40.0f × 32768 / 1000 ≈ 1311
    const float THRESH_FACTOR = 0.80f; // Jelszint küszöbfaktor - nagyobb érték konzervatívabb detektálást eredményez

    // Jelzi, hogy az AGC egyszer már inicializálva lett valódi mérésből
    bool agcInitialized_ = false;

    // --- Frekvencia követés ---
    static constexpr size_t FREQ_SCAN_STEPS = 7; // 7 lépés:  -150, -100, -50, 0, +50, +100, +150 Hz
    static constexpr float FREQ_STEPS[FREQ_SCAN_STEPS] = {-150.0f, -100.0f, -50.0f, 0.0f, 50.0f, 100.0f, 150.0f};
    static constexpr float CHANGE_TONE_THRESHOLD = 70.0f;     // Váltás küszöbértéke
    static constexpr float CHANGE_TONE_MAG_THRESHOLD = 10.0f; // Minimális magnitúdó a frekvia váltáshoz

    // Frekvencia követéshez szükséges adatok
    float scanFrequencies_[FREQ_SCAN_STEPS];
    q15_t scanCoeffs_[FREQ_SCAN_STEPS]; // Goertzel együtthatók (Q15)
    uint8_t currentFreqIndex_;          // Aktuális frekvencia index

    // --- Türelmes váltási szabályok (kezeljük, ha rövid ideig ingadozik a mért frekvencia) ---
    // Ha egyszer stabilnak tekintettük a frekvenciát, tartsa meg legalább 3 percig
    static constexpr unsigned long STABLE_HOLD_MS = 180000UL;               // 3 perc ms-ben
    static constexpr uint8_t REQUIRED_CONSECUTIVE_TO_SWITCH = 10;           // 10 egymás utáni mérés szükséges
    static constexpr unsigned long REQUIRED_DURATION_TO_SWITCH_MS = 5000UL; // vagy 5s folyamatos megfigyelés
    uint8_t stableFreqIndex_ = 4;                                           // Jelenleg „stabilnak” tekintett frekvencia index
    unsigned long stableHoldUntilMs_ = 0;                                   // Meddig kell még tartani a stabil értéket (millis)
    uint8_t candidateFreqIndex_ = 0;                                        // Jelenlegi váltási jelölt index
    uint8_t candidateCount_ = 0;                                            // Hány egymás utáni mérés egyezik a jelölttel
    unsigned long candidateFirstSeenMs_ = 0;                                // Mikor láttuk először a jelöltet
    // Ha nincs tónus X egymást követő mérés, publikáljuk, hogy nincs frekvencia
    static constexpr uint8_t NO_TONE_PUBLISH_COUNT = 10; // ha 10 egymás utáni mérésben nincs tónus -> nincs freki
    uint8_t noToneConsecutiveCount_ = 0;                 // hány egymás utáni blokkban nem észleltünk tónust
    // Ha 1 percig nincs JÓ tónus, akkor töröljük a publikált frekit és WPM-et
    static constexpr unsigned long NO_GOOD_TONE_TIMEOUT_MS = 60000UL; // 1 perc
    unsigned long lastGoodToneMs_ = 0;                                // utolsó JÓ tónus időbélyege

    // --- Jel detekció ---
    bool toneDetected_;              // Aktuálisan észlelt tónus
    unsigned long leadingEdgeTime_;  // Jel felfutó él időbélyege (ms)
    unsigned long trailingEdgeTime_; // Jel lefutó él időbélyege (ms)

    // --- WPM és időzítés ---
    unsigned long startReference_; // Kezdeti referencia érték (ms)
    unsigned long reference_;      // Aktuális referencia (ms) - dit/dah határ
    unsigned long toneMin_;        // Legrövidebb tónus (ms)
    unsigned long toneMax_;        // Leghosszabb tónus (ms)
    unsigned long lastElement_;    // Előző tónus hossza (dit-dah pár validációhoz)
    uint8_t currentWpm_;           // Aktuális WPM

    // --- Dekódolás ---
    static constexpr size_t MAX_TONES = 6; // Max 6 dit/dah egy karakterben
    unsigned long toneDurations_[MAX_TONES];
    uint8_t toneIndex_; // Aktuális tónus index

    // Statisztikák a stabilabb méréshez
    static constexpr size_t WPM_HISTORY_SIZE = 5;
    uint8_t wpmHistory_[WPM_HISTORY_SIZE];
    uint8_t wpmHistoryIndex_;

    static constexpr size_t FREQ_HISTORY_SIZE = 20; // Max tónus blokk egy karakterben
    uint8_t freqHistory_[FREQ_HISTORY_SIZE];
    uint8_t freqHistoryCount_;

    uint8_t lastPublishedWpm_;
    float lastPublishedFreq_;

    /*
      A Morse kód pontokból és vonásokból áll.
       - Egy pont hossza egy egység.
       - Egy vonás hossza három egység.
       - Az azonos betűn belüli jelek közötti szünet egy egység.
       - A betűk közötti szünet három egység.
       - A szavak közötti szünet hét egység.
       - Egy szó ötven egységből áll
       - A PARIS szó pontosan 50 egység
       - 10 WPM (szó/perc) = 10*50 = 500 egység
         (ami megfelel annak, hogy a PARIS szót tízszer küldjük el egy perc alatt)
       - WPM = 1200/pont-hossz (ms)

      ----------------------------------------------
      Pont és vonás hosszok (ms) különböző WPM-eknél
      ----------------------------------------------
      WPM  Pont  Vonás      WPM   Pont  Vonás
      1    1200  3600       11    109   327
      2    600   1800       12    100   300    <=== 200 ms a pont/vonás határ 12 WPM-nél
      3    400   1200       13    92    276
      4    300   900        14    86    257
      5    240   720        15    80    240
      6    200   600        16    75    225
      7    171   514        17    71    211
      8    150   450        18    67    199
      9    133   400        19    63    189
      10   120   360        20    60    180
    */

    static constexpr char morseSymbols_[]{
        ' ', '5', ' ', 'H', ' ',  '4', ' ', 'S', // 0
        ' ', ' ', ' ', 'V', ' ',  '3', ' ', 'I', // 8
        ' ', ' ', ' ', 'F', ' ',  ' ', ' ', 'U', // 16
        '?', ' ', '_', ' ', ' ',  '2', ' ', 'E', // 24
        ' ', '&', ' ', 'L', '"',  ' ', ' ', 'R', // 32
        ' ', '+', '.', ' ', ' ',  ' ', ' ', 'A', // 40
        ' ', ' ', ' ', 'P', '@',  ' ', ' ', 'W', // 48
        ' ', ' ', ' ', 'J', '\'', '1', ' ', ' ', // 56
        ' ', '6', '-', 'B', ' ',  '=', ' ', 'D', // 64
        ' ', '/', ' ', 'X', ' ',  ' ', ' ', 'N', // 72
        ' ', ' ', ' ', 'C', ';',  ' ', '!', 'K', // 80
        ' ', '(', ')', 'Y', ' ',  ' ', ' ', 'T', // 88
        ' ', '7', ' ', 'Z', ' ',  ' ', ',', 'G', // 96
        ' ', ' ', ' ', 'Q', ' ',  ' ', ' ', 'M', // 104
        ':', '8', ' ', ' ', ' ',  ' ', ' ', 'O', // 112
        ' ', '9', ' ', ' ', ' ',  '0', ' ', ' '  // 120
    };

    uint8_t symbolIndex_;  // Aktuális pozíció a bináris fában
    uint8_t symbolOffset_; // Lépésköz a fában
    uint8_t symbolCount_;  // Szimbólumok száma

    // --- Állapotgép ---
    bool started_;   // Dekódolás elindult
    bool measuring_; // Tónus mérés folyamatban

    // --- Segéd függvények ---
    void initGoertzel();
    q15_t calculateGoertzelCoeff(float frequency);
    q15_t processGoertzelBlock(const int16_t *samples, size_t count, q15_t coeff);

    // Hann ablak a Goertzel blokkok számára (alapértelmezett Hann)
    WindowApplier windowApplier;
    bool detectTone(const int16_t *samples, size_t count);
    void updateFrequencyTracking();
    // Csúszó puffer, amely a legutóbbi GOERTZEL_N mintákat tárolja
    int16_t lastSamples_[GOERTZEL_N];
    size_t lastSampleCount_ = 0;
    size_t lastSamplePos_ = 0; // következő írási pozíció a körkörös pufferben
    // Hysteresis / debounce számlálók a zajos jel miatt fellépő flicker csökkentésére
    uint8_t consecutiveAboveCount_ = 0; // hány egymás utáni blokk volt a küszöb fölött
    uint8_t consecutiveBelowCount_ = 0; // hány egymás utáni blokk volt a küszöb alatt
    void processDot();
    void processDash();
    bool decodeSymbol();
    void resetDecoder();
    void calculateWpm(unsigned long letterDuration);
    void updateTracking(unsigned long dit, unsigned long dah);
};