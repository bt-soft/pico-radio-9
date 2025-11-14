#pragma once

#include <TFT_eSPI.h>
#include <vector>

#include "UIComponent.h"

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
    static constexpr float MAX_DISPLAY_FREQUENCY_AM = 6000.0f;  // AM max frekvencia
    static constexpr float MAX_DISPLAY_FREQUENCY_FM = 15000.0f; // FM max frekvencia

    /**
     * @brief Waterfall színpaletta
     */
    static const uint16_t WATERFALL_COLORS[16];

    /**
     * @brief Konstruktor
     * @param x X pozíció
     * @param y Y pozíció
     * @param w Szélesség
     * @param h Magasság
     * @param radioMode Rádió mód (AM/FM)
     */
    UICompSpectrumVis(int x, int y, int w, int h, RadioMode radioMode);

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
    //  */
    // inline void setBorderDrawn() { needBorderDrawn = true; }

    /**
     * @brief Frissíti a maximális megjelenítési frekvenciát
     * @param maxDisplayFrequencyHz Az FFT mintavételezési frekvenciája
     */
    inline void setMaxDisplayFrequencyHz(uint16_t maxDisplayFrequencyHz) {
        maxDisplayFrequencyHz_ = maxDisplayFrequencyHz;
        frequencyLabelsDrawn_ = true;
        DEBUG("SpectrumVisualizationComponent::setMaxDisplayFrequencyHz Max display frequency set to %d Hz\n", maxDisplayFrequencyHz_);
    }

    /**
     * @brief Kényszeríti a frekvencia feliratok újrarajzolását
     */
    inline void refreshFrequencyLabels() {
        // Töröljük a felirat területet és engedélyezzük az újrarajzolást
        frequencyLabelsDrawn_ = true;
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
    bool modeIndicatorVisible_;
    bool modeIndicatorDrawn_;   // Flag to avoid redrawing the indicator unnecessarily
    bool frequencyLabelsDrawn_; // Flag to avoid redrawing frequency labels unnecessarily
    bool needBorderDrawn;       // Flag to indicate if the border needs to be redrawn
    uint32_t modeIndicatorHideTime_;
    uint32_t lastTouchTime_;
    uint32_t lastFrameTime_; // FPS limitáláshoz
    uint16_t maxDisplayFrequencyHz_;
    float envelopeLastSmoothedValue_;

    // Frame-alapú adaptív autogain rendszer
    static constexpr int FRAME_HISTORY_SIZE = 16; // Több frame átlagolás a stabilabb működésért
    float frameMaxHistory_[FRAME_HISTORY_SIZE];   // Frame maximum értékek története
    int frameHistoryIndex_;                       // Jelenlegi index a circular bufferben
    bool frameHistoryFull_;                       // Már tele van-e a history buffer
    float adaptiveGainFactor_;                    // Frame-alapú adaptív gain faktor
    uint32_t lastGainUpdateTime_;                 // Utolsó gain frissítés ideje

    // AGC: 5 frame-es buffer a bar maximumhoz
    static constexpr int AGC_BAR_MAX_HISTORY = 5;
    float agcBarMaxHistory_[AGC_BAR_MAX_HISTORY] = {0};
    uint8_t agcBarMaxHistoryIndex_ = 0;

    static constexpr uint32_t GAIN_UPDATE_INTERVAL_MS = 750; // Lassabb frissítés a stabilabb működésért
    static constexpr float TARGET_MAX_UTILIZATION = 0.85f;   // 85%-os maximális kitöltés
    static constexpr float GAIN_SMOOTH_FACTOR = 0.2f;        // Lassabb simítási faktor a stabilabb működésért
    static constexpr float MIN_SIGNAL_THRESHOLD = 0.1f;      // Minimum jel küszöb

    // Sprite handling
    TFT_eSprite *sprite_;
    bool spriteCreated_;
    int indicatorFontHeight_;

    // Peak detection buffer (24 bands max)
    static constexpr int MAX_SPECTRUM_BANDS = 24;
    int Rpeak_[MAX_SPECTRUM_BANDS];
    uint8_t bar_height_[MAX_SPECTRUM_BANDS]; // Bar magasságok csillapításhoz

    // CW/RTTY hangolási segéd változók
    TuningAidType currentTuningAidType_;
    uint16_t currentTuningAidMinFreqHz_;
    uint16_t currentTuningAidMaxFreqHz_;

    // Envelope és Waterfall buffer - egyszerűsített 2D vektor
    std::vector<std::vector<uint8_t>> wabuf;

    /**
     * @brief Sprite kezelő függvények
     * @param modeToPrepareForDisplayMode Az a mód, amelyhez a sprite-ot elő kell készíteni.
     */
    void manageSpriteForMode(DisplayMode modeToPrepareForDisplayMode);

    /**
     * @brief Renderelő függvények
     */
    void renderOffMode();
    void renderSpectrumBar(bool isHighRes);
    void renderOscilloscope();
    void renderWaterfall();
    void renderEnvelope();
    void renderSnrCurve();
    void renderModeIndicator();
    void renderFrequencyRangeLabels(uint16_t minDisplayFrequencyHz, uint16_t maxDisplayFrequencyHz);

    void startShowModeIndicator();

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
     * @brief Interpolált magnitude érték lekérése két FFT bin között
     * @param magnitudeData Az FFT magnitude adatok tömbje
     * @param exactBinIndex Pontos (lebegőpontos) bin index
     * @param minBin Minimum megengedett bin index
     * @param maxBin Maximum megengedett bin index
     * @return Interpolált magnitude érték
     */
    float getInterpolatedMagnitude(const float *magnitudeData, float exactBinIndex, int minBin, int maxBin) const;

    /**
     * @brief Core1 audio adatok kezelése
     */
    bool getCore1SpectrumData(const float **outData, uint16_t *outSize, float *outBinWidth, float *outAutoGain);
    bool getCore1OscilloscopeData(const int16_t **outData, uint16_t *outSampleCount);

    /**
     * @brief Autogain kezelés
     */
    bool isAutoGainMode();

    /**
     * @brief Frame-alapú adaptív autogain rendszer
     */
    void updateFrameBasedGain(float currentFrameMaxValue);
    float getAdaptiveScale(float baseConstant);
    void resetAdaptiveGain();
    float getCurrentGainFactor() const { return adaptiveGainFactor_; }
    float getAverageFrameMax() const;
    float isMutedDrawn;

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
};
