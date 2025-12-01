/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompSpectrumVis.h                                                                                           *
 * Created Date: 2025.11.08.                                                                                           *
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
 * Last Modified: 2025.11.30, Sunday  05:06:11                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <TFT_eSPI.h>
#include <vector>

#include "UIComponent.h"
#include "decoder_api.h"

// A Core1 által szolgáltatott FFT adat típusa (egész alakban)
// Az FFT magnitúdó értékek int16_t tartományban érkeznek (±32768 körüli skála)

/**
 * @brief Rádió módok
 */
enum class RadioMode { AM = 0, FM = 1 };

/**
 * @brief Spektrum vizualizáció komponens a radio-2 projekt alapján
 */
class UICompSpectrumVis : public UIComponent {

  public:
    /**
     * @brief Megjelenítési módok
     */
    enum class DisplayMode {
        Off = 0,         //
        SpectrumLowRes,  // 1
        SpectrumHighRes, // 2
        Oscilloscope,    // 3
        Envelope,        // 4
        Waterfall,       // 5
        CWWaterfall,     // 6
        CwSnrCurve,      // 7
        RTTYWaterfall,   // 8
        RttySnrCurve     // 9
    };

    /**
     * @brief Hangolási segéd típusok (CW/RTTY)
     */
    enum class TuningAidType : uint8_t {
        OFF_DECODER, // A fő dekóder ki van kapcsolva
        CW_TUNING,
        RTTY_TUNING,
        // Később itt lehetnek más dekóder típusok is
    };

    /**
     * @brief Konstansok
     */
    static constexpr float MAX_DISPLAY_FREQUENCY_AM = 6000.0f;                // AM max frekvencia
    static constexpr float MAX_DISPLAY_FREQUENCY_FM = MAX_AUDIO_FREQUENCY_HZ; // FM max frekvencia

    /**
     * @brief Waterfall színpaletta
     */
    static const uint16_t WATERFALL_COLORS[16];

    /**
     * @brief Konstruktor
     * @param  rect Téglalap határok
     * @param radioMode Rádió mód (AM/FM)
     * @param bandwidthHz Aktuális sávszélesség Hz-ben
     */
    UICompSpectrumVis(const Rect &rect, RadioMode radioMode, uint32_t bandwidthHz);

    /**
     * @brief Destruktor
     */
    ~UICompSpectrumVis();

    // UIComponent interface
    void draw() override;
    bool handleTouch(const TouchEvent &touch) override;

  protected:
    /**
     * @brief Dialog eltűnésekor meghívódik (ősosztályból örökölt)
     */
    void onDialogDismissed() override;

  public:
    /**
     * @brief Keret rajzolása
     */
    void drawFrame();

    /**
     * @brief Módok közötti váltás
     */
    void cycleThroughModes();

    /**
     * @brief Mód betöltése config-ból
     */
    void loadModeFromConfig();

    /**
     * @brief Módkijelző láthatóságának beállítása
     */
    void setModeIndicatorVisible(bool visible);

    /**
     * @brief Ellenőrzi, hogy egy megjelenítési mód elérhető-e az aktuális rádió módban
     * @param mode A vizsgálandó megjelenítési mód
     * @return true ha a mód elérhető, false ha nem
     */
    bool isModeAvailable(DisplayMode mode) const;

    // /**
    //  * @brief Beállítja, hogy a keret rajzolása szükséges-e
    //  * @param drawn true ha a keretet rajzolni kell, false ha nem
    // BandwidthGainConfig definíció a .cpp fájlban (globális scope) szerepel, hogy elkerüljük a többszörös definíciót.
    /**
     * @brief Kényszeríti a frekvencia feliratok újrarajzolását
     */
    inline void refreshFrequencyLabels() {
        // Töröljük a felirat területet és engedélyezzük az újrarajzolást
        flags_.frequencyLabelsDrawn = true;
    }

    /**
     * @brief Frissíti a CW/RTTY hangolási segéd paramétereket
     *
     * Hívja meg ezt a metódust, ha a cwToneFrequencyHz, rttyMarkFrequencyHz
     * vagy rttyShiftHz globális változók értéke megváltozott.
     */
    void updateTuningAidParameters();

    /**
     * @brief lekéri a jelenlegi megjelenítési módot
     * @return A jelenlegi megjelenítési mód
     */
    inline DisplayMode getCurrentMode() { return currentMode_; }

    /**
     * @brief Beállítja a jelenlegi megjelenítési módot
     * @param newdisplayMode A beállítandó megjelenítési mód
     */
    void setCurrentDisplayMode(DisplayMode newdisplayMode);

  private:
    RadioMode radioMode_;
    DisplayMode currentMode_;
    DisplayMode lastRenderedMode_;

    // Bitfield flag-ek (memória optimalizálás: 8 bool → 1 byte)
    struct {
        uint8_t modeIndicatorVisible : 1;
        uint8_t modeIndicatorDrawn : 1;
        uint8_t frequencyLabelsDrawn : 1;
        uint8_t needBorderDrawn : 1;
        uint8_t spriteCreated : 1;
        uint8_t isMutedDrawn : 1;
        uint8_t _unused : 2; // Rezervált jövőbeli használatra
    } flags_;
    uint32_t modeIndicatorHideTime_;
    uint32_t lastTouchTime_;
    uint32_t lastFrameTime_; // FPS limitáláshoz
    uint16_t maxDisplayFrequencyHz_;
    float envelopeLastSmoothedValue_;

    static constexpr uint8_t BAR_GAP_PIXELS = 1; // lowres bar-ok közötti hézag pixelek száma
    static constexpr uint8_t LOW_RES_BANDS = 24; // lowres sávok száma

    // ===== KÖZÖS AGC KONSTANSOK =====
    static constexpr uint32_t AGC_UPDATE_INTERVAL_MS = 500; // Időalapú AGC frissítés (ms)
    static constexpr float AGC_SMOOTH_FACTOR = 0.2f;        // Simítási faktor (közös)
    static constexpr float AGC_MIN_SIGNAL_THRESHOLD = 0.1f; // Minimum jel küszöb (közös)
    static constexpr uint8_t AGC_HISTORY_SIZE = 40;         // History buffer méret (közös) - per-frame history (~30-40 frames)

    // ===== BAR-ALAPÚ AGC (Spektrum módok: LowRes, HighRes) =====
    // Spektrum bar-ok magasságát méri, nem a nyers magnitude-ot
    float barAgcHistory_[AGC_HISTORY_SIZE] = {0}; // Bar max magasságok története
    uint8_t barAgcHistoryIndex_ = 0;              // Circular buffer index
    float barAgcGainFactor_ = 0.02f;              // Bar-alapú gain faktor
    uint32_t barAgcLastUpdateTime_ = 0;           // Utolsó frissítés időpontja
    float barAgcRunningSum_ = 0.0f;               // Futó összeg (O(1) AGC számításhoz)
    uint8_t barAgcValidCount_ = 0;                // Érvényes minták száma a history bufferben

    // ===== MAGNITUDE-ALAPÚ AGC (Jel-alapú módok: Envelope, Waterfall, Oszcilloszkóp) =====
    // Nyers FFT magnitude maximum-ot méri
    float magnitudeAgcHistory_[AGC_HISTORY_SIZE] = {0}; // Magnitude max értékek története
    uint8_t magnitudeAgcHistoryIndex_ = 0;              // Circular buffer index
    float magnitudeAgcGainFactor_ = 0.02f;              // Magnitude-alapú gain faktor
    uint32_t magnitudeAgcLastUpdateTime_ = 0;           // Utolsó frissítés időpontja
    float magnitudeAgcRunningSum_ = 0.0f;               // Futó összeg (O(1) AGC számításhoz)
    uint8_t magnitudeAgcValidCount_ = 0;                // Érvényes minták száma a history bufferben

    // Sprite handling
    TFT_eSprite *sprite_;
    int indicatorFontHeight_;

    // Peak buffer a LowRes módhoz
    int Rpeak_[LOW_RES_BANDS];
    uint8_t bar_height_[LOW_RES_BANDS]; // Bar magasságok csillapításhoz

    // HighRes simítási puffer a képkockák közötti villogás csökkentésére
    std::vector<float> highresSmoothedCols;
    // HighRes időbeli (temporal) simítás mértéke (0.0 = nincs simítás, 1.0 = lefagyasztás)
    static constexpr float HIGHRES_SMOOTH_ALPHA = 0.7f;

    // CW/RTTY hangolási segéd változók
    TuningAidType currentTuningAidType_;
    uint16_t currentTuningAidMinFreqHz_;
    uint16_t currentTuningAidMaxFreqHz_;

    // Envelope és Waterfall buffer - körkörös 1D buffer (optimalizált)
    std::vector<uint8_t> wabuf_; // 1D buffer (width * height)
    uint16_t wabufWriteCol_ = 0; // Körkörös írási pozíció (oszlop index)

    /**
     * @brief Jelenlegi sávszélesség (Hz)
     */
    uint32_t currentBandwidthHz_;

    /**
     * @brief Sprite kezelő függvények
     * @param modeToPrepareForDisplayMode Az a mód, amelyhez a sprite-ot elő kell készíteni.
     */
    void manageSpriteForMode(DisplayMode modeToPrepareForDisplayMode);

    /**
     * @brief Renderelő függvények
     */
    void renderOffMode();
    void renderSpectrumBar(bool isLowRes);
    void renderOscilloscope();
    void renderWaterfall();
    void renderEnvelope();
    void renderSnrCurve();
    void renderModeIndicator();
    void renderFrequencyRangeLabels(uint16_t minDisplayFrequencyHz, uint16_t maxDisplayFrequencyHz);

    void startShowModeIndicator();

    // Általános AGC segédfüggvény (privát, statikus) - összevonja a bar/magnitude AGC logikát
    float calculateAgcGainGeneric(const float *history, uint8_t historySize, float currentGainFactor, float targetValue) const;

    // Cache-elt grafikun és mód (CW/RTTY/FM/AM, stb) típustól függő erősítés dB-ben (bázis érték, AGC korrekció nélkül)
    float cachedGainDb_ = 0.0f;
    float cachedGainLinear_ = 1.0f; // Lineáris gain (előre konvertálva dB-ből, powf kiküszöbölésére)
    int32_t cachedGainScaled_ = 0;  // Gain * 255 (max ~25k, 32-bit nativ muveletek)

    // Zajszűrő/silence detektáláshoz simított RMS értékek
    float oscRmsSmoothed_ = 0.0f; // Oszcilloszkóp zaj/silence detektáláshoz
    float magRmsSmoothed_ = 0.0f; // RMS simítás spektrum/envelope/waterfall célokra

    /**
     * @brief Kiszámolja a rövidtávú RMS-et a magnitude (q15) tömb adott bin tartományán
     */
    float computeMagnitudeRmsMember(const q15_t *data, int startBin, int endBin) const;

    /**
     * @brief Frissíti a belső simított RMS értéket és visszaad egy soft-gain (0..1) faktort
     * @param newRms A legutóbbi RMS érték (nem simított)
     * @param smoothAlpha Simítási faktor (0..1)
     * @param silenceThreshold Küszöb a soft-gainhez
     * @param minGain Minimális lineáris gain csendes állapotban
     * @return Soft gain faktorként alkalmazandó (0..1)
     */
    float updateRmsAndGetSoftGain(float newRms, float smoothAlpha, float silenceThreshold, float minGain);

    /**
     * @brief Spectrum bar függvények
     */
    uint8_t getBandVal(int fft_bin_index, int min_bin_low_res, int num_bins_low_res_range, int total_bands);

    /**
     * @brief CW/RTTY hangolási segéd függvények
     */
    void setTuningAidType(TuningAidType type);
    void renderCwOrRttyTuningAidWaterfall();

    /**
     * @brief Segéd függvények
     */
    uint16_t valueToWaterfallColor(float val, float min_val, float max_val, byte colorProfileIndex);
    uint16_t getGraphHeight() const;
    uint8_t getIndicatorHeight() const;
    uint16_t getEffectiveHeight() const;

    /**
     * @brief Core1 audio adatok kezelése
     */
    bool getCore1SpectrumData(const q15_t **outData, uint16_t *outSize, float *outBinWidth);
    bool getCore1OscilloscopeData(const int16_t **outData, uint16_t *outSampleCount);

    /**
     * @brief Autogain kezelés - REFAKTORÁLT 2 KÜLÖN AGC RENDSZER
     */
    bool isAutoGainMode() const;

    // Bar-alapú AGC (Spektrum módok: LowRes, HighRes)
    void updateBarBasedGain(float currentBarMaxValue);
    float getBarAgcScale(float baseConstant);
    void resetBarAgc();

    // AGC logging helper values (for consolidated debug output)
    float lastBarAgcMaxForLog_ = 0.0f;   // cached latest bar max for logging
    float lastBarAgcGainForLog_ = 1.0f;  // cached latest bar gain for logging
    uint32_t lastAgcSummaryLogTime_ = 0; // last time consolidated AGC log was emitted

    // Magnitude-alapú AGC (Jel-alapú módok: Envelope, Waterfall, Oszcilloszkóp)
    void updateMagnitudeBasedGain(float currentMagnitudeMaxValue);
    float getMagnitudeAgcScale(float baseConstant);
    void resetMagnitudeAgc();

    // AGC gain számítás (külön bar és magnitude verzió)
    float calculateBarAgcGain(const float *history, uint8_t historySize, float currentGainFactor) const;
    float calculateMagnitudeAgcGain(const float *history, uint8_t historySize, float currentGainFactor) const;

    /**
     * @brief Config konverziós függvények
     */
    DisplayMode configValueToDisplayMode(uint8_t configValue);

    /**
     * @brief Kiírja a spektrum terület közepére, hogy "-- Muted --"
     */
    void drawMutedMessage();

    /**
     * @brief Spektrum mód dekódolása szöveggé
     */
    const char *decodeModeToStr();

    /**
     * @brief Core1 FFT paraméterek (frekvencia/méret) beállítása
     */
    void setFftParametersForDisplayMode();

    /**
     * @brief Előre kiszámolt sávszélesség alapú erősítés (dB-ben) cache-elése.
     */
    void computeCachedGain();

    /**
     * @brief Mute állapot kezelése
     * @return true ha a renderelés szünetel, false egyébként
     */
    bool handleMuteState();

    /**
     * @brief Mód indikátor állapotának és láthatóságának kezelése
     */
    void handleModeIndicator();
};
