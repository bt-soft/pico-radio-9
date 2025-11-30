/*
 * Projekt: [pico-radio-9] Raspberry Pi Pico Si4735 rádió                                                              *
 * Fájl: UICompSpectrumVis.cpp                                                                                         *
 * Készítés dátuma: 2025.11.15.                                                                                        *
 *                                                                                                                     *
 * Szerző: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----                                                                                                               *
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * Licenc: MIT                                                                                                         *
 * 	A fájl szabadon használható, módosítható és terjeszthető; beépíthető más projektekbe (akár zártkódúba is).
 * 	Feltétel: a szerző és a licenc feltüntetése a forráskódban.
 * -----                                                                                                               *
 * Utolsó módosítás: 2025.11.30. (Sunday) 07:48:15                                                                     *
 * Módosította: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * VÁLTOZÁSOK (HISTORY):                                                                                               *
 * Dátum		Szerző	Megjegyzés                                                                                         *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "AudioController.h"
#include "Config.h"
#include "UICompSpectrumVis.h"
#include "UIComponent.h"
#include "defines.h"
#include "rtVars.h"
#include "utils.h"

// Ezt tesztre használjuk, hogy a rádió némított állapotát figyelmen kívül hagyjuk (Az AD + FFT hangolásához)
#define TEST_DO_NOT_PROCESS_MUTED_STATE

// UICompSpectrumVis működés debug engedélyezése de csak DEBUG módban
//#define __UISPECTRUM_DEBUG
#if defined(__DEBUG) && defined(__UISPECTRUM_DEBUG)
#define UISPECTRUM_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define UISPECTRUM_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// A grafikon megjelenítési kitöltése
// Itt egyszerre határozzuk meg a vizuális kitöltést és az AGC célját.
static constexpr float GRAPH_TARGET_HEIGHT_UTILIZATION = 0.85f; // grafikon kitöltés / AGC cél (85%)

// Színprofilok
namespace FftDisplayConstants {
/**
 * Waterfall színpaletta RGB565 formátumban
 */
const uint16_t waterFallColors_0[16] = {
    0x000C, // sötétkék háttér (gyenge jel)
    0x001F, // közepes kék
    0x021F, // világoskék
    0x07FF, // cián
    0x07E0, // zöld
    0x5FE0, // világoszöld
    0xFFE0, // sárga
    0xFD20, // narancs
    0xF800, // piros
    0xF81F, // pink
    0xFFFF, // fehér
    0xFFFF, // fehér
    0xFFFF, // fehér
    0xFFFF, // fehér
    0xFFFF, // fehér
    0xFFFF, // fehér
};
// piros árnyalatokkal kezdődő paletta
const uint16_t waterFallColors_1[16] = {0x0000, 0x1000, 0x2000, 0x4000, 0x8000, 0xC000, 0xF800, 0xF8A0,
                                        0xF9C0, 0xFD20, 0xFFE0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
#define WATERFALL_COLOR_INDEX 0

constexpr uint16_t MODE_INDICATOR_VISIBLE_TIMEOUT_MS = 10 * 1000; // A mód indikátor kiírásának láthatósága x másodpercig
constexpr uint8_t SPECTRUM_FPS = 25;                              // FPS limitálás konstans, ez még élvezhető vizualizációt ad, maradjon így 20 FPS-en

}; // namespace FftDisplayConstants

// ===== Integer-alapú segédfüggvények =====
// Cél: minden számítás egész számokkal történjen, minimalizálva a castolásokat
// Q15 alapú segédfüggvények
// Ezek fogadják a Core1-től érkező q15_t értékeket és belül végzik a szükséges konverziókat
// Q15: signed 1.15 fixed-point, typedef q15_t == int16_t

static inline int32_t q15Abs(q15_t v) { return (v < 0) ? -(int32_t)v : (int32_t)v; }

// Q15 -> uint8 (0..255) using a pre-calculated linear gain factor
static inline uint8_t q15ToUint8(q15_t v, float gain_lin) {
    int32_t abs_val = q15Abs(v); // 0..32767
    if (abs_val == 0)
        return 0;

    // Map abs_val to 0..255 range applying linear gain
    float scaled_f = (static_cast<float>(abs_val) * gain_lin) / 32767.0f * 255.0f;
    int32_t scaled = static_cast<int32_t>(scaled_f + 0.5f);
    if (scaled <= 0)
        return 0;
    if (scaled >= 255)
        return 255;
    return static_cast<uint8_t>(scaled);
}

// Q15 -> pixel height (0..max_height) using a pre-calculated linear gain factor
static inline uint16_t q15ToPixelHeight(q15_t v, float gain_lin, uint16_t max_height) {
    int32_t abs_val = q15Abs(v); // 0..32767
    if (abs_val == 0)
        return 0;

    float scaled_f = (static_cast<float>(abs_val) * gain_lin) / 32767.0f * static_cast<float>(max_height);
    int32_t scaled = static_cast<int32_t>(scaled_f + 0.5f);

    if (scaled <= 0)
        return 0;
    if (scaled > max_height)
        return max_height;
    return static_cast<uint16_t>(scaled);
}

// Interpoláció Q15 tömbből: visszaad float-ot (rajzoláshoz). A belső q15->float konverzió itt történik.
static inline float q15InterpolateFloat(const q15_t *data, float exactIndex, int minIdx, int maxIdx) {
    int idx_low = static_cast<int>(std::floor(exactIndex));
    int idx_high = idx_low + 1;
    idx_low = constrain(idx_low, minIdx, maxIdx);
    idx_high = constrain(idx_high, minIdx, maxIdx);
    if (idx_low == idx_high)
        return static_cast<float>(data[idx_low]);

    float frac = exactIndex - idx_low;
    float low = static_cast<float>(data[idx_low]);
    float high = static_cast<float>(data[idx_high]);
    return low * (1.0f - frac) + high * frac;
}

/**
 * @brief Sávszélesség-specifikus scale faktor konfiguráció (MINDEN megjelenítési módhoz)
 *
 * Keskenyebb sávszélesség = kevesebb FFT bin = kisebb összenergia → nagyobb erősítés szükséges
 * Szélesebb sávszélesség = több FFT bin = nagyobb összenergia → kisebb erősítés elegendő
 *
 * MINDEN scale faktor (envelope, waterfall, tuning aid, spectrum bar, oszcilloszkóp)
 * egységesen a táblázatból származik.
 */
struct BandwidthScaleConfig {
    uint32_t bandwidthHz;       // Dekóder sávszélesség (Hz)
    float lowResBarGainDb;      // dB a low-res bar megjelenítéshez (float támogatja a tizedesjegyeket)
    float highResBarGainDb;     // dB a high-res bar megjelenítéshez
    float oscilloscopeGainDb;   // dB az oszcilloszkóphoz
    float envelopeGainDb;       // dB a burkológörbéhez
    float waterfallGainDb;      // dB a vízeséshez
    float tuningAidWaterfallDb; // dB tuning aid waterfall
    float tuningAidSnrCurveDb;  // dB tuning aid SNR curve
};

// Előre definiált gain táblázat (sávszélesség szerint növekvő sorrendben!)
#define NOAMP 0.0f // Nincs erősítés
constexpr BandwidthScaleConfig BANDWIDTH_GAIN_TABLE[] = {
    // bandwidthHz,    lowResBarGainDb, highResBarGainDb, oscilloscopeGainDb, envelopeGainDb, waterfallGainDb,
    // csak CW éls RRTY módban: tuningAidWaterfallDb, tuningAidSnrCurveDb
    {CW_AF_BANDWIDTH_HZ, 6.0f, 5.0f, 3.0f, 4.0f, 3.0f, 4.0f, 5.0f},            // 1.5kHz: CW mód
    {RTTY_AF_BANDWIDTH_HZ, 4.0f, 3.0f, 2.0f, 3.0f, 2.0f, 3.0f, 3.0f},          // 3kHz: RTTY mód
    {AM_AF_BANDWIDTH_HZ, -18.0f, -18.0f, 0.0f, -10.0f, -10.0f, 10.0f, -10.0f}, // 6kHz: AM mód
    {WEFAX_SAMPLE_RATE_HZ, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, -1.0f},       // 11025Hz: WEFAX mód
    {FM_AF_BANDWIDTH_HZ, 20.0f, 25.0f, -3.0f, 18.0f, 22.0f, NOAMP, NOAMP},     // 15kHz: FM mód
};
constexpr size_t BANDWIDTH_GAIN_TABLE_SIZE = ARRAY_ITEM_COUNT(BANDWIDTH_GAIN_TABLE);

//--- AGC konstansok ---
constexpr float AGC_GENERIC_MIN_GAIN_VALUE = 0.0001f;
constexpr float AGC_GENERIC_MAX_GAIN_VALUE = 80.0f;

/**
 * @brief Konstruktor
 * @param  rect Téglalap határok
 * @param radioMode Rádió mód (AM/FM)
 * @param bandwidthHz Aktuális sávszélesség Hz-ben
 */
UICompSpectrumVis::UICompSpectrumVis(const Rect &rect, RadioMode radioMode, uint32_t bandwidthHz)
    : UIComponent(rect),                               //
      radioMode_(radioMode),                           //
      currentBandwidthHz_(bandwidthHz),                //
      currentMode_(DisplayMode::Off),                  //
      lastRenderedMode_(DisplayMode::Off),             //
      modeIndicatorVisible_(false),                    //
      modeIndicatorDrawn_(false),                      //
      frequencyLabelsDrawn_(false),                    //
      needBorderDrawn(true),                           //
      modeIndicatorHideTime_(0),                       //
      lastTouchTime_(0),                               //
      lastFrameTime_(0),                               //
      envelopeLastSmoothedValue_(0.0f),                //
      barAgcGainFactor_(1.0f),                         //
      barAgcLastUpdateTime_(0),                        //
      magnitudeAgcGainFactor_(1.0f),                   //
      magnitudeAgcLastUpdateTime_(0),                  //
      sprite_(nullptr),                                //
      spriteCreated_(false),                           //
      indicatorFontHeight_(0),                         //
      currentTuningAidType_(TuningAidType::CW_TUNING), //
      currentTuningAidMinFreqHz_(0.0f),                //
      currentTuningAidMaxFreqHz_(0.0f),                //
      isMutedDrawn(false)                              //
{

    // Inicializáljuk a cache-elt gain értéket (dB-ben)
    cachedGainDb_ = 0;

    maxDisplayFrequencyHz_ = radioMode_ == RadioMode::AM ? UICompSpectrumVis::MAX_DISPLAY_FREQUENCY_AM : UICompSpectrumVis::MAX_DISPLAY_FREQUENCY_FM;

    // Peak detection buffer inicializálása
    memset(Rpeak_, 0, sizeof(Rpeak_));
    memset(bar_height_, 0, sizeof(bar_height_));

    // AGC history bufferek inicializálása
    memset(barAgcHistory_, 0, sizeof(barAgcHistory_));
    memset(magnitudeAgcHistory_, 0, sizeof(magnitudeAgcHistory_));

    // Waterfall buffer inicializálása
    if (bounds.height > 0 && bounds.width > 0) {
        wabuf.resize(bounds.height, std::vector<uint8_t>(bounds.width, 0));
    }

    // Sprite inicializálása
    sprite_ = new TFT_eSprite(&tft);

    // Sprite előkészítése a kezdeti módhoz
    manageSpriteForMode(currentMode_);

    // Indítsuk el a módok megjelenítését
    startShowModeIndicator();
}

/**
 * @brief Destruktor
 */
UICompSpectrumVis::~UICompSpectrumVis() {
    if (sprite_) {
        sprite_->deleteSprite();
        delete sprite_;
        sprite_ = nullptr;
    }
}

/**
 * @brief Bar-alapú AGC frissítése (Spektrum módok: LowRes, HighRes)
 * @param currentBarMaxValue Jelenlegi frame legnagyobb bar magassága
 */
void UICompSpectrumVis::updateBarBasedGain(float currentBarMaxValue) {

    // Ha némított állapotban vagyunk, az AGC ne működjön!
    if (rtv::muteStat) {
        return;
    }

    // Bar maximum hozzáadása a history bufferhez
    barAgcHistory_[barAgcHistoryIndex_] = currentBarMaxValue;
    barAgcHistoryIndex_ = (barAgcHistoryIndex_ + 1) % AGC_HISTORY_SIZE;

    // Időalapú gain frissítés
    if (Utils::timeHasPassed(barAgcLastUpdateTime_, AGC_UPDATE_INTERVAL_MS)) {
        float newGain = calculateBarAgcGain(barAgcHistory_, AGC_HISTORY_SIZE, barAgcGainFactor_);
        if (newGain > 0.0f) {
            barAgcGainFactor_ = newGain;
        }
        barAgcLastUpdateTime_ = millis();
        // Értékek cache-elése a konszolidált AGC naplózáshoz
        lastBarAgcMaxForLog_ = currentBarMaxValue;
        lastBarAgcGainForLog_ = barAgcGainFactor_;
    }
}

/**
 * @brief Magnitude-alapú AGC frissítése (Jel-alapú módok: Envelope, Waterfall, Oszcilloszkóp)
 * @param currentMagnitudeMaxValue Jelenlegi frame legnagyobb magnitude értéke (nyers FFT adat)
 */
void UICompSpectrumVis::updateMagnitudeBasedGain(float currentMagnitudeMaxValue) {

    // Ha némított állapotban vagyunk, az AGC ne működjön!
    if (rtv::muteStat) {
        return;
    }

    // Magnitude maximum hozzáadása a history bufferhez
    magnitudeAgcHistory_[magnitudeAgcHistoryIndex_] = currentMagnitudeMaxValue;
    magnitudeAgcHistoryIndex_ = (magnitudeAgcHistoryIndex_ + 1) % AGC_HISTORY_SIZE;

    // Időalapú gain frissítés
    if (Utils::timeHasPassed(magnitudeAgcLastUpdateTime_, AGC_UPDATE_INTERVAL_MS)) {
        float newGain = calculateMagnitudeAgcGain(magnitudeAgcHistory_, AGC_HISTORY_SIZE, magnitudeAgcGainFactor_);
        if (newGain > 0.0f) {
            magnitudeAgcGainFactor_ = newGain;
        }
        magnitudeAgcLastUpdateTime_ = millis();
    }
}

/**
 * @brief Bar-alapú AGC gain számítás (pixel magasság alapú)
 * @param history History buffer (bar magasságok pixelben)
 * @param historySize History buffer mérete
 * @param currentGainFactor Jelenlegi gain faktor
 * @return Új gain faktor
 */
float UICompSpectrumVis::calculateBarAgcGain(const float *history, uint8_t historySize, float currentGainFactor) const {
    float targetHeight = static_cast<float>(getGraphHeight()) * GRAPH_TARGET_HEIGHT_UTILIZATION;
    return calculateAgcGainGeneric(history, historySize, currentGainFactor, targetHeight);
}

/**
 * @brief Magnitude-alapú AGC gain számítás (nyers FFT érték alapú)
 * @param history History buffer (magnitude értékek)
 * @param historySize History buffer mérete
 * @param currentGainFactor Jelenlegi gain faktor
 * @return Új gain faktor
 */
float UICompSpectrumVis::calculateMagnitudeAgcGain(const float *history, uint8_t historySize, float currentGainFactor) const {
    // A célérték a grafikon megjelenítési magasságának százaléka (pixelekben)
    float targetHeight = static_cast<float>(getGraphHeight()) * GRAPH_TARGET_HEIGHT_UTILIZATION;
    return calculateAgcGainGeneric(history, historySize, currentGainFactor, targetHeight);
}

//
/**
 * @brief Általános AGC gain számítás, mindkét AGC típushoz
 * @param history History buffer (bar magasságok pixelben vagy magnitude értékek
 * @param historySize History buffer mérete
 * @param currentGainFactor Jelenlegi gain faktor
 * @param targetValue Célérték (pixel magasság vagy magnitude érték)
 * @return Új gain faktor
 */
float UICompSpectrumVis::calculateAgcGainGeneric(const float *history, uint8_t historySize, float currentGainFactor, float targetValue) const {

    // History átlag számítása (stabil érték több frame alapján)
    float sum = 0.0f;
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < historySize; ++i) {
        if (history[i] > AGC_MIN_SIGNAL_THRESHOLD) {
            sum += history[i];
            validCount++;
        }
    }

    if (validCount == 0) {
        return currentGainFactor; // Nincs elég jel, megtartjuk a jelenlegi gain-t
    }

    float averageMax = sum / validCount;
    float idealGain = targetValue / averageMax;

    // Simított átmenet az új gain-hez
    float newGainSuggested = AGC_SMOOTH_FACTOR * idealGain + (1.0f - AGC_SMOOTH_FACTOR) * currentGainFactor;

    // Biztonsági korlátok
    float newgainLimited = constrain(newGainSuggested, AGC_GENERIC_MIN_GAIN_VALUE, AGC_GENERIC_MAX_GAIN_VALUE);
#ifdef __DEBUG
    if (this->isAutoGainMode()) {
        static long lastAgcGeneritLogTime = 0;
        if (Utils::timeHasPassed(lastAgcGeneritLogTime, 2000)) {
            UISPECTRUM_DEBUG("UICompSpectrumVis [AGC Generic]: averageMax=%.4f idealGain=%.4f newGainSuggested=%.4f (min=%.4f max=%.4f), newgainLimited=%.3f\n",
                             averageMax, idealGain, newGainSuggested, AGC_GENERIC_MIN_GAIN_VALUE, AGC_GENERIC_MAX_GAIN_VALUE, newgainLimited);
            lastAgcGeneritLogTime = millis();
        }
    }
#endif

    return newgainLimited;
}

/**
 * @brief Bar-alapú AGC scale lekérése
 * @param baseConstant Alap érzékenységi konstans
 * @return Skálázási faktor
 */
float UICompSpectrumVis::getBarAgcScale(float baseConstant) {
    if (isAutoGainMode()) {
        return baseConstant * barAgcGainFactor_;
    }

    // Manual gain mód: dB -> lineáris konverzió
    int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float gainDb = static_cast<float>(gainCfg);
    float gainLinear = powf(10.0f, gainDb / 20.0f);
    return baseConstant * gainLinear;
}

/**
 * @brief Magnitude-alapú AGC scale lekérése
 * @param baseConstant Alap érzékenységi konstans
 * @return Skálázási faktor
 */
float UICompSpectrumVis::getMagnitudeAgcScale(float baseConstant) {
    if (isAutoGainMode()) {
        return baseConstant * magnitudeAgcGainFactor_;
    }

    // Manual gain mód: dB -> lineáris konverzió
    int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float gainDb = static_cast<float>(gainCfg);
    float gainLinear = powf(10.0f, gainDb / 20.0f);
    return baseConstant * gainLinear;
}

/**
 * @brief Bar-alapú AGC reset
 */
void UICompSpectrumVis::resetBarAgc() {
    memset(barAgcHistory_, 0, sizeof(barAgcHistory_));
    barAgcHistoryIndex_ = 0;
    barAgcGainFactor_ = 1.0f;
    barAgcLastUpdateTime_ = 0;
}

/**
 * @brief Magnitude-alapú AGC reset
 */
void UICompSpectrumVis::resetMagnitudeAgc() {
    memset(magnitudeAgcHistory_, 0, sizeof(magnitudeAgcHistory_));
    magnitudeAgcHistoryIndex_ = 0;
    magnitudeAgcGainFactor_ = 1.0f;
    magnitudeAgcLastUpdateTime_ = 0;
}

/**
 * @brief Ellenőrzi, hogy auto gain módban vagyunk-e
 * @return True, ha auto gain mód aktív
 */
bool UICompSpectrumVis::isAutoGainMode() const {
    int8_t currentGainConfig = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    return (currentGainConfig == SPECTRUM_GAIN_MODE_AUTO); // -128 = Auto Gain (speciális érték)
}

/**
 * @brief Config értékek konvertálása
 */
UICompSpectrumVis::DisplayMode UICompSpectrumVis::configValueToDisplayMode(uint8_t configValue) {
    if (configValue <= static_cast<uint8_t>(DisplayMode::RttySnrCurve)) {
        return static_cast<DisplayMode>(configValue);
    }
    return DisplayMode::Off;
}

/**
 * @brief Dialog eltűnésekor meghívódó metódus
 */
void UICompSpectrumVis::onDialogDismissed() {
    // Dialog épp eltűnt - újra kell rajzolni a keretet és a frekvencia feliratokat is
    needBorderDrawn = true;           // Rajzoljuk újra a keretet
    frequencyLabelsDrawn_ = true;     // Frissítsük a frekvencia feliratokat is
    UIComponent::onDialogDismissed(); // Hívjuk meg az ősosztály implementációját (markForRedraw)
}

/**
 * @brief Handles the mute state, drawing a message if muted.
 * @return true if rendering should be halted due to mute, false otherwise.
 */
bool UICompSpectrumVis::handleMuteState() {
#if not defined TEST_DO_NOT_PROCESS_MUTED_STATE
    if (rtv::muteStat) {
        if (!isMutedDrawn) {
            drawFrame();
            drawMutedMessage();
            isMutedDrawn = true;
        }
        return true; // Halt further rendering
    }

    if (isMutedDrawn) {
        isMutedDrawn = false;
        needBorderDrawn = true; // Redraw frame after unmuting
    }
#endif
    return false; // Continue rendering
}

/**
 * @brief Handles the visibility and timing of the mode indicator display.
 */
void UICompSpectrumVis::handleModeIndicator() {
    // Show indicator if it's supposed to be visible and hasn't been drawn yet
    if (modeIndicatorVisible_ && !modeIndicatorDrawn_) {
        renderModeIndicator();
        modeIndicatorDrawn_ = true;
    }

    // Hide indicator after timeout
    if (modeIndicatorVisible_ && millis() > modeIndicatorHideTime_) {
        modeIndicatorVisible_ = false;
        modeIndicatorDrawn_ = false;

        // Clear the area where the indicator was
        int indicatorY = bounds.y + bounds.height;
        tft.fillRect(bounds.x - 3, indicatorY, bounds.width + 6, 12, TFT_BLACK);

        // Allow frequency labels to be redrawn
        frequencyLabelsDrawn_ = true;
    }
}


/**
 * @brief UIComponent draw implementáció
 */
void UICompSpectrumVis::draw() {

    // Ha van aktív dialog a képernyőn, ne rajzoljunk semmit
    if (isCurrentScreenDialogActive()) {
        return;
    }

    // FPS limitálás
    constexpr uint32_t FRAME_TIME_MS = 1000 / FftDisplayConstants::SPECTRUM_FPS;
    if (!Utils::timeHasPassed(lastFrameTime_, FRAME_TIME_MS)) {
        return;
    }
    lastFrameTime_ = millis();

#ifdef __DEBUG
    //  AGC naplózás időzítése
    if (isAutoGainMode() && Utils::timeHasPassed(lastAgcSummaryLogTime_, 2000)) {
        if (currentMode_ == DisplayMode::SpectrumLowRes || currentMode_ == DisplayMode::SpectrumHighRes) {
            UISPECTRUM_DEBUG("UICompSpectrumVis [Bar AGC]: mód=%d barGain=%.3f jelenlegiBarMax=%.1f\n", (int)currentMode_, lastBarAgcGainForLog_,
                             lastBarAgcMaxForLog_);
        } else if (currentMode_ != DisplayMode::Off) {
            UISPECTRUM_DEBUG("UICompSpectrumVis [Magnitude AGC]: mód=%d magnitudeAgcGainFactor_=%.3f\n", (int)currentMode_, magnitudeAgcGainFactor_);
        }
        lastAgcSummaryLogTime_ = millis();
    }
#endif

    if (handleMuteState()) {
        return; // Stop rendering if muted
    }

    if (needBorderDrawn) {
        drawFrame();
        needBorderDrawn = false;
    }

    // Biztonsági ellenőrzés: FM módban CW/RTTY és az SNR hangolássegéd módok nem engedélyezettek
    if (radioMode_ == RadioMode::FM && (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall ||
                                        currentMode_ == DisplayMode::CwSnrCurve || currentMode_ == DisplayMode::RttySnrCurve)) {
        setCurrentDisplayMode(DisplayMode::Waterfall); // Automatikus váltás Waterfall módra
    }

    // Renderelés módjának megfelelően
    switch (currentMode_) {
    case DisplayMode::SpectrumLowRes:
        renderSpectrumBar(true);
        break;
    case DisplayMode::SpectrumHighRes:
        renderSpectrumBar(false);
        break;
    case DisplayMode::Oscilloscope:
        renderOscilloscope();
        break;
    case DisplayMode::Envelope:
        renderEnvelope();
        break;
    case DisplayMode::Waterfall:
        renderWaterfall();
        break;
    case DisplayMode::CWWaterfall:
    case DisplayMode::RTTYWaterfall:
        renderCwOrRttyTuningAidWaterfall();
        break;

    case DisplayMode::CwSnrCurve:
    case DisplayMode::RttySnrCurve:
        renderSnrCurve();
        break;
    case DisplayMode::Off:
        renderOffMode();
        break;
    }

    // Mode indicator kezelése
    handleModeIndicator();

    // Ha változott a kijelzési mód, töröljük a waterfall felső feliratát
    if (lastRenderedMode_ != currentMode_) {
        if (lastRenderedMode_ == DisplayMode::Waterfall) {
            tft.fillRect(bounds.x, bounds.y - 18, bounds.width, 15, TFT_BLACK);
        }
        lastRenderedMode_ = currentMode_;
    }
}

/**
 * @brief Touch kezelés
 */
bool UICompSpectrumVis::handleTouch(const TouchEvent &touch) {
    if (touch.pressed && isPointInside(touch.x, touch.y)) {
        if (!Utils::timeHasPassed(lastTouchTime_, 500)) {
            return false;
        }
        lastTouchTime_ = millis();

        cycleThroughModes();

        // Csippantunk egyet, ha az engedélyezve van
        if (config.data.beeperEnabled) {
            Utils::beepTick();
        }

        return true;
    }
    return false;
}

/**
 * @brief Keret rajzolása
 */
void UICompSpectrumVis::drawFrame() {

    // Belső terület kitöltése feketével - MINDEN border-től 1 pixellel beljebb
    // A külső keret bounds.x-1, bounds.y-1 pozícióban van, ezért a fillRect
    // bounds.x, bounds.y koordinátától kezdődik, de minden oldalon 1 pixellel kevesebb
    tft.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, TFT_BLACK);

    // Teljes külső keret rajzolása (1 pixel vastag, minden oldalon)
    tft.drawRect(bounds.x - 1, bounds.y - 2, bounds.width + 3, bounds.height + 2, TFT_DARKGREY);
}

/**
 * @brief Kezeli a sprite létrehozását/törlését a megadott módhoz
 * @param modeToPrepareForDisplayMode Az a mód, amelyhez a sprite-ot elő kell készíteni.
 */
void UICompSpectrumVis::manageSpriteForMode(DisplayMode modeToPrepareForDisplayMode) {

    UISPECTRUM_DEBUG("UICompSpectrumVis::manageSpriteForMode() - modeToPrepareFor=%d\n", static_cast<int>(modeToPrepareForDisplayMode));

    if (spriteCreated_) { // Ha létezik sprite egy korábbi módból
        sprite_->deleteSprite();
        spriteCreated_ = false;
    }

    // Sprite használata MINDEN módhoz (kivéve Off)
    if (modeToPrepareForDisplayMode != DisplayMode::Off) {
        uint16_t graphH = getGraphHeight();
        if (bounds.width > 0 && graphH > 0) {
            sprite_->setColorDepth(16); // RGB565
            spriteCreated_ = sprite_->createSprite(bounds.width, graphH);
            if (spriteCreated_) {
                sprite_->fillSprite(TFT_BLACK); // Kezdeti törlés
                UISPECTRUM_DEBUG("UICompSpectrumVis: Sprite létrehozva, méret: %dx%d, bounds.width=%d\n", sprite_->width(), sprite_->height(), bounds.width);
            } else {
                UISPECTRUM_DEBUG("UICompSpectrumVis: Sprite létrehozása sikertelen, mód: %d (szélesség:%d, grafikon magasság:%d)\n",
                                 static_cast<int>(modeToPrepareForDisplayMode), bounds.width, graphH);
            }
        }
    }

    // LowRes Bar magasságok nullázása módváltáskor
    for (uint8_t i = 0; i < LOW_RES_BANDS; ++i) {
        bar_height_[i] = 0;
    }

    // Teljes terület törlése mód váltáskor az előző grafikon eltávolításához
    if (modeToPrepareForDisplayMode != lastRenderedMode_) {

        // Csak a belső területet töröljük, de az alsó bordert meghagyjuk
        tft.fillRect(bounds.x, bounds.y, bounds.width, bounds.height - 1, TFT_BLACK);

        // Frekvencia feliratok területének törlése - CSAK a component szélességében
        tft.fillRect(bounds.x, bounds.y + bounds.height + 1, bounds.width, 10, TFT_BLACK);

        // Sprite is törlése ha létezett
        if (spriteCreated_) {
            sprite_->fillSprite(TFT_BLACK);
        }

        // Envelope reset mód váltáskor
        if (modeToPrepareForDisplayMode == DisplayMode::Envelope) {
            envelopeLastSmoothedValue_ = 0.0f; // Simított érték nullázása
            // Wabuf buffer teljes törlése hogy tiszta vonallal kezdődjön az envelope
            for (auto &row : wabuf) {
                std::fill(row.begin(), row.end(), 0);
            }
        }
    }
}

/**
 * @brief Grafikon magasságának számítása (teljes keret magasság)
 */
uint16_t UICompSpectrumVis::getGraphHeight() const {
    return bounds.height - 1; // 1 pixel eltávolítása az alsó border megőrzéséhez
}

/**
 * @brief Indicator magasságának számítása
 */
uint8_t UICompSpectrumVis::getIndicatorHeight() const { return modeIndicatorVisible_ ? 10 : 0; }

/**
 * @brief Hatékony magasság számítása (grafikon + indicator)
 */
uint16_t UICompSpectrumVis::getEffectiveHeight() const {
    return bounds.height + getIndicatorHeight(); // Keret + indicator alatta
}

/**
 * @brief FFT paraméterek beállítása az aktuális módhoz
 */
void UICompSpectrumVis::setFftParametersForDisplayMode() {

    UISPECTRUM_DEBUG("UICompSpectrumVis::setFftParametersForDisplayMode() - currentMode_=%d\n", static_cast<int>(currentMode_));

    if (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::CwSnrCurve) {
        // CW módok esetén a CW tuning aid használata
        setTuningAidType(TuningAidType::CW_TUNING);
    } else if (currentMode_ == DisplayMode::RTTYWaterfall || currentMode_ == DisplayMode::RttySnrCurve) {
        // RTTY módok esetén a RTTY tuning aid használata
        setTuningAidType(TuningAidType::RTTY_TUNING);
    }

    // Megosztott adatok elérhetőségének ellenőrzése
    if (::activeSharedDataIndex < 0) {
        return;
    }

    // Lekérdezzük mindkét puffer referenciáját
    const int8_t backIdx = 1 - ::activeSharedDataIndex;
    const SharedData &sdActive = ::sharedData[::activeSharedDataIndex];
    const SharedData &sdBack = ::sharedData[backIdx];

    // Előnyben részesítjük a hátsó puffert, ha nem nulla nyomokat tartalmaz (frissen írva a Core1 által)
    const SharedData *sdToUse = nullptr;
    if (sdBack.displayMinFreqHz != 0 || sdBack.displayMaxFreqHz != 0) {
        sdToUse = &sdBack;
    } else if (sdActive.displayMinFreqHz != 0 || sdActive.displayMaxFreqHz != 0) {
        sdToUse = &sdActive;
    }

    if (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall || currentMode_ == DisplayMode::CwSnrCurve ||
        currentMode_ == DisplayMode::RttySnrCurve) {
        // Tuning aid mód: mindig a tuning aid által számolt tartományt használjuk
        maxDisplayFrequencyHz_ = currentTuningAidMaxFreqHz_;
        // (A min frekvencia a rajzolásnál, bin index számításnál lesz figyelembe véve)
        frequencyLabelsDrawn_ = true;
    } else if (sdToUse != nullptr) {
        // Egyéb módokban a SharedData alapján
        uint16_t minHz = sdToUse->displayMinFreqHz ? sdToUse->displayMinFreqHz : MIN_AUDIO_FREQUENCY_HZ;
        uint16_t maxHz = sdToUse->displayMaxFreqHz ? sdToUse->displayMaxFreqHz : static_cast<uint16_t>(maxDisplayFrequencyHz_);
        if (maxHz < minHz + 100) {
            maxHz = minHz + 100;
        }
        maxDisplayFrequencyHz_ = maxHz;
        frequencyLabelsDrawn_ = true;
    }
}

/**
 * @brief Módok közötti váltás
 */
void UICompSpectrumVis::cycleThroughModes() {

    uint8_t nextMode = static_cast<uint8_t>(currentMode_) + 1;

    // FM módban kihagyjuk a CW, RTTY és SNR hangolási segéd módokat
    if (radioMode_ == RadioMode::FM) {
        if (nextMode > static_cast<uint8_t>(DisplayMode::Waterfall)) {
            nextMode = static_cast<uint8_t>(DisplayMode::Off);
        }
    } else {
        // AM módban minden mód elérhető
        if (nextMode > static_cast<uint8_t>(DisplayMode::RttySnrCurve)) {
            nextMode = static_cast<uint8_t>(DisplayMode::Off);
        }
    }

    // Új mód beállítása
    setCurrentDisplayMode(static_cast<DisplayMode>(nextMode));

    // Config-ba is beállítjuk (mentésre) az aktuális módot
    config.data.audioModeAM = nextMode;

    // AGC reset mód váltáskor
    resetMagnitudeAgc();
    resetBarAgc();
}

/**
 * @brief Módok megjelenítésének indítása
 */
void UICompSpectrumVis::startShowModeIndicator() {
    // Mode indicator megjelenítése 20 másodpercig
    modeIndicatorVisible_ = true;
    modeIndicatorDrawn_ = false; // A flag visszaállítása, hogy az indikátor azonnal megjelenhessen
    needBorderDrawn = true;      // Kényszerítjük a keret újrarajzolását

    // Indikátor eltüntetésének időzítése
    modeIndicatorHideTime_ = millis() + FftDisplayConstants::MODE_INDICATOR_VISIBLE_TIMEOUT_MS; // 20 másodpercig látható
}

/**
 * @brief AM/FM Mód betöltése config-ból
 */
void UICompSpectrumVis::loadModeFromConfig() {

    UISPECTRUM_DEBUG("UICompSpectrumVis::loadModeFromConfig() - radioMode_=%d\n", static_cast<int>(radioMode_));

    // Config-ból betöltjük az aktuális rádió módnak megfelelő audio módot
    uint8_t configValue = radioMode_ == RadioMode::FM ? config.data.audioModeFM : config.data.audioModeAM;
    DisplayMode configDisplayMode = configValueToDisplayMode(configValue);

    // FM módban CW/RTTY módok nem engedélyezettek
    if (radioMode_ == RadioMode::FM && (configDisplayMode == DisplayMode::CWWaterfall || configDisplayMode == DisplayMode::RTTYWaterfall)) {
        configDisplayMode = DisplayMode::Waterfall; // Alapértelmezés FM módban
    }

    // TODO: HIBA - Még nem találtam meg a hibát (2025.11). Ha a config-ba SNR Curve van lementve AM módban,
    // akkor a rendszer induláskor lefagy. Ezért AM módban, ha SNR Curve van beállítva, visszaállunk Spectrum LowRes módra
    // hogy legalább elinduljon az eszköz. A pontos hiba a renderSnrCurve metódusban található, ezt később javítani kell.
    if (radioMode_ == RadioMode::AM && (configDisplayMode == DisplayMode::CwSnrCurve || configDisplayMode == DisplayMode::RttySnrCurve)) {
        configDisplayMode = DisplayMode::SpectrumLowRes;
    }

    // Beállítjuk a betöltött módot a config alapján
    setCurrentDisplayMode(configDisplayMode);
}

/**
 * @brief Beállítja a jelenlegi megjelenítési módot
 * @param newdisplayMode A beállítandó megjelenítési mód
 */
void UICompSpectrumVis::setCurrentDisplayMode(DisplayMode newdisplayMode) {

    UISPECTRUM_DEBUG("UICompSpectrumVis::setCurrentDisplayMode() - newdisplayMode=%d\n", static_cast<int>(newdisplayMode));

    // Előző mód megőrzése a tisztításhoz
    lastRenderedMode_ = currentMode_;
    currentMode_ = newdisplayMode;

    // FFT paraméterek beállítása az új módhoz
    setFftParametersForDisplayMode();

    // Mód indikátor indítása
    startShowModeIndicator();

    // Sprite előkészítése az új módhoz
    manageSpriteForMode(currentMode_);

    // Számoljuk ki egyszer a sávszélesség alapú erősítést (dB-ben) és cache-eljük
    computeCachedGain();
}

/**
 * @brief Egyszeri számítás a sávszélesség alapú erősítésre, cache-eli az eredményt.
 * Ezt a metódust a módváltáskor hívjuk meg, így a render ciklusok gyorsabbak lesznek.
 */
// Segéd: a cache-elt dB értéket renderelésbarát százalékértékre konvertálja
// NINCS külön cached-percent segéd: a render kód közvetlenül a dB-t használja

void UICompSpectrumVis::computeCachedGain() {

    // Milyen üzemmódban vagyunk?
    bool forLowResBar = (currentMode_ == DisplayMode::SpectrumLowRes //
                         || currentMode_ == DisplayMode::Off);
    bool forHighResBar = (currentMode_ == DisplayMode::SpectrumHighRes);
    bool forOscilloscope = (currentMode_ == DisplayMode::Oscilloscope);
    bool forEnvelope = (currentMode_ == DisplayMode::Envelope      //
                        || currentMode_ == DisplayMode::CwSnrCurve //
                        || currentMode_ == DisplayMode::RttySnrCurve);
    bool forWaterfall = (currentMode_ == DisplayMode::Waterfall      //
                         || currentMode_ == DisplayMode::CWWaterfall //
                         || currentMode_ == DisplayMode::RTTYWaterfall);
    bool forTuningAid = (currentMode_ == DisplayMode::CWWaterfall      //
                         || currentMode_ == DisplayMode::RTTYWaterfall //
                         || currentMode_ == DisplayMode::CwSnrCurve    //
                         || currentMode_ == DisplayMode::RttySnrCurve);
    bool forSnrCurve = (currentMode_ == DisplayMode::CwSnrCurve //
                        || currentMode_ == DisplayMode::RttySnrCurve);

    // Lambda a megfelelő scale mező kiválasztásához (dB érték visszaadása)
    auto getDbFromTable = [forEnvelope, forWaterfall, forTuningAid, forSnrCurve, forLowResBar, forHighResBar,
                           forOscilloscope](const BandwidthScaleConfig &cfg) -> float {
        if (forEnvelope)
            return cfg.envelopeGainDb;
        if (forWaterfall)
            return cfg.waterfallGainDb;
        if (forTuningAid && forSnrCurve)
            return cfg.tuningAidSnrCurveDb;
        if (forTuningAid)
            return cfg.tuningAidWaterfallDb;
        if (forLowResBar)
            return cfg.lowResBarGainDb;
        if (forHighResBar)
            return cfg.highResBarGainDb;
        if (forOscilloscope)
            return cfg.oscilloscopeGainDb;
        return cfg.envelopeGainDb;
    };

    // 1. Pontos egyezés kell a táblázatban
    for (size_t i = 0; i < BANDWIDTH_GAIN_TABLE_SIZE; ++i) {
        if (BANDWIDTH_GAIN_TABLE[i].bandwidthHz == this->currentBandwidthHz_) {
            cachedGainDb_ = getDbFromTable(BANDWIDTH_GAIN_TABLE[i]);
            return;
        }
    }

    Utils::beepError(); // Hibára figyelmeztető csippanás
    DEBUG("UICompSpectrumVis::computeCachedGain() - Nincs pontos egyezés a sávszélesség táblázatban, interpoláció szükséges. currentBandwidthHz_=%d\n",
          this->currentBandwidthHz_);
}

/**
 * @brief Módkijelző láthatóságának beállítása
 */
void UICompSpectrumVis::setModeIndicatorVisible(bool visible) {
    modeIndicatorVisible_ = visible;
    modeIndicatorDrawn_ = false; // A rajzolási flag visszaállítása, ha megváltozik a láthatóság
    if (visible) {
        modeIndicatorHideTime_ = millis() + 20000; // 20 másodperc
    }
}

/**
 * @brief Ellenőrzi, hogy egy megjelenítési mód elérhető-e az aktuális rádió módban
 */
bool UICompSpectrumVis::isModeAvailable(DisplayMode mode) const {
    // FM módban CW és RTTY hangolási segéd módok nem elérhetők
    if (radioMode_ == RadioMode::FM && (mode == DisplayMode::CWWaterfall || mode == DisplayMode::RTTYWaterfall)) {
        return false;
    }

    // Minden más mód minden rádió módban elérhető
    return true;
}

/**
 * @brief Off mód renderelése
 */
void UICompSpectrumVis::renderOffMode() {

    if (lastRenderedMode_ == currentMode_) {
        return;
    }

    // Terület törlése, de alsó border meghagyása
    tft.fillRect(bounds.x, bounds.y, bounds.width, bounds.height - 1, TFT_BLACK);

    // OFF szöveg középre igazítása, nagyobb betűmérettel
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFreeFont();
    tft.setTextSize(3);         // Még nagyobb betűméret
    tft.setTextDatum(MC_DATUM); // Középre igazítás (függőlegesen és vízszintesen középre)
    int textX = bounds.x + bounds.width / 2;
    int textY = bounds.y + (bounds.height - 1) / 2; // Pontos középre igazítás, figyelembe véve az alsó border-t
    tft.drawString("OFF", textX, textY);
}

/**
 * @brief Waterfall szín meghatározása
 */
uint16_t UICompSpectrumVis::valueToWaterfallColor(float val, float min_val, float max_val, byte colorProfileIndex) {
    const uint16_t *colors = (colorProfileIndex == 0) ? FftDisplayConstants::waterFallColors_0 : FftDisplayConstants::waterFallColors_1;
    byte color_size = 16;

    val = constrain(val, min_val, max_val);

    int index = (int)((val - min_val) * (color_size - 1) / (max_val - min_val));
    index = constrain(index, 0, color_size - 1);

    return colors[index];
}

/**
 * @brief Beállítja a hangolási segéd típusát (CW vagy RTTY).
 * @param type A beállítandó TuningAidType.
 * @note A setFftParametersForDisplayMode() hívja meg ezt a függvényt a hangolási segéd típusának beállítására.
 */
void UICompSpectrumVis::setTuningAidType(TuningAidType type) {

    UISPECTRUM_DEBUG("UICompSpectrumVis::setTuningAidType - Beállítva TuningAidType: %d\n", static_cast<int>(type));

    bool typeChanged = (currentTuningAidType_ != type);
    currentTuningAidType_ = type;

    if (currentMode_ == DisplayMode::CWWaterfall      //
        || currentMode_ == DisplayMode::RTTYWaterfall //
        || currentMode_ == DisplayMode::CwSnrCurve    //
        || currentMode_ == DisplayMode::RttySnrCurve) {

        uint16_t oldMinFreq = currentTuningAidMinFreqHz_;
        uint16_t oldMaxFreq = currentTuningAidMaxFreqHz_;

        if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
            // CW: Az aktuális HF sávszélesség alapján optimalizált span
            uint16_t centerFreq = config.data.cwToneFrequencyHz;
            uint16_t hfBandwidthHz = CW_AF_BANDWIDTH_HZ; // Alapértelmezett CW sávszélesség

            float cwSpanHz = std::max(600.0f, hfBandwidthHz / 2.0f);

            currentTuningAidMinFreqHz_ = centerFreq - cwSpanHz / 2;
            currentTuningAidMaxFreqHz_ = centerFreq + cwSpanHz / 2;

        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            uint16_t f_mark = config.data.rttyMarkFrequencyHz;
            uint16_t f_space = f_mark - config.data.rttyShiftFrequencyHz;
            float f_center = (f_mark + f_space) / 2.0f;
            if (config.data.rttyShiftFrequencyHz <= 200) {
                // 8 rész: teljes sávszélesség 1kHz,
                // A space a 3., mark az 5. jobb oldalán
                constexpr float total_bw = 800.0f;
                float part = total_bw / 8.0f;
                // 3. jobb oldala: f_center - part
                // 5. jobb oldala: f_center + part
                currentTuningAidMinFreqHz_ = f_center - 4 * part;
                currentTuningAidMaxFreqHz_ = f_center + 4 * part;
            } else {
                // 6 rész: space a 2. jobb oldalán, mark a 4. jobb oldalán
                float part = config.data.rttyShiftFrequencyHz / 2.0f;
                currentTuningAidMinFreqHz_ = f_center - 3 * part;
                currentTuningAidMaxFreqHz_ = f_center + 3 * part;
            }

        } else {
            // OFF_DECODER: alapértelmezett tartomány
            currentTuningAidMinFreqHz_ = 0.0f;
            currentTuningAidMaxFreqHz_ = maxDisplayFrequencyHz_;
        }

        // Ha változott a frekvencia tartomány, invalidáljuk a buffert (csak waterfall módokhoz)
        if ((typeChanged || oldMinFreq != currentTuningAidMinFreqHz_ || oldMaxFreq != currentTuningAidMaxFreqHz_) &&
            (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall)) {
            for (auto &row : wabuf) {
                std::fill(row.begin(), row.end(), 0);
            }
        }
    }
}

/**
 * @brief Meghatározza, hogy egy adott FFT bin melyik alacsony felbontású sávhoz tartozik.
 * @param fft_bin_index Az FFT bin indexe.
 * @param min_bin_low_res A LowRes spektrumhoz figyelembe vett legalacsonyabb FFT bin index.
 * @param num_bins_low_res_range A LowRes spektrumhoz figyelembe vett FFT bin-ek száma.
 * @param total_bands A frekvenciasávok száma.
 * @return A sáv indexe (0-tól total_bands-1-ig).
 */
uint8_t UICompSpectrumVis::getBandVal(int fft_bin_index, int min_bin_low_res, int num_bins_low_res_range, int total_bands) {
    if (fft_bin_index < min_bin_low_res || num_bins_low_res_range <= 0) {
        return 0; // Vagy egy érvénytelen sáv index, ha szükséges
    }
    // Kiszámítjuk a relatív indexet a megadott tartományon belül
    int relative_bin_index = fft_bin_index - min_bin_low_res;
    if (relative_bin_index < 0)
        return 0; // Elvileg nem fordulhat elő, ha fft_bin_index >= min_bin_low_res

    // Leképezzük a relatív bin indexet (0-tól num_bins_low_res_range-1-ig) a total_bands sávokra
    return constrain(relative_bin_index * total_bands / num_bins_low_res_range, 0, total_bands - 1);
}

/**
 * @brief Core1 spektrum adatok lekérése
 * @param outData Kimeneti paraméter, amely a spektrum adatokra mutató pointert tartalmazza (q15_t típus).
 * @param outSize Kimeneti paraméter, amely a spektrum adatméretet tartalmazza.
 * @param outBinWidth Opcionális kimeneti paraméter, amely a bin szélességet Hz-ben tartalmazza.
 * @return Igaz, ha sikerült lekérni az adatokat, hamis egyébként.
 */
bool UICompSpectrumVis::getCore1SpectrumData(const q15_t **outData, uint16_t *outSize, float *outBinWidth) {

    if (::activeSharedDataIndex < 0 || ::activeSharedDataIndex > 1) {
        // Érvénytelen index a Core1-től - biztonságosan leállunk
        *outData = nullptr;
        *outSize = 0;
        if (outBinWidth) {
            *outBinWidth = 0.0f;
        }
        UISPECTRUM_DEBUG("UICompSpectrumVis::getCore1SpectrumData - érvénytelen shared index: %d\n", activeSharedDataIndex);
        return false;
    }

    const SharedData &data = ::sharedData[::activeSharedDataIndex];

    *outData = data.fftSpectrumData; // q15_t pointer (Core1 szolgáltatja)
    *outSize = data.fftSpectrumSize;

    if (outBinWidth) {
        *outBinWidth = data.fftBinWidthHz;
    }

    return (data.fftSpectrumData != nullptr && data.fftSpectrumSize > 0);
}

/**
 * @brief Core1 oszcilloszkóp adatok lekérése
 */
bool UICompSpectrumVis::getCore1OscilloscopeData(const int16_t **outData, uint16_t *outSampleCount) {

    // Core1 oszcilloszkóp adatok lekérése
    if (::activeSharedDataIndex < 0 || ::activeSharedDataIndex > 1) {
        *outData = nullptr;
        *outSampleCount = 0;
        UISPECTRUM_DEBUG("UICompSpectrumVis::getCore1OscilloscopeData - érvénytelen shared index: %d\n", activeSharedDataIndex);
        return false;
    }

    const SharedData &data = ::sharedData[::activeSharedDataIndex];

    *outData = data.rawSampleData;
    *outSampleCount = data.rawSampleCount;

    return (data.rawSampleData != nullptr && data.rawSampleCount > 0);
}

/**
 * @brief Muted üzenet kirajzolása
 */
void UICompSpectrumVis::drawMutedMessage() {

    // Ha már kirajzoltuk, nem csinálunk semmit
    if (isMutedDrawn) {
        return;
    }

    // A terület közepének meghatározása
    int16_t x = bounds.x + bounds.width / 2;
    int16_t y = bounds.y + bounds.height / 2;
    tft.setFreeFont();
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM); // Középre igazítás
    tft.drawString("-- Muted --", x, y);

    isMutedDrawn = true;
}

/**
 * @brief Spektrum mód dekódolása szöveggé
 */
const char *UICompSpectrumVis::decodeModeToStr() {

    const char *modeText = "";

    switch (currentMode_) {
        case DisplayMode::Off:
            modeText = "Off";
            break;
        case DisplayMode::SpectrumLowRes:
            modeText = "FFT lowres";
            break;
        case DisplayMode::SpectrumHighRes:
            modeText = "FFT highres";
            break;
        case DisplayMode::Oscilloscope:
            modeText = "Oscilloscope";
            break;
        case DisplayMode::Waterfall:
            modeText = "Waterfall";
            break;
        case DisplayMode::Envelope:
            modeText = "Envelope";
            break;
        case DisplayMode::CWWaterfall:
            modeText = "CW Waterfall";
            break;
        case DisplayMode::RTTYWaterfall:
            modeText = "RTTY Waterfall";
            break;
        case DisplayMode::CwSnrCurve:
            modeText = "CW SNR Curve";
            break;
        case DisplayMode::RttySnrCurve:
            modeText = "RTTY SNR Curve";
            break;
        default:
            modeText = "Unknown";
            break;
    }
    return modeText;
}

/**
 * @brief Mode indicator renderelése
 */
void UICompSpectrumVis::renderModeIndicator() {
    if (!modeIndicatorVisible_)
        return;

    int indicatorH = getIndicatorHeight();
    if (indicatorH < 8)
        return;

    tft.setFreeFont();
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Fekete háttér, ez törli az előzőt
    tft.setTextDatum(BC_DATUM);              // Alul-középre igazítás

    // Mode szöveggé dekódolása
    String modeText = decodeModeToStr();
    if (currentMode_ != DisplayMode::Off) {
        if (isAutoGainMode()) {
            modeText += " (Auto gain)";
        } else {
            // Manuális mód: írjuk ki az aktuális FFT gain értéket dB-ben
            int8_t gainCfg = (this->radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
            char buf[32];
            sprintf(buf, " (Man:%ddB)", gainCfg);
            modeText += buf;
        }
    }

    // A mód indikátor területének törlése a szöveg rajzolása előtt - KERET ALATT
    int indicatorY = bounds.y + bounds.height; // Közvetlenül a keret alatt kezdődik
    tft.fillRect(bounds.x - 4, indicatorY, bounds.width + 8, 10, TFT_BLACK);

    // Szöveg kirajzolása a komponens alján + indikátor terület közepén
    // Az Y koordináta a szöveg alapvonalát jelenti (az indikátor terület alja)
    tft.drawString(modeText, bounds.x + bounds.width / 2, indicatorY + indicatorH);
}

/**
 * @brief A látható frekvencia tartomány alsó és felső címkéinek kirajzolása
 * a mode indikátor helyére amikor az eltűnik
 * @param minDisplayFrequencyHz Az aktuálisan megjelenített minimum frekvencia Hz-ben
 * @param maxDisplayFrequencyHz Az aktuálisan megjelenített maximum frekvencia Hz-ben
 */
void UICompSpectrumVis::renderFrequencyRangeLabels(uint16_t minDisplayFrequencyHz, uint16_t maxDisplayFrequencyHz) {

    if (!frequencyLabelsDrawn_) {
        return;
    }

    uint16_t indicatorH = 10;
    uint16_t indicatorY = bounds.y + bounds.height; // Közvetlenül a keret alatt kezdődik

    // Felirat terület törlése a teljes szélességben
    tft.fillRect(bounds.x, indicatorY, bounds.width, indicatorH + 2, TFT_BLACK);

    tft.setFreeFont();
    tft.setTextSize(1);
    tft.setTextColor(TFT_SILVER, TFT_BLACK);

    // Waterfall módban alul/felül középre igazított a freki címkék elrendezése
    if (currentMode_ == DisplayMode::Waterfall) {
        // Min frekvencia a spektrum alatt középen
        tft.setTextDatum(BC_DATUM); // Alsó-középre igazítás
        tft.drawString(Utils::formatFrequencyString(minDisplayFrequencyHz), bounds.x + bounds.width / 2, indicatorY + indicatorH);

        // Max frekvencia a spektrum felett középen - csak a címke mögötti kis területet töröljük,
        // hogy ne töröljük a teljes felső keretet.
        String topLabel = Utils::formatFrequencyString(maxDisplayFrequencyHz);
        tft.setTextDatum(TC_DATUM);    // Felső-középre igazítás
        const int approxCharWidth = 6; // jó közelítés a setTextSize(1) esetén
        int textWidth = topLabel.length() * approxCharWidth;

        // Kis margó a szöveg körül
        int bgMargin = 4;
        int centerX = bounds.x + bounds.width / 2;
        int rectX = centerX - (textWidth / 2) - bgMargin;
        int rectW = textWidth + bgMargin * 2;

        // Clamp a téglalapot a komponens területére
        if (rectX < bounds.x) {
            rectX = bounds.x;
        }
        if (rectX + rectW > bounds.x + bounds.width) {
            rectW = (bounds.x + bounds.width) - rectX;
        }

        // Emeljük a címkét 2 pixellel magasabbra a kérés szerint.
        int rectY = bounds.y - 16; // háttér téglalap kezdete kicsit fentebb
        int rectH = 14;            // magasság csökkentve, hogy ne érjen a felső keretbe

        // Clamp rectY hogy ne lépjen ki túl messze a komponens fölé
        if (rectY < bounds.y - 20) {
            rectY = bounds.y - 20;
        }
        tft.fillRect(rectX, rectY, rectW, rectH, TFT_BLACK);

        tft.drawString(topLabel, centerX, bounds.y - 12); // eredeti -10 helyett -12

    } else {
        // Spektrum és tuning aid módokban balra/jobbra igazított a freki címkék elrendezése
        // Min/Max + köztes frekvencia címkék összesen 5 darabban
        constexpr uint8_t LABEL_NUMS = 5;
        for (uint8_t i = 0; i < LABEL_NUMS; ++i) {
            float frac = (float)i / (LABEL_NUMS - 1);
            float freq = minDisplayFrequencyHz + frac * (maxDisplayFrequencyHz - minDisplayFrequencyHz);
            uint16_t x;
            if (i == 0) {
                tft.setTextDatum(TL_DATUM);
                x = bounds.x;
            } else if (i == LABEL_NUMS - 1) {
                tft.setTextDatum(TR_DATUM);
                x = bounds.x + bounds.width - 1;
            } else {
                tft.setTextDatum(TC_DATUM);
                x = bounds.x + (int)(frac * (bounds.width - 1));
            }

            char buf[8];
            if (freq < 1000) {
                sprintf(buf, "%d", (int)roundf(freq));
            } else {
                float kfreq = freq / 1000.0f;
                uint16_t kint = (uint16_t)kfreq;
                float frac = kfreq - kint;
                uint8_t decimal = (uint8_t)(frac * 10.0f);
                if (decimal == 0) {
                    sprintf(buf, "%dk", kint);
                } else {
                    sprintf(buf, "%d.%dk", kint, decimal);
                }
            }
            tft.drawString(buf, x, indicatorY);
        }
    }

    frequencyLabelsDrawn_ = false;
}

/**
 * @brief Frissíti a CW/RTTY hangolási segéd paramétereket
 */
void UICompSpectrumVis::updateTuningAidParameters() {

    UISPECTRUM_DEBUG("UICompSpectrumVis::updateTuningAidParameters - Frissítés a konfiguráció alapján\n");

    // Újrahívjuk a setTuningAidType-ot az aktuális típussal
    // Ez frissíti a currentTuningAidMinFreqHz_ és currentTuningAidMaxFreqHz_ értékeket
    // a frissített globális változók (cwToneFrequencyHz, rttyMarkFrequencyHz, stb.) alapján
    setTuningAidType(currentTuningAidType_);

    // Ha waterfall módban vagyunk, érdemes újrarajzolni a frekvencia feliratokat is
    if (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall) {
        refreshFrequencyLabels();
    }
}

/**
 * @brief Egységes spektrum renderelés (Low és High resolution)
 * @param isLowRes true = LowRes (sávok), false = HighRes (pixel-per-bin)
 */
void UICompSpectrumVis::renderSpectrumBar(bool isLowRes) {

    float maxBarHeightThisFrame = 0.0f; // AGC-hez
    uint8_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    const uint16_t min_bin_idx = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t max_bin_idx = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const uint8_t MAX_BAR_HEIGHT = static_cast<uint8_t>(graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);

    float magRms = computeMagnitudeRmsMember(magnitudeData, min_bin_idx, max_bin_idx);
    float mag_softGain = updateRmsAndGetSoftGain(magRms, 0.08f, 400.0f, 0.12f);

    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        final_gain_lin *= barAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f);
    }
    // --- End of Gain Calculation ---

    if (isLowRes) {
        // ===== LOW RESOLUTION MODE =====
        uint8_t bands_to_display = LOW_RES_BANDS;
        if (bounds.width < (bands_to_display + (bands_to_display - 1) * BAR_GAP_PIXELS)) {
            bands_to_display = static_cast<uint8_t>((bounds.width + BAR_GAP_PIXELS) / (1 + BAR_GAP_PIXELS));
        }
        bands_to_display = std::max((uint8_t)1, bands_to_display);

        uint8_t bar_width = (bounds.width - (std::max(0, bands_to_display - 1) * BAR_GAP_PIXELS)) / bands_to_display;
        bar_width = std::max((uint8_t)1, bar_width);
        
        uint8_t bar_total_width = bar_width + BAR_GAP_PIXELS;
        int16_t x_offset = (bounds.width - ((bands_to_display * bar_width) + (std::max(0, bands_to_display - 1) * BAR_GAP_PIXELS))) / 2;

        static uint8_t peak_fall_counter = 0;
        if (++peak_fall_counter % 3 == 0) {
            for (uint8_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
                if (Rpeak_[band_idx] >= 1) Rpeak_[band_idx]--;
            }
        }

        const uint16_t num_bins_in_range = std::max(1, max_bin_idx - min_bin_idx + 1);
        q15_t band_magnitudes_q15[LOW_RES_BANDS] = {0};

        for (uint16_t i = min_bin_idx; i <= max_bin_idx; i++) {
            uint8_t band_idx = getBandVal(i, min_bin_idx, num_bins_in_range, LOW_RES_BANDS);
            if (band_idx < LOW_RES_BANDS && q15Abs(magnitudeData[i]) > q15Abs(band_magnitudes_q15[band_idx])) {
                band_magnitudes_q15[band_idx] = magnitudeData[i];
            }
        }

        uint16_t computedHeights[LOW_RES_BANDS] = {0};
        for (uint8_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            q15_t adjusted_q15 = static_cast<q15_t>(band_magnitudes_q15[band_idx] * mag_softGain);
            uint16_t height = q15ToPixelHeight(adjusted_q15, final_gain_lin, MAX_BAR_HEIGHT);
            if (height == 0 && band_magnitudes_q15[band_idx] != 0) height = 1;
            computedHeights[band_idx] = height;
        }

        uint16_t maxComputed = 0;
        for (uint8_t i = 0; i < bands_to_display; ++i) {
            if (computedHeights[i] > maxComputed) maxComputed = computedHeights[i];
        }
        maxBarHeightThisFrame = static_cast<float>(maxComputed);

        float finalScale = 1.0f;
        if (maxComputed > 0) {
            finalScale = (static_cast<float>(graphH) * GRAPH_TARGET_HEIGHT_UTILIZATION) / maxComputed;
            finalScale = constrain(finalScale, 0.2f, 5.0f);
        }

        static uint8_t bar_release_counter = 0;
        static uint8_t peak_release_counter = 0;
        bar_release_counter = (bar_release_counter + 1) % 1;
        peak_release_counter = (peak_release_counter + 1) % 5;

        for (uint16_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            uint16_t x_pos = x_offset + bar_total_width * band_idx;
            sprite_->fillRect(x_pos, 0, bar_width, graphH, TFT_BLACK);

            uint16_t scaledHeight = static_cast<uint16_t>(computedHeights[band_idx] * finalScale);
            uint8_t dsize = static_cast<uint8_t>(constrain(scaledHeight, 0, MAX_BAR_HEIGHT));
            if (dsize == 0 && computedHeights[band_idx] > 0) dsize = 1;

            if (band_idx < LOW_RES_BANDS) {
                if (dsize >= bar_height_[band_idx]) {
                    bar_height_[band_idx] = dsize;
                } else if (bar_release_counter == 0 && bar_height_[band_idx] > 0) {
                    bar_height_[band_idx] = std::max(0, bar_height_[band_idx] - (bar_height_[band_idx] - dsize > 8 ? 6 : 2));
                }
            }

            if (bar_height_[band_idx] > Rpeak_[band_idx]) {
                Rpeak_[band_idx] = bar_height_[band_idx];
            } else if (peak_release_counter == 0) {
                Rpeak_[band_idx] = std::max(0, Rpeak_[band_idx] -1);
            }

            if (bar_height_[band_idx] > 0) {
                int16_t y_start = graphH - bar_height_[band_idx];
                sprite_->fillRect(x_pos, y_start, bar_width, bar_height_[band_idx], TFT_GREEN);
            }
            if (Rpeak_[band_idx] > 3) {
                int16_t y_peak = graphH - Rpeak_[band_idx];
                sprite_->fillRect(x_pos, y_peak, bar_width, 2, TFT_CYAN);
            }
        }
    } else {
        // ===== HIGH RESOLUTION MODE =====
        const uint16_t num_bins_in_range = std::max(1, max_bin_idx - min_bin_idx + 1);
        std::vector<uint16_t> computedCols(bounds.width, 0);
        if (highresSmoothedCols.size() != bounds.width) {
            highresSmoothedCols.assign(bounds.width, 0.0f);
        }

        for (uint8_t x = 0; x < bounds.width; ++x) {
            float ratio = (bounds.width > 1) ? static_cast<float>(x) / (bounds.width - 1) : 0.0f;
            uint16_t fft_bin_index = min_bin_idx + static_cast<uint16_t>(ratio * (num_bins_in_range - 1));
            fft_bin_index = constrain(fft_bin_index, min_bin_idx, max_bin_idx);

            q15_t magnitude_q15 = magnitudeData[fft_bin_index];
            magnitude_q15 = static_cast<q15_t>(magnitude_q15 * mag_softGain);
            if (magnitude_q15 < 380) magnitude_q15 = 0;

            uint16_t height = q15ToPixelHeight(magnitude_q15, final_gain_lin, MAX_BAR_HEIGHT);
            float newSm = HIGHRES_SMOOTH_ALPHA * highresSmoothedCols[x] + (1.0f - HIGHRES_SMOOTH_ALPHA) * height;
            highresSmoothedCols[x] = newSm;
            computedCols[x] = static_cast<uint16_t>(newSm + 0.5f);
        }

        uint16_t maxComputed = *std::max_element(computedCols.begin(), computedCols.end());
        maxBarHeightThisFrame = static_cast<float>(maxComputed);
        
        float finalScale = 1.0f;
        if (maxComputed > 0) {
            finalScale = (static_cast<float>(graphH) * GRAPH_TARGET_HEIGHT_UTILIZATION) / maxComputed;
            finalScale = constrain(finalScale, 0.2f, 5.0f);
        }

        for (uint8_t x = 0; x < bounds.width; ++x) {
            uint16_t scaledHeight = static_cast<uint16_t>(computedCols[x] * finalScale);
            scaledHeight = constrain(scaledHeight, 0, graphH -1);
            if (scaledHeight == 0 && computedCols[x] > 0) scaledHeight = 1;

            sprite_->drawFastVLine(x, 0, graphH, TFT_BLACK);
            if (scaledHeight > 0) {
                sprite_->drawFastVLine(x, graphH - 1 - scaledHeight, scaledHeight + 1, TFT_SKYBLUE);
            }
        }
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    if (isAutoGainMode()) {
        updateBarBasedGain(maxBarHeightThisFrame);
    }
    renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
}

/**
 * @brief Oszcilloszkóp renderelése
 */
void UICompSpectrumVis::renderOscilloscope() {
    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const int16_t *osciRawData = nullptr;
    uint16_t sampleCount = 0;
    if (!getCore1OscilloscopeData(&osciRawData, &sampleCount) || !osciRawData || sampleCount <= 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    sprite_->fillSprite(TFT_BLACK);
    sprite_->drawFastHLine(0, graphH / 2, bounds.width, TFT_DARKGREY);

    int32_t max_abs = 1;
    double sum_sq = 0.0;
    for (uint16_t j = 0; j < sampleCount; ++j) {
        int16_t v_abs = abs(osciRawData[j]);
        if (v_abs > max_abs) max_abs = v_abs;
        sum_sq += static_cast<double>(osciRawData[j]) * osciRawData[j];
    }

    double rms = (sampleCount > 0) ? std::sqrt(sum_sq / sampleCount) : 0.0;
    oscRmsSmoothed_ = 0.08f * oscRmsSmoothed_ + (1.0f - 0.08f) * rms;

    float rms_ratio = (30.0f <= 0.0f) ? 1.0f : (oscRmsSmoothed_ / 30.0f);
    rms_ratio = constrain(rms_ratio, 0.0f, 1.0f);
    float softGainFactor = (rms_ratio < 1.0f) ? (0.12f + powf(rms_ratio, 2.0f) * (1.0f - 0.12f)) : 1.0f;

    const int32_t half_h = graphH / 2 - 1;
    
    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        uint16_t maxPixelHeight = q15ToPixelHeight(max_abs, final_gain_lin, graphH / 4);
        if (maxPixelHeight == 0 && max_abs > 0) maxPixelHeight = 1;
        updateMagnitudeBasedGain(static_cast<float>(maxPixelHeight));
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f) * softGainFactor;
    }
    // --- End of Gain Calculation ---

    uint16_t prev_x = 0;
    int16_t prev_y = 0;
    for (uint16_t i = 0; i < sampleCount; i++) {
        float pxf = isAutoGainMode() ? 
            (static_cast<float>(osciRawData[i]) / 32767.0f) * half_h * final_gain_lin :
            (static_cast<float>(osciRawData[i]) / max_abs) * half_h * final_gain_lin;

        int16_t y_pos = (graphH / 2) - static_cast<int32_t>(pxf + (pxf >= 0 ? 0.5f : -0.5f));
        y_pos = constrain(y_pos, 0, graphH - 1);
        uint16_t x_pos = (sampleCount <= 1) ? 0 : (int)round((float)i * (bounds.width - 1) / (sampleCount - 1));

        if (i > 0) {
            sprite_->drawLine(prev_x, prev_y, x_pos, y_pos, TFT_GREEN);
        }
        prev_x = x_pos;
        prev_y = y_pos;
    }
    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief RMS számítása a magnitude adatok adott bin tartományára (Q15 szemantika)
 * @param data Pointer a Q15 magnitude adatokra
 * @param startBin Kezdő FFT bin index
 * @param endBin Záró FFT bin index
 * @return RMS érték lebegőpontos formátumban
 */
float UICompSpectrumVis::computeMagnitudeRmsMember(const q15_t *data, int startBin, int endBin) const {
    if (!data || endBin < startBin)
        return 0.0f;
    double sum_sq = 0.0;
    int count = 0;
    for (int i = startBin; i <= endBin; ++i) {
        double v = static_cast<double>(data[i]);
        sum_sq += v * v;
        ++count;
    }
    if (count == 0)
        return 0.0f;
    return static_cast<float>(std::sqrt(sum_sq / count));
}

/**
 * @brief Tagfüggvény implementáció: belső simított RMS frissítése és egy "soft" erősítés visszaadása (0..1)
 */
float UICompSpectrumVis::updateRmsAndGetSoftGain(float newRms, float smoothAlpha, float silenceThreshold, float minGain) {
    // Update member smoothed RMS
    magRmsSmoothed_ = smoothAlpha * magRmsSmoothed_ + (1.0f - smoothAlpha) * newRms;
    float ratio = (silenceThreshold <= 0.0f) ? 1.0f : (magRmsSmoothed_ / silenceThreshold);
    ratio = constrain(ratio, 0.0f, 1.0f);
    if (ratio >= 1.0f)
        return 1.0f;
    constexpr float kneeExp = 2.0f;
    return minGain + powf(ratio, kneeExp) * (1.0f - minGain);
}

/**
 * @brief Envelope renderelése
 */
void UICompSpectrumVis::renderEnvelope() {
    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    for (auto &row : wabuf) {
        std::move(row.begin() + 1, row.end(), row.begin());
    }

    const uint16_t min_bin_for_env = std::max(10, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t max_bin_for_env = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ * 0.2f / currentBinWidthHz)));
    const uint16_t num_bins_in_env_range = std::max(1, max_bin_for_env - min_bin_for_env + 1);

    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        q15_t maxMagnitudeQ15 = 0;
        for (uint16_t i = min_bin_for_env; i <= max_bin_for_env; i++) {
            if (q15Abs(magnitudeData[i]) > maxMagnitudeQ15) {
                maxMagnitudeQ15 = q15Abs(magnitudeData[i]);
            }
        }
        uint16_t maxPixelHeight = q15ToPixelHeight(maxMagnitudeQ15, final_gain_lin, getGraphHeight());
        updateMagnitudeBasedGain(static_cast<float>(maxPixelHeight));
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f);
    }
    // --- End of Gain Calculation ---

    float envRms = computeMagnitudeRmsMember(magnitudeData, min_bin_for_env, max_bin_for_env);
    float env_softGain = updateRmsAndGetSoftGain(envRms, 0.05f, 380.0f, 0.08f);

    for (uint8_t r = 0; r < bounds.height; ++r) {
        uint16_t fft_bin_index = min_bin_for_env + static_cast<int>(std::round(static_cast<float>(r) * (num_bins_in_env_range - 1) / std::max(1, (bounds.height - 1))));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_env, max_bin_for_env);
        
        q15_t rawMagnitudeQ15 = static_cast<q15_t>(magnitudeData[fft_bin_index] * env_softGain);
        uint8_t rawVal = q15ToUint8(rawMagnitudeQ15, final_gain_lin);
        
        uint8_t prevVal = wabuf[r][bounds.width - 2];
        wabuf[r][bounds.width - 1] = static_cast<uint8_t>(roundf(0.35f * prevVal + (1.0f - 0.35f) * rawVal));
    }

    sprite_->fillSprite(TFT_BLACK);
    uint8_t yCenter_on_sprite = graphH / 2;
    sprite_->drawFastHLine(0, yCenter_on_sprite, bounds.width, TFT_WHITE);

    for (uint8_t c = 0; c < bounds.width; ++c) {
        int32_t sum_val_in_col = 0;
        int16_t count_val_in_col = 0;
        for (uint8_t r = 0; r < bounds.height; ++r) {
            if (wabuf[r][c] > 0) {
                sum_val_in_col += wabuf[r][c];
                count_val_in_col++;
            }
        }

        float current_col_max_amplitude = (count_val_in_col > 0) ? static_cast<float>(sum_val_in_col) / count_val_in_col : 0.0f;
        envelopeLastSmoothedValue_ = 0.05f * envelopeLastSmoothedValue_ + (1.0f - 0.05f) * current_col_max_amplitude;
        if (abs(current_col_max_amplitude - envelopeLastSmoothedValue_) < 2.0f) {
            current_col_max_amplitude = envelopeLastSmoothedValue_;
        }

        if (current_col_max_amplitude > 0.5f) {
            float displayValue = envelopeLastSmoothedValue_;
            if (displayValue > 150.0f) displayValue = 150.0f + (displayValue - 150.0f) * 0.1f;
            
            int y_offset_pixels = round((displayValue / 100.0f) * (graphH * GRAPH_TARGET_HEIGHT_UTILIZATION));
            y_offset_pixels = std::min(y_offset_pixels, static_cast<int>(graphH) - 4);

            if (y_offset_pixels > 1) {
                int yUpper = constrain(yCenter_on_sprite - y_offset_pixels / 2, 2, graphH - 3);
                int yLower = constrain(yCenter_on_sprite + y_offset_pixels / 2, 2, graphH - 3);
                if (yUpper <= yLower) {
                    sprite_->drawFastVLine(c, yUpper, yLower - yUpper + 1, TFT_WHITE);
                    if (y_offset_pixels > 4) {
                        sprite_->drawPixel(c, yUpper - 1, TFT_WHITE);
                        sprite_->drawPixel(c, yLower + 1, TFT_WHITE);
                    }
                }
            }
        }
    }
    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief Waterfall renderelése
 */
void UICompSpectrumVis::renderWaterfall() {
    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    for (auto &row : wabuf) {
        std::move(row.begin() + 1, row.end(), row.begin());
    }

    const int min_bin_for_wf = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const int max_bin_for_wf = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const int num_bins_in_wf_range = std::max(1, max_bin_for_wf - min_bin_for_wf + 1);

    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f);
    }
    // --- End of Gain Calculation ---

    uint8_t maxWabufVal = 0;
    for (uint16_t r = 0; r < bounds.height; ++r) {
        int fft_bin_index = min_bin_for_wf + static_cast<int>(std::round(static_cast<float>(r) * (num_bins_in_wf_range - 1) / std::max(1, (bounds.height - 1))));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_wf, max_bin_for_wf);
        uint8_t val = q15ToUint8(magnitudeData[fft_bin_index], final_gain_lin);
        wabuf[r][bounds.width - 1] = val;
        if (val > maxWabufVal) maxWabufVal = val;
    }

    sprite_->scroll(-1, 0);
    for (uint16_t y = 0; y < graphH; ++y) {
        float r_wabuf_float = (static_cast<float>(graphH - 1 - y) * (bounds.height - 1)) / std::max(1, graphH - 1);
        int r_lower = floor(r_wabuf_float);
        int r_upper = ceil(r_wabuf_float);
        r_lower = constrain(r_lower, 0, bounds.height - 1);
        r_upper = constrain(r_upper, 0, bounds.height - 1);
        
        float frac = r_wabuf_float - r_lower;
        float val_interp = wabuf[r_lower][bounds.width-1] * (1.0f - frac) + wabuf[r_upper][bounds.width-1] * frac;
        
        uint16_t color = valueToWaterfallColor(100 * val_interp, 0.0f, 255.0f * 100, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(bounds.width - 1, y, color);
    }

    if (isAutoGainMode()) {
        float estimatedPeak = (static_cast<float>(maxWabufVal) / 255.0f) * (graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);
        updateMagnitudeBasedGain(estimatedPeak);
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    if (!modeIndicatorVisible_) {
        renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
    }
}

/**
 * @brief Hangolási segéd renderelése (CW/RTTY waterfall)
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidWaterfall() {
    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    sprite_->scroll(0, 1);

    const int min_bin = std::max(2, static_cast<int>(std::round(currentTuningAidMinFreqHz_ / currentBinWidthHz)));
    const int max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(currentTuningAidMaxFreqHz_ / currentBinWidthHz)));
    const int num_bins = std::max(1, max_bin - min_bin + 1);

    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f);
    }
    // --- End of Gain Calculation ---

    uint8_t maxWabufVal = 0;
    for (uint16_t c = 0; c < bounds.width; ++c) {
        float ratio = (bounds.width <= 1) ? 0.0f : static_cast<float>(c) / (bounds.width - 1);
        float exact_bin = min_bin + ratio * (num_bins - 1);
        
        q15_t mag_q15 = static_cast<q15_t>(std::round(q15InterpolateFloat(magnitudeData, exact_bin, min_bin, max_bin)));
        uint8_t val = q15ToUint8(mag_q15, final_gain_lin);
        wabuf[0][c] = val;
        if (val > maxWabufVal) maxWabufVal = val;

        uint16_t color = valueToWaterfallColor(100 * val, 0.0f, 255.0f * 100, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(c, 0, color);
    }

    if (isAutoGainMode()) {
        float estimatedPeak = (static_cast<float>(maxWabufVal) / 255.0f) * (graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);
        updateMagnitudeBasedGain(estimatedPeak);
    }

    renderFrequencyRangeLabels(currentTuningAidMinFreqHz_, currentTuningAidMaxFreqHz_);
}

/**
 * @brief SNR Curve renderelése - frekvencia/SNR burkológörbe
 */
void UICompSpectrumVis::renderSnrCurve() {
    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    sprite_->fillSprite(TFT_BLACK);

    const float min_freq = (currentTuningAidMinFreqHz_ == 0) ? MIN_AUDIO_FREQUENCY_HZ : currentTuningAidMinFreqHz_;
    const float max_freq = (currentTuningAidMaxFreqHz_ == 0) ? maxDisplayFrequencyHz_ : currentTuningAidMaxFreqHz_;

    const uint16_t min_bin = std::max(2, static_cast<int>(std::round(min_freq / currentBinWidthHz)));
    const uint16_t max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(max_freq / currentBinWidthHz)));
    const uint16_t num_bins = std::max(1, max_bin - min_bin + 1);

    // --- Gain Calculation ---
    float final_gain_lin = powf(10.0f, cachedGainDb_ / 20.0f);
    if (isAutoGainMode()) {
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f);
    }
    // --- End of Gain Calculation ---

    float maxMagnitude = 0.0f;
    int16_t prevX = -1, prevY = -1;
    for (uint16_t x = 0; x < bounds.width; x++) {
        float ratio = (bounds.width <= 1) ? 0.0f : static_cast<float>(x) / (bounds.width - 1);
        float exact_bin = min_bin + ratio * (num_bins - 1);

        q15_t mag_q15 = static_cast<q15_t>(std::round(q15InterpolateFloat(magnitudeData, exact_bin, min_bin, max_bin)));
        uint16_t pixelHeight = q15ToPixelHeight(mag_q15, final_gain_lin, graphH);
        maxMagnitude = std::max(maxMagnitude, static_cast<float>(pixelHeight));

        uint16_t y = graphH - constrain(pixelHeight, 0, graphH -1);

        if (prevX != -1) {
            sprite_->drawLine(prevX, prevY, x, y, TFT_CYAN);
        }
        sprite_->drawPixel(x, y, TFT_WHITE);
        prevX = x;
        prevY = y;
    }

    if (isAutoGainMode()) {
        updateMagnitudeBasedGain(maxMagnitude);
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    renderFrequencyRangeLabels(static_cast<uint16_t>(min_freq), static_cast<uint16_t>(max_freq));
}
