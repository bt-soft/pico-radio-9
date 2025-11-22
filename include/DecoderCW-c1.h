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
 * Last Modified: 2025.11.22, Saturday  04:41:10                                                                       *
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
    float goertzelCoeff_;                    // Goertzel együttható
    float goertzelQ1_;
    float goertzelQ2_;
    float threshold_; // Jelszint küszöb

    // --- AGC paraméterek ---
    // Ha true, a dekóder adaptív küszöböt számol (agc-szerű viselkedés)
    // Ha false, csak a `minThreshold_` értéket használjuk (fix küszöb)
    bool useAdaptiveThreshold_ = false;

    // AGC runtime paraméterek
    // Kezdeti AGC értékek a gyakoribb mért magnitúdókhoz igazítva
    float agcLevel_ = 15.0f;           // AGC szint (mozgó átlag) - kezdeti tipp a mérések alapján (konzervatív)
    float agcAlpha_ = 0.02f;           // AGC szűrési állandó (lassabb követés, kevesebb fluktuáció)
    float minThreshold_ = 8.0f;        // Minimális threshold_ érték
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
    float scanCoeffs_[FREQ_SCAN_STEPS];
    uint8_t currentFreqIndex_; // Aktuális frekvencia index

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
    float calculateGoertzelCoeff(float frequency);
    float processGoertzelBlock(const float *samples, size_t count, float coeff);

    // Hann ablak a Goertzel blokkok számára (alapértelmezett Hann)
    WindowApplier windowApplier;
    bool useWindow_ = true;
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