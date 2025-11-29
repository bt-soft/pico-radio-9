/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompSpectrumVis.cpp                                                                                         *
 * Created Date: 2025.11.15.                                                                                           *
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
 * Last Modified: 2025.11.29, Saturday  05:39:55                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

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
#define __UISPECTRUM_DEBUG
#if defined(__DEBUG) && defined(__UISPECTRUM_DEBUG)
#define UISPECTRUM_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define UISPECTRUM_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// A képernyő ennyi százalékát töltse ki a grafikon a rendelkezésre álló magasságból
#define UI_TARGET_HEIGHT_UTILIZATION 0.85f // 85%-os magasságú maximális magasság

// Magnitude-alapú AGC célértékek (nyers FFT magnitude tartomány)
#define MAGNITUDE_AGC_TARGET_VALUE 8000.0f // Célérték magnitude módokhoz

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

// Helper: egységes Q15 -> display float konverzió
static inline float q15ToDisplayMagnitude(q15_t v) {
    // A Core1 által visszaadott Q15 értékek már tartalmazhatnak FFT skálázást.
    // Itt egyszerűen float-ra konvertáljuk, a UI érzékenységet az erre alapuló
    // SensitivityConstants értékekkel hangoljuk.
    return static_cast<float>(v);
}

const uint16_t waterFallColors_1[16] = {0x0000, 0x1000, 0x2000, 0x4000, 0x8000, 0xC000, 0xF800, 0xF8A0,
                                        0xF9C0, 0xFD20, 0xFFE0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}; // Hot
#define WATERFALL_COLOR_INDEX 0

constexpr uint16_t MODE_INDICATOR_VISIBLE_TIMEOUT_MS = 10 * 1000; // A mód indikátor kiírásának láthatósága x másodpercig
constexpr uint8_t SPECTRUM_FPS = 25;                              // FPS limitálás konstans, ez még élvezhető vizualizációt ad, maradjon így 20 FPS-en

}; // namespace FftDisplayConstants

// Zajküszöb - alacsony szintű zajt nullázza
constexpr float NOISE_THRESHOLD_FM = 0.0f; // Zajszűrés: zaj magnitúdó értékével
constexpr float NOISE_THRESHOLD_AM = 0.0f; // Zajszűrés: zaj magnitúdó értékével

// ===== ÉRZÉKENYSÉGI / AMPLITÚDÓ SKÁLÁZÁSI KONSTANSOK =====
// Minden grafikon mód érzékenységét és amplitúdó skálázását itt lehet módosítani
// EGYSÉGES LOGIKA: nagyobb érték = nagyobb érzékenység (minden módnál)
namespace SensitivityConstants {

//
// TODO: Az érzékenységi értékek az Si4703 50-es hangerőssége mellett a kapcsrajznak megfelelő hardveren lettek beállítva
//

// LoweRes Spektrum mód
constexpr float LOWRES_SPECTRUMBAR_SENSITIVITY_FACTOR = 0.003f; // LowRes Spektrum bar-ok amplitúdó skálázása (nagyobb érték: nagyobb sávok)

// HighRes Spektrum mód
constexpr float HIGHRES_SPECTRUMBAR_SENSITIVITY_FACTOR = 0.08f; // HighRes Spektrum bar-ok amplitúdó skálázása

// Oszcilloszkóp mód
constexpr float OSCI_SENSITIVITY_FACTOR = 3.5f; // Oszcilloszkóp jel erősítése

// Envelope mód
constexpr float ENVELOPE_SENSITIVITY_FACTOR = 0.15f; // Envelope amplitúdó erősítése

// Waterfall mód
constexpr float WATERFALL_SENSITIVITY_FACTOR = 0.2f; // Waterfall intenzitás skálázása

// CW/RTTY SNR Curve
constexpr float TUNING_AID_SNR_CURVE_SENSITIVITY_FACTOR = 0.08f;

// CW/RTTY Tuning Aid Waterfall
constexpr float TUNING_AID_WATERFALL_SENSITIVITY_FACTOR = 0.2f;

}; // namespace SensitivityConstants

// Analizátor konstansok
namespace AnalyzerConstants {
constexpr uint16_t ANALYZER_MIN_FREQ_HZ = 300;
}; // namespace AnalyzerConstants

// Tunnnig Aid/Curve Színek
constexpr uint16_t TUNING_AID_CW_TARGET_COLOR = TFT_GREEN;
constexpr uint16_t TUNING_AID_RTTY_SPACE_COLOR = TFT_MAGENTA;
constexpr uint16_t TUNING_AID_RTTY_MARK_COLOR = TFT_YELLOW;

/**
 * @brief Konstruktor
 */
UICompSpectrumVis::UICompSpectrumVis(int x, int y, int w, int h, RadioMode radioMode)
    : UIComponent(Rect(x, y, w, h)),                   //
      radioMode_(radioMode),                           //
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
      isMutedDrawn(false) {

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

        UISPECTRUM_DEBUG("UICompSpectrumVis [Bar AGC]: currentBarMaxValue=%.1f barAgcGainFactor_=%.3f\n", currentBarMaxValue, barAgcGainFactor_);
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

        UISPECTRUM_DEBUG("UICompSpectrumVis [Magnitude AGC]: currentMagnitudeMaxValue=%.1f magnitudeAgcGainFactor_=%.3f\n", currentMagnitudeMaxValue,
                         magnitudeAgcGainFactor_);
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

    // History átlag számítása (stabil érték több frame alapján)
    float sum = 0.0f;
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < historySize; i++) {
        if (history[i] > AGC_MIN_SIGNAL_THRESHOLD) {
            sum += history[i];
            validCount++;
        }
    }

    if (validCount == 0) {
        return currentGainFactor; // Nincs elég jel, megtartjuk a jelenlegi gain-t
    }

    float averageMax = sum / validCount;
    int graphH = getGraphHeight();
    float targetHeight = graphH * UI_TARGET_HEIGHT_UTILIZATION; // Pixel magasság cél
    float idealGain = targetHeight / averageMax;

    // Simított átmenet az új gain-hez
    float newGain = AGC_SMOOTH_FACTOR * idealGain + (1.0f - AGC_SMOOTH_FACTOR) * currentGainFactor;

    // Biztonsági korlátok
    return constrain(newGain, 0.1f, 20.0f);
}

/**
 * @brief Magnitude-alapú AGC gain számítás (nyers FFT érték alapú)
 * @param history History buffer (magnitude értékek)
 * @param historySize History buffer mérete
 * @param currentGainFactor Jelenlegi gain faktor
 * @return Új gain faktor
 */
float UICompSpectrumVis::calculateMagnitudeAgcGain(const float *history, uint8_t historySize, float currentGainFactor) const {
    // History átlag számítása (stabil érték több frame alapján)
    float sum = 0.0f;
    int validCount = 0;
    for (int i = 0; i < historySize; i++) {
        if (history[i] > AGC_MIN_SIGNAL_THRESHOLD) {
            sum += history[i];
            validCount++;
        }
    }

    if (validCount == 0) {
        return currentGainFactor; // Nincs elég jel, megtartjuk a jelenlegi gain-t
    }

    float averageMax = sum / validCount;
    float targetMagnitude = MAGNITUDE_AGC_TARGET_VALUE; // Fix magnitude cél
    float idealGain = targetMagnitude / averageMax;

    // Simított átmenet az új gain-hez
    float newGain = AGC_SMOOTH_FACTOR * idealGain + (1.0f - AGC_SMOOTH_FACTOR) * currentGainFactor;

    // Biztonsági korlátok
    return constrain(newGain, 0.1f, 20.0f);
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
    barAgcGainFactor_ = 0.02f;
    barAgcLastUpdateTime_ = 0;
}

/**
 * @brief Magnitude-alapú AGC reset
 */
void UICompSpectrumVis::resetMagnitudeAgc() {
    memset(magnitudeAgcHistory_, 0, sizeof(magnitudeAgcHistory_));
    magnitudeAgcHistoryIndex_ = 0;
    magnitudeAgcGainFactor_ = 0.02f;
    magnitudeAgcLastUpdateTime_ = 0;
}

/**
 * @brief Ellenőrzi, hogy auto gain módban vagyunk-e
 * @return True, ha auto gain mód aktív
 */
bool UICompSpectrumVis::isAutoGainMode() {
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
 * @brief UIComponent draw implementáció
 */
void UICompSpectrumVis::draw() {

    // Ha van aktív dialog a képernyőn, ne rajzoljunk semmit
    if (isCurrentScreenDialogActive()) {
        return;
    }

    // FPS limitálás - az FPS értéke makróval állítható
    constexpr uint32_t FRAME_TIME_MS = 1000 / FftDisplayConstants::SPECTRUM_FPS;
    uint32_t currentTime = millis();
    if (!Utils::timeHasPassed(lastFrameTime_, FRAME_TIME_MS)) {
        return;
    }
    lastFrameTime_ = currentTime;

#ifdef __DEBUG
    // AGC logolás 1mp-ként, de csak ha be van kapcsolva az auto agc
    static uint32_t lastAgcLogTime = 0;
    if (isAutoGainMode() && Utils::timeHasPassed(lastAgcLogTime, 1000)) {
        UISPECTRUM_DEBUG("UICompSpectrumVis [Auto AGC] mode=%d barGain=%.3f magGain=%.3f\n", (int)currentMode_, barAgcGainFactor_, magnitudeAgcGainFactor_);
        lastAgcLogTime = currentTime;
    }
#endif

    // Ha Mute állapotban vagy vagyunk
#if not defined TEST_DO_NOT_PROCESS_MUTED_STATE
    if (rtv::muteStat) {
        if (!isMutedDrawn) {
            drawFrame();
            drawMutedMessage();
        }
        return;

    } else if (!rtv::muteStat && isMutedDrawn) {
        isMutedDrawn = false;
        needBorderDrawn = true; // Muted állapot megszűnt, rajzoljuk újra a keretet
    }
#endif

    if (needBorderDrawn) {
        drawFrame();             // Rajzoljuk meg a keretet, ha szükséges
        needBorderDrawn = false; // Reset the flag after drawing
    }

#if not defined TEST_DO_NOT_PROCESS_MUTED_STATE
    if (rtv::muteStat) {
        return;
    }
#endif

    // Biztonsági ellenőrzés: FM módban CW/RTTY és az SNR hangolássegéd módok nem engedélyezettek
    if (radioMode_ == RadioMode::FM &&                 //
        (currentMode_ == DisplayMode::CWWaterfall      //
         || currentMode_ == DisplayMode::RTTYWaterfall //
         || currentMode_ == DisplayMode::CwSnrCurve    //
         || currentMode_ == DisplayMode::RttySnrCurve) //
    ) {
        currentMode_ = DisplayMode::Waterfall; // Automatikus váltás Waterfall módra
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

    // Mode indicator megjelenítése ha szükséges
    if (modeIndicatorVisible_ && !modeIndicatorDrawn_) {
        renderModeIndicator();
        modeIndicatorDrawn_ = true; // Mark as drawn
    }

    // A mode indicator időzített eltüntetése
    if (modeIndicatorVisible_ && millis() > modeIndicatorHideTime_) {
        modeIndicatorVisible_ = false;
        modeIndicatorDrawn_ = false;

        // Töröljük a területet ahol az indicator volt - KERET ALATT
        int indicatorY = bounds.y + bounds.height; // Közvetlenül a keret alatt
        tft.fillRect(bounds.x - 3, indicatorY, bounds.width + 3, 10, TFT_BLACK);

        // Frekvencia feliratok kirajzolásának engedélyezése
        frequencyLabelsDrawn_ = true;
    }

    // Ha változott a kijelzési mód, töröljük a waterfall felső feliratát
    if (lastRenderedMode_ != currentMode_) {
        if (lastRenderedMode_ == DisplayMode::Waterfall) {
            // Töröljük a spektrum feletti területet ahol a max frekvencia volt
            // Pontosan a szöveg területét töröljük
            tft.fillRect(bounds.x, bounds.y - 18, bounds.width, 15, TFT_BLACK);
        }
    }

    lastRenderedMode_ = currentMode_;
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
        int graphH = getGraphHeight();
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
    for (int i = 0; i < MAX_SPECTRUM_BANDS; ++i) {
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
        uint16_t minHz = sdToUse->displayMinFreqHz ? sdToUse->displayMinFreqHz : AnalyzerConstants::ANALYZER_MIN_FREQ_HZ;
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
    modeIndicatorDrawn_ = false; // Reset a flag-et hogy azonnal megjelenjen
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

    // TODO: BUG - Még nem találtam meg a hibát (2025.11), de ha a config-ban SNR Curve van lementve AM módban, akkor lefagy a rendszer az induláskor
    // Emiatt ha AM módban, ha valamelyi SNR Curve van elmentve, akkor visszaállunk Spectrum LowRes módra, így legalább elindul...
    // Nyilván a renderSnrCurve metódusban van valami gond, amit még ki kell javítani...
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
}

/**
 * @brief Módkijelző láthatóságának beállítása
 */
void UICompSpectrumVis::setModeIndicatorVisible(bool visible) {
    modeIndicatorVisible_ = visible;
    modeIndicatorDrawn_ = false; // Reset draw flag when visibility changes
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
    tft.setTextSize(3);         // Még nagyobb betűméret
    tft.setTextDatum(MC_DATUM); // Középre igazítás (Middle Center)
    int textX = bounds.x + bounds.width / 2;
    int textY = bounds.y + (bounds.height - 1) / 2; // Pontos középre igazítás, figyelembe véve az alsó border-t
    tft.drawString("OFF", textX, textY);
}

/**
 * @brief Egységes spektrum renderelés (Low és High resolution)
 * @param isLowRes true = LowRes (sávok), false = HighRes (pixel-per-bin)
 */
void UICompSpectrumVis::renderSpectrumBar(bool isLowRes) {

    uint8_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        if (!spriteCreated_) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderSpectrum - Sprite nincs létrehozva\n");
        }
        return;
    }

    // Core1 spektrum adatok lekérése
    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    float currentAutoGain = 1.0f;
    bool dataAvailable = getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz, &currentAutoGain);

    // Ha nincs friss adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !magnitudeData || currentBinWidthHz == 0) {
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderSpectrum - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // FFT bin index tartomány számítása a megjelenítési frekvencia határok alapján
    const uint16_t min_bin_idx = std::max(2, static_cast<int>(std::round(AnalyzerConstants::ANALYZER_MIN_FREQ_HZ / currentBinWidthHz)));
    const uint16_t max_bin_idx = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));

    // DEBUG: Frekvencia skálázás ellenőrzése
    // UISPECTRUM_DEBUG("renderSpectrum: binWidth=%.2f Hz, ANALYZER_MIN=%.0f Hz, maxDisplay=%.0f Hz, min_bin=%d, max_bin=%d, num_bins=%d\n", currentBinWidthHz,
    //                  (float)AnalyzerConstants::ANALYZER_MIN_FREQ_HZ, (float)maxDisplayFrequencyHz_, min_bin_idx, max_bin_idx, (max_bin_idx - min_bin_idx +
    //                  1));

    float maxMagnitude = 0.0f;

    // Maximális magnitude érték keresése a megjelenítési tartományban
    const uint8_t MAX_BAR_HEIGHT = static_cast<uint8_t>(graphH * UI_TARGET_HEIGHT_UTILIZATION);

    if (isLowRes) {

        // ===== LOW RESOLUTION MODE =====
        // Sávokba csoportosított rajzolás (széles oszlopok peak-kel)
        uint8_t bands_to_display = LOW_RES_BANDS;

        if (bounds.width < (bands_to_display + (bands_to_display - 1) * BAR_GAP_PIXELS)) {
            bands_to_display = static_cast<uint8_t>((bounds.width + BAR_GAP_PIXELS) / (1 + BAR_GAP_PIXELS));
        }
        if (bands_to_display == 0) {
            bands_to_display = 1;
        }

        uint8_t bar_width = 1;
        if (bands_to_display > 0) {
            bar_width = static_cast<uint8_t>((bounds.width - (std::max(0, bands_to_display - 1) * BAR_GAP_PIXELS)) / bands_to_display);
        }
        if (bar_width < 1) {
            bar_width = 1;
        }

        uint8_t bar_total_width = bar_width + BAR_GAP_PIXELS;
        uint16_t total_drawn_width = (bands_to_display * bar_width) + (std::max(0, bands_to_display - 1) * BAR_GAP_PIXELS);
        int16_t x_offset = (bounds.width - total_drawn_width) / 2;

        // Peak ereszkedés: csak minden 3. hívásnál csökkentjük
        static uint8_t peak_fall_counter = 0;
        peak_fall_counter = (peak_fall_counter + 1) % 3;
        for (uint8_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            if (peak_fall_counter == 0) {
                if (Rpeak_[band_idx] >= 1)
                    Rpeak_[band_idx] -= 1;
            }
        }

        const uint16_t num_bins_in_range = std::max(1, max_bin_idx - min_bin_idx + 1);
        float band_magnitudes[LOW_RES_BANDS] = {0.0f};

        // Sávok feltöltése a legnagyobb magnitude értékekkel
        for (uint16_t i = min_bin_idx; i <= max_bin_idx; i++) {
            uint8_t band_idx = getBandVal(i, min_bin_idx, num_bins_in_range, LOW_RES_BANDS);
            if (band_idx < LOW_RES_BANDS) {
                // Q15 direkt float konverzió (hasonló tartomány mint a float FFT)
                float magnitude = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[i]);
                // ELŐSZÖR zajszűrés a nyers adaton
                if (magnitude < (this->radioMode_ == RadioMode::AM ? NOISE_THRESHOLD_AM : NOISE_THRESHOLD_FM)) {
                    magnitude = 0.0f;
                    UISPECTRUM_DEBUG("UICompSpectrumVis::renderSpectrumBar - Bin %d magnitude %.3f zajküszöb alatt, nullázva\n", i, magnitude);
                }
                band_magnitudes[band_idx] = std::max(band_magnitudes[band_idx], magnitude);
            }
        }

        // AGC-hez: bar-ok maximum magassága (első néhány sáv kihagyva)
        float maxBarMagnitude = 0.0f;
        for (uint8_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            maxBarMagnitude = std::max(maxBarMagnitude, band_magnitudes[band_idx]);
        }

        // Bar-alapú AGC frissítés (LowRes: maxBarMagnitude a legnagyobb sáv magnitude értéke)
        updateBarBasedGain(maxBarMagnitude);

        // Sávok számítása (első passz): kiszámoljuk a nyers bar magasságokat, de még nem rajzolunk
        float computedHeights[LOW_RES_BANDS] = {0.0f};
        for (uint8_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            // Skálázás a konfigurált érzékenység és AGC alapján
            float adaptiveScale = getBarAgcScale(SensitivityConstants::LOWRES_SPECTRUMBAR_SENSITIVITY_FACTOR);
            if (isAutoGainMode()) {
                adaptiveScale *= 5.5f; // megőrizve a korábbi finomítást
            }
            float magnitude = band_magnitudes[band_idx] * adaptiveScale;

            // Zajküszöb és legalacsonyabb pixel
            if (magnitude <= 0.0f)
                magnitude = 1.0f;

            // Korlátozás a megjeleníthető maximális pixelmagasságra
            float clamped = constrain(magnitude, 0.0f, static_cast<float>(MAX_BAR_HEIGHT));
            computedHeights[band_idx] = clamped;
        }

        // Második passz: skálázzuk a teljes sor magasságait úgy, hogy a legnagyobb bar a cél százalékot kitöltse
        float maxComputed = 0.0f;
        for (uint8_t i = 0; i < bands_to_display; ++i) {
            maxComputed = std::max(maxComputed, computedHeights[i]);
        }

        float targetHeight = static_cast<float>(graphH) * UI_TARGET_HEIGHT_UTILIZATION; // cél pixelmagasság
        float finalScale = 1.0f;
        if (maxComputed > 0.0f) {
            finalScale = targetHeight / maxComputed;
            // Korlátozzuk a skálát túl nagy felskálázás ellen
            finalScale = constrain(finalScale, 0.2f, 5.0f);
        }

        // Végső kirajzolás és peak frissítés
        static uint8_t bar_release_counter = 0;
        static uint8_t peak_release_counter = 0;
        bar_release_counter = (bar_release_counter + 1) % 1;
        peak_release_counter = (peak_release_counter + 1) % 5;

        for (uint16_t band_idx = 0; band_idx < bands_to_display; band_idx++) {
            uint16_t x_pos = x_offset + bar_total_width * band_idx;
            sprite_->fillRect(x_pos, 0, bar_width, graphH, TFT_BLACK);

            float scaledHeightF = computedHeights[band_idx] * finalScale;
            uint8_t dsize = static_cast<uint8_t>(constrain(scaledHeightF, 0.0f, static_cast<float>(MAX_BAR_HEIGHT)));
            if (dsize == 0)
                dsize = 1;

            // Csillapítás: ugyanaz a logika, de most a dsize-ot használjuk
            if (band_idx < MAX_SPECTRUM_BANDS) {
                if (dsize >= bar_height_[band_idx]) {
                    bar_height_[band_idx] = dsize;
                } else {
                    if (bar_release_counter == 0 && bar_height_[band_idx] > 1) {
                        if (bar_height_[band_idx] - dsize > 8) {
                            bar_height_[band_idx] -= 6;
                        } else {
                            bar_height_[band_idx] -= 2;
                        }
                        if (bar_height_[band_idx] < 1) {
                            bar_height_[band_idx] = 0;
                        }
                    } else if (bar_release_counter == 0 && bar_height_[band_idx] == 1) {
                        bar_height_[band_idx] = 0;
                    }
                }
            }

            // Peak frissítés
            if (bar_height_[band_idx] > Rpeak_[band_idx] && band_idx < MAX_SPECTRUM_BANDS) {
                Rpeak_[band_idx] = bar_height_[band_idx];
            } else if (bar_height_[band_idx] < Rpeak_[band_idx] && band_idx < MAX_SPECTRUM_BANDS) {
                if (peak_release_counter == 0) {
                    Rpeak_[band_idx]--;
                }
            }

            // Rajzolás
            uint8_t bar_draw_height = bar_height_[band_idx];
            if (bar_draw_height > 0) {
                int16_t y_start = graphH - bar_draw_height;
                uint8_t bar_h = bar_draw_height;
                if (y_start < 0) {
                    bar_h -= (0 - y_start);
                    y_start = 0;
                }
                if (bar_h > 0) {
                    sprite_->fillRect(x_pos, y_start, bar_width, bar_h, TFT_GREEN);
                }
            }

            if (Rpeak_[band_idx] > 3) {
                int16_t y_peak = graphH - Rpeak_[band_idx];
                sprite_->fillRect(x_pos, y_peak, bar_width, 2, TFT_CYAN);
            }
        }
    } else {
        // ===== HIGH RESOLUTION MODE =====
        // Pixel-per-bin rajzolás (vékony vonalak)
        const uint16_t num_bins_in_range = std::max(1, max_bin_idx - min_bin_idx + 1);

        float maxBinMagnitudeForAgc = 0.0f;

        // HighRes: kétfázisos számítás (először nyers oszlopmagasságok, majd skálázás a cél kitöltésre)
        std::vector<float> computedCols(bounds.width, 0.0f);
        for (uint8_t screen_pixel_x = 0; screen_pixel_x < bounds.width; ++screen_pixel_x) {
            uint16_t fft_bin_index;
            if (bounds.width == 1) {
                fft_bin_index = min_bin_idx;
            } else {
                float ratio = static_cast<float>(screen_pixel_x) / (bounds.width - 1);
                fft_bin_index = min_bin_idx + static_cast<uint16_t>(std::round(ratio * (num_bins_in_range - 1)));
            }
            fft_bin_index = constrain(fft_bin_index, 0, max_bin_idx);

            // Q15 direkt float konverzió (hasonló tartomány mint a float FFT)
            float magnitude = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[fft_bin_index]);
            // ELŐSZÖR zajszűrés a nyers adaton
            if (magnitude < (this->radioMode_ == RadioMode::AM ? NOISE_THRESHOLD_AM : NOISE_THRESHOLD_FM)) {
                magnitude = 0.0f;
            }

            // AGC-hez: csak a kihagyott bin-ek után számoljuk a max-ot
            if (fft_bin_index >= min_bin_idx) {
                maxBinMagnitudeForAgc = std::max(maxBinMagnitudeForAgc, magnitude);
            }

            // UTÁNA erősítés a tiszta jelen (de még nem skálázzuk a teljes oszlopsort)
            float adaptiveScale = getBarAgcScale(SensitivityConstants::HIGHRES_SPECTRUMBAR_SENSITIVITY_FACTOR);
            float amplified = magnitude * adaptiveScale;
            computedCols[screen_pixel_x] = amplified;
            maxMagnitude = std::max(maxMagnitude, amplified);
        }

        // Második passz: skálázzuk úgy, hogy a legnagyobb oszlop a cél kitöltést adja
        float maxComputed = 0.0f;
        for (auto v : computedCols)
            maxComputed = std::max(maxComputed, v);

        float targetHeight = static_cast<float>(graphH) * UI_TARGET_HEIGHT_UTILIZATION;
        float finalScale = 1.0f;
        if (maxComputed > 0.0f) {
            finalScale = targetHeight / maxComputed;
            finalScale = constrain(finalScale, 0.2f, 5.0f);
        }

        for (uint8_t screen_pixel_x = 0; screen_pixel_x < bounds.width; ++screen_pixel_x) {
            float amplified = computedCols[screen_pixel_x] * finalScale;

            // AGC esetén a highresBar túlvezérlődik, ha szükséges itt csökkenthető (jelenleg nincs további csillapítás)
            if (isAutoGainMode()) {
                // optional: amplified *= 0.07f;
            }

            sprite_->drawFastVLine(screen_pixel_x, 0, graphH, TFT_BLACK);
            uint8_t scaled_magnitude = static_cast<uint8_t>(constrain(amplified, 0, graphH - 1));

            if (scaled_magnitude == 0) {
                scaled_magnitude = 1;
            }

            int16_t y_bar_start = graphH - 1 - scaled_magnitude;
            uint8_t bar_actual_height = scaled_magnitude + 1;
            if (y_bar_start < 0) {
                bar_actual_height -= (0 - y_bar_start);
                y_bar_start = 0;
            }
            if (bar_actual_height > 0) {
                sprite_->drawFastVLine(screen_pixel_x, y_bar_start, bar_actual_height, TFT_SKYBLUE);
            }
        }

        // AGC frissítés: az első néhány bin kihagyásával számolt max értéket használjuk
        updateBarBasedGain(maxBinMagnitudeForAgc);
    }

    // Frissítjük a bar-alapú AGC-t (HighRes: maxMagnitude a legnagyobb bar magasság)
    updateBarBasedGain(maxMagnitude);

    // Sprite kirakása
    sprite_->pushSprite(bounds.x, bounds.y);

    // Frekvencia feliratok kirajzolása, ha szükséges
    renderFrequencyRangeLabels(AnalyzerConstants::ANALYZER_MIN_FREQ_HZ, maxDisplayFrequencyHz_);
}

/**
 * @brief Oszcilloszkóp renderelése
 */
void UICompSpectrumVis::renderOscilloscope() {

    uint8_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        if (!spriteCreated_) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderOscilloscope - Sprite nincs létrehozva\n");
        }
        return;
    }

    // Core1 oszcilloszkóp adatok lekérése
    const int16_t *osciData = nullptr;
    uint16_t sampleCount = 0;
    bool dataAvailable = getCore1OscilloscopeData(&osciData, &sampleCount);

    // Ha nincs friss adat vagy nincs oszcilloszkóp adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !osciData || sampleCount <= 0) {
        // Csak a sprite kirakása a korábbi tartalommal
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderOscilloscope - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // Sprite törlése - csak akkor, ha van új adat
    sprite_->fillSprite(TFT_BLACK);

    uint16_t prev_x = -1, prev_y = -1;
    for (uint16_t i = 0; i < sampleCount; i++) {

        int16_t raw_sample = osciData[i];

        float gain_adjusted_deviation = static_cast<float>(raw_sample) * SensitivityConstants::OSCI_SENSITIVITY_FACTOR;
        float scaled_y_deflection = gain_adjusted_deviation * (static_cast<float>(graphH) / 2.0f - 1.0f) / 2048.0f;

        uint16_t y_pos = graphH / 2 - static_cast<int>(round(scaled_y_deflection));
        y_pos = constrain(y_pos, 0, graphH - 1);
        uint16_t x_pos = (sampleCount == 1) ? 0 : (int)round((float)i / (sampleCount - 1) * (bounds.width - 1));

        if (prev_x != -1 && i > 0) {
            sprite_->drawLine(prev_x, prev_y, x_pos, y_pos, TFT_GREEN);
        } else if (prev_x == -1) {
            sprite_->drawPixel(x_pos, y_pos, TFT_GREEN);
        }

        prev_x = x_pos;
        prev_y = y_pos;
    }
    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief Envelope renderelése
 */
void UICompSpectrumVis::renderEnvelope() {
    // Audio feldolgozás Core1-en történik, AudioCore1Manager-en keresztül

    int graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        if (!spriteCreated_) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderEnvelope - Sprite nincs létrehozva\n");
        }
        return;
    }

    // Core1 spektrum adatok lekérése
    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    float currentAutoGain = 1.0f;
    bool dataAvailable = getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz, &currentAutoGain);

    // Ha nincs friss adat vagy nincs magnitude adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !magnitudeData || currentBinWidthHz == 0) {
        // Csak a sprite kirakása a korábbi tartalommal
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderEnvelope - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // 1. Adatok eltolása balra a wabuf-ban
    for (uint8_t r = 0; r < bounds.height; ++r) { // Teljes bounds.height
        for (uint8_t c = 0; c < bounds.width - 1; ++c) {
            wabuf[r][c] = wabuf[r][c + 1];
        }
    }

    const uint16_t min_bin_for_env = std::max(10, static_cast<int>(std::round(AnalyzerConstants::ANALYZER_MIN_FREQ_HZ / currentBinWidthHz)));
    const uint16_t max_bin_for_env =
        std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ * 0.2f / currentBinWidthHz)));
    const uint16_t num_bins_in_env_range = std::max(1, max_bin_for_env - min_bin_for_env + 1);

    // Magnitude-alapú adaptív skálázás envelope-hoz
    float adaptiveScale = getMagnitudeAgcScale(SensitivityConstants::ENVELOPE_SENSITIVITY_FACTOR);
    // Konzervatív korlátok envelope-hoz
    adaptiveScale =
        constrain(adaptiveScale, SensitivityConstants::ENVELOPE_SENSITIVITY_FACTOR * 0.1f, SensitivityConstants::ENVELOPE_SENSITIVITY_FACTOR * 10.0f);

    // 2. Új adatok betöltése
    // Az Envelope módhoz az magnitudeData értékeit használjuk csökkentett erősítéssel.
    float maxRawMagnitude = 0.0f;
    float maxGainedVal = 0.0f;

    // Minden sort feldolgozunk a teljes felbontásért
    for (uint8_t r = 0; r < bounds.height; ++r) {
        // 'r' (0 to bounds.height-1) leképezése FFT bin indexre a szűkített tartományon belül
        uint8_t fft_bin_index =
            min_bin_for_env + static_cast<uint8_t>(std::round(static_cast<float>(r) / std::max(1, (bounds.height - 1)) * (num_bins_in_env_range - 1)));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_env, max_bin_for_env);
        // Q15 direkt float konverzió (hasonló tartomány mint a float FFT)
        float rawMagnitude = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[fft_bin_index]);

        // KRITIKUS: Infinity és NaN értékek szűrése!
        if (!isfinite(rawMagnitude) || rawMagnitude < 0.0) {
            rawMagnitude = 0.0;
        }

        // ELŐSZÖR zajszűrés a nyers adaton (NOISE_THRESHOLD)
        if (rawMagnitude < (this->radioMode_ == RadioMode::AM ? NOISE_THRESHOLD_AM : NOISE_THRESHOLD_FM)) {
            rawMagnitude = 0.0;
        }

        // ----------------------------------------
        // AGC vagy manuális erősítés alkalmazása
        // ----------------------------------------
        // AGC esetén az envelope túlvezérlődik, csökkentjük az erősítést
        if (isAutoGainMode()) {
            adaptiveScale *= 0.7f;
        }
        // UTÁNA erősítés a tiszta jelen
        float gained_val = rawMagnitude * adaptiveScale;

        // Debug info gyűjtése
        maxRawMagnitude = std::max(maxRawMagnitude, rawMagnitude);
        maxGainedVal = std::max(maxGainedVal, gained_val);

        wabuf[r][bounds.width - 1] = static_cast<uint8_t>(constrain(gained_val, 0.0, 255.0));
    }

    // 3. Sprite törlése és burkológörbe kirajzolása
    sprite_->fillSprite(TFT_BLACK); // Sprite törlése

    // Erőteljes simítás a tüskék ellen
    constexpr float ENVELOPE_SMOOTH_FACTOR = 0.05f; // Sokkal erősebb simítás
    // ENVELOPE_NOISE_THRESHOLD ELTÁVOLÍTVA - használjuk az általános NOISE_THRESHOLD-ot!

    // Először rajzoljunk egy vékony központi vízszintes vonalat (alapvonal) - mindig látható
    uint8_t yCenter_on_sprite = graphH / 2;
    sprite_->drawFastHLine(0, yCenter_on_sprite, bounds.width, TFT_WHITE);

    for (uint8_t c = 0; c < bounds.width; ++c) {
        int16_t sum_val_in_col = 0;
        int16_t count_val_in_col = 0;
        bool column_has_signal = false;

        for (uint8_t r_wabuf = 0; r_wabuf < bounds.height; ++r_wabuf) { // Teljes bounds.height
            // Használjuk az általános NOISE_THRESHOLD-ot (már felerősített értékeken)
            if (wabuf[r_wabuf][c] > 0) { // Ha nem nulla (már szűrve volt a nyers adaton)
                column_has_signal = true;
                sum_val_in_col += wabuf[r_wabuf][c];
                count_val_in_col++;
            }
        }

        // A maximális amplitúdó simítása az oszlopban - átlag helyett maximum, de korlátozott
        float current_col_max_amplitude = 0.0f;
        if (count_val_in_col > 0) {
            current_col_max_amplitude = static_cast<float>(sum_val_in_col) / count_val_in_col;
            // NEM korlátozzuk itt - a rajzolásnál fogjuk kezelni a tüskéket
        }

        // Zajszűrés már megtörtént a nyers adaton, nincs szükség itt dupla szűrésre

        // Erősebb simítás az oszlopok között - lassabb változás
        envelopeLastSmoothedValue_ = ENVELOPE_SMOOTH_FACTOR * envelopeLastSmoothedValue_ + (1.0f - ENVELOPE_SMOOTH_FACTOR) * current_col_max_amplitude;

        // További simítás: csak jelentős változásokat engedünk át (adaptív küszöb)
        constexpr float ENVELOPE_CHANGE_THRESHOLD = 2.0f; // Minimális változás az átrajzoláshoz
        if (abs(current_col_max_amplitude - envelopeLastSmoothedValue_) < ENVELOPE_CHANGE_THRESHOLD) {
            current_col_max_amplitude = envelopeLastSmoothedValue_;
        }

        // Csak akkor rajzolunk burkológörbét, ha van számottevő jel
        if (column_has_signal || envelopeLastSmoothedValue_ > 0.5f) {
            // VÍZSZINTES NAGYÍTÁS: Csak a középső részt használjuk nagyobb felbontásért
            float displayValue = envelopeLastSmoothedValue_;

            // Intelligens tüske korlát megtartva
            if (displayValue > 150.0f) {
                displayValue = 150.0f + (displayValue - 150.0f) * 0.1f;
            }

            // EREDETI NAGYÍTÁS + vízszintes szétnyújtás
            // A teljes grafikon magasság x%-át használjuk a jobb láthatóságért
            float y_offset_float = (displayValue / 100.0f) * (graphH * UI_TARGET_HEIGHT_UTILIZATION);

            int y_offset_pixels = static_cast<int>(round(y_offset_float));
            y_offset_pixels = std::min(y_offset_pixels, graphH - 4); // Kis margó
            if (y_offset_pixels < 0) {
                y_offset_pixels = 0;
            }

            if (y_offset_pixels > 1) {
                // Szimmetrikus burkoló a középvonaltól - eredeti stílus
                int yUpper_on_sprite = yCenter_on_sprite - y_offset_pixels / 2;
                int yLower_on_sprite = yCenter_on_sprite + y_offset_pixels / 2;

                yUpper_on_sprite = constrain(yUpper_on_sprite, 2, graphH - 3);
                yLower_on_sprite = constrain(yLower_on_sprite, 2, graphH - 3);

                if (yUpper_on_sprite <= yLower_on_sprite) {
                    // Vastagabb vonal a jobb láthatóságért
                    sprite_->drawFastVLine(c, yUpper_on_sprite, yLower_on_sprite - yUpper_on_sprite + 1, TFT_WHITE);
                    // Második vonal a széleken a részletesebb megjelenítésért
                    if (y_offset_pixels > 4) {
                        sprite_->drawPixel(c, yUpper_on_sprite - 1, TFT_WHITE);
                        sprite_->drawPixel(c, yLower_on_sprite + 1, TFT_WHITE);
                    }
                }
            }
        }
    }

    // Frissítjük a magnitude-alapú AGC-t (Envelope: maxRawMagnitude a legnagyobb nyers FFT magnitude)
    updateMagnitudeBasedGain(maxRawMagnitude);

    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief Waterfall renderelése
 */
void UICompSpectrumVis::renderWaterfall() {
    // Audio feldolgozás Core1-en történik, AudioCore1Manager-en keresztül

    int graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        if (!spriteCreated_) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderWaterfall - Sprite nincs létrehozva\n");
        }
        return;
    }

    // Core1 spektrum adatok lekérése
    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    float currentAutoGain = 1.0f;

    bool dataAvailable = getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz, &currentAutoGain);

    // Ha nincs friss adat vagy nincs magnitude adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !magnitudeData || currentBinWidthHz == 0) {
        // Csak a sprite kirakása a korábbi tartalommal
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderWaterfall - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // 1. Adatok eltolása balra a wabuf-ban (ez továbbra is szükséges a wabuf frissítéséhez)
    for (int r = 0; r < bounds.height; ++r) { // A teljes bounds.height magasságon iterálunk a wabuf miatt
        for (int c = 0; c < bounds.width - 1; ++c) {
            wabuf[r][c] = wabuf[r][c + 1];
        }
    }

    const int min_bin_for_wf = std::max(2, static_cast<int>(std::round(AnalyzerConstants::ANALYZER_MIN_FREQ_HZ / currentBinWidthHz)));
    const int max_bin_for_wf = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const int num_bins_in_wf_range = std::max(1, max_bin_for_wf - min_bin_for_wf + 1);

    // 2. Új adatok betöltése a wabuf jobb szélére (a wabuf továbbra is bounds.height magas)
    // Magnitude-alapú AGC használata waterfall-hoz
    float adaptiveScale = getMagnitudeAgcScale(SensitivityConstants::WATERFALL_SENSITIVITY_FACTOR);
    float maxRawMagnitude = 0.0f;

    for (int r = 0; r < bounds.height; ++r) {
        // 'r' (0 to bounds.height-1) leképezése FFT bin indexre a szűkített tartományon belül
        int fft_bin_index =
            min_bin_for_wf + static_cast<int>(std::round(static_cast<float>(r) / std::max(1, (bounds.height - 1)) * (num_bins_in_wf_range - 1)));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_wf, max_bin_for_wf);

        // Q15 direkt float konverzió (hasonló tartomány mint a float FFT)
        float rawMagnitude = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[fft_bin_index]);
        // ELŐSZÖR zajszűrés a nyers adaton
        if (rawMagnitude < (this->radioMode_ == RadioMode::AM ? NOISE_THRESHOLD_AM : NOISE_THRESHOLD_FM)) {
            rawMagnitude = 0.0f;
        }

        // Gyűjtjük a legnagyobb nyers magnitude-ot az AGC-hez
        maxRawMagnitude = std::max(maxRawMagnitude, rawMagnitude);

        // ----------------------------------------
        // AGC vagy manuális erősítés alkalmazása
        // ----------------------------------------
        // AGC esetén a waterfall túlvezérlődik, csökkentjük az erősítést
        if (isAutoGainMode()) {
            adaptiveScale *= 0.95f;
        }
        // UTÁNA erősítés a tiszta jelen
        float amplified = rawMagnitude * adaptiveScale;

        uint8_t finalValue = static_cast<uint8_t>(constrain(amplified, 0.0f, 255.0f));

        wabuf[r][bounds.width - 1] = finalValue;
    }

    // 3. Sprite görgetése és új oszlop kirajzolása
    sprite_->scroll(-1, 0); // Tartalom görgetése 1 pixellel balra

    // Az új (jobb szélső) oszlop kirajzolása a sprite-ra INTERPOLÁCIÓVAL
    // A sprite graphH magas, a wabuf bounds.height magas.
    // Interpolálunk a wabuf bin-ek között a simább megjelenítésért
    constexpr int WF_GRADIENT = 100;

    for (int y_on_sprite = 0; y_on_sprite < graphH; ++y_on_sprite) {
        // y_on_sprite (0..graphH-1) leképezése wabuf indexre (float, interpolációhoz)
        // A vízesés "fentről lefelé" jelenik meg (y=0 felül), a wabuf sorai frekvenciák (r=0 alacsony frekvencia)
        int screen_y_inverted = graphH - 1 - y_on_sprite;
        float r_wabuf_float = (screen_y_inverted * (bounds.height - 1)) / static_cast<float>(std::max(1, graphH - 1));

        // Interpoláció a két legközelebbi wabuf sor között
        int r_wabuf_lower = static_cast<int>(std::floor(r_wabuf_float));
        int r_wabuf_upper = static_cast<int>(std::ceil(r_wabuf_float));

        // Határok ellenőrzése
        r_wabuf_lower = constrain(r_wabuf_lower, 0, bounds.height - 1);
        r_wabuf_upper = constrain(r_wabuf_upper, 0, bounds.height - 1);

        // Interpolációs súly (0.0 - 1.0)
        float frac = r_wabuf_float - r_wabuf_lower;

        // Interpolált magnitude érték
        float val_lower = wabuf[r_wabuf_lower][bounds.width - 1];
        float val_upper = wabuf[r_wabuf_upper][bounds.width - 1];
        float interpolated_val = val_lower * (1.0f - frac) + val_upper * frac;

        // Szín konverzió
        uint16_t color = valueToWaterfallColor(WF_GRADIENT * interpolated_val, 0.0f, 255.0f * WF_GRADIENT, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(bounds.width - 1, y_on_sprite, color);
    }

    // Frissítjük a magnitude-alapú AGC-t (Waterfall: maxRawMagnitude a legnagyobb nyers FFT magnitude)
    updateMagnitudeBasedGain(maxRawMagnitude);

    // Sprite kirakása a képernyőre
    sprite_->pushSprite(bounds.x, bounds.y);

    // Frekvencia feliratok rajzolása, ha még nem történt meg és nincs aktív mód indicator
    if (!modeIndicatorVisible_) {
        renderFrequencyRangeLabels(AnalyzerConstants::ANALYZER_MIN_FREQ_HZ, maxDisplayFrequencyHz_);
    }
}

/**
 * @brief Waterfall szín meghatározása
 */
uint16_t UICompSpectrumVis::valueToWaterfallColor(float val, float min_val, float max_val, byte colorProfileIndex) {
    const uint16_t *colors = (colorProfileIndex == 0) ? FftDisplayConstants::waterFallColors_0 : FftDisplayConstants::waterFallColors_1;
    byte color_size = 16;

    if (val < min_val)
        val = min_val;
    if (val > max_val)
        val = max_val;

    int index = (int)((val - min_val) * (color_size - 1) / (max_val - min_val));
    if (index < 0)
        index = 0;
    if (index >= color_size)
        index = color_size - 1;

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
 * @brief Hangolási segéd renderelése (CW/RTTY waterfall)
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidWaterfall() {
    // Audio feldolgozás Core1-en történik, AudioCore1Manager-en keresztül

    int graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0 || wabuf.empty() || wabuf[0].empty()) {
        if (!spriteCreated_) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderTuningAid - Sprite nincs létrehozva\n");
        }
        return;
    }

    // Core1 spektrum adatok lekérése
    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    float currentAutoGain = 1.0f;

    // Lekérjük az adatokat a Core1-ről
    bool dataAvailable = getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz, &currentAutoGain);
    // Ha nincs friss adat vagy nincs magnitude adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !magnitudeData || currentBinWidthHz == 0) {
        // Csak a sprite kirakása a korábbi tartalommal
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderCwOrRttyTuningAid - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // 1. Sprite scroll lefelé (1 pixel)
    sprite_->scroll(0, 1);

    // Waterfall paraméterek: tuning aid-hez a min-max frekvenciahatárok alapján
    const int min_bin_for_tuning = std::max(2, static_cast<int>(std::round(currentTuningAidMinFreqHz_ / currentBinWidthHz)));
    // actualFftSize is number of bins; don't divide by 2 again
    const int max_bin_for_tuning = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(currentTuningAidMaxFreqHz_ / currentBinWidthHz)));
    const int num_bins_in_tuning_range = std::max(1, max_bin_for_tuning - min_bin_for_tuning + 1);

    // Magnitude-alapú adaptív skálázás a módtól függően
    float adaptiveScale = getMagnitudeAgcScale(SensitivityConstants::TUNING_AID_WATERFALL_SENSITIVITY_FACTOR);
    adaptiveScale = getMagnitudeAgcScale(SensitivityConstants::TUNING_AID_WATERFALL_SENSITIVITY_FACTOR);
    // ----------------------------------------
    // AGC vagy manuális erősítés alkalmazása
    // ----------------------------------------
    // AGC esetén a cwwaterfall túlvezérlődik, csökkentjük az erősítést
    if (isAutoGainMode()) {
        adaptiveScale *= 0.03f;
    }

    float maxRawMagnitude = 0.0f;
    // 2. Új adatok betöltése a legfelső sorba INTERPOLÁCIÓVAL (simább tuning aid)
    constexpr int WF_GRADIENT = 100;

    for (int c = 0; c < bounds.width; ++c) {
        // Pixel → FFT bin leképezés (lebegőpontos index interpolációhoz)
        float ratio_in_display_width = (bounds.width <= 1) ? 0.0f : (static_cast<float>(c) / (bounds.width - 1));
        float exact_bin_index = min_bin_for_tuning + ratio_in_display_width * (num_bins_in_tuning_range - 1);

        // Interpolált magnitude érték (közös helper metódus használata)
        // A getInterpolatedMagnitude már normalizált float-ot ad vissza
        float rawMagnitude = getInterpolatedMagnitude(magnitudeData, exact_bin_index, min_bin_for_tuning, max_bin_for_tuning);
        maxRawMagnitude = std::max(maxRawMagnitude, rawMagnitude);

        float scaledMagnitude = rawMagnitude * adaptiveScale;
        uint8_t finalValue = static_cast<uint8_t>(constrain(scaledMagnitude, 0.0, 255.0));
        wabuf[0][c] = finalValue;

        // Csak a legfelső sort rajzoljuk ki (y=0) interpolált értékkel
        uint16_t color = valueToWaterfallColor(WF_GRADIENT * finalValue, 0.0f, 255.0f * WF_GRADIENT, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(c, 0, color);
    }

    // Frissítjük a magnitude-alapú AGC-t
    updateMagnitudeBasedGain(maxRawMagnitude);

    uint16_t min_freq_displayed = currentTuningAidMinFreqHz_;
    uint16_t max_freq_displayed = currentTuningAidMaxFreqHz_;
    uint16_t displayed_span_hz = max_freq_displayed - min_freq_displayed;

    // 4. Célfrekvencia vonalának kirajzolása a sprite-ra
    if (currentTuningAidType_ == TuningAidType::CW_TUNING || currentTuningAidType_ == TuningAidType::RTTY_TUNING) {

        sprite_->setFreeFont();
        sprite_->setTextSize(1);
        uint16_t label_y = graphH - 4; // Alsó részre

        // Hangolási csík kirajzolása
        if (displayed_span_hz > 0) {
            if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
                // CW: a célfrekvencia pixel pozícióját a tényleges megjelenített tartomány alapján számoljuk ki
                // Use currentTuningAidMin/Max to compute pixel position for the CW center tone
                if (currentTuningAidMaxFreqHz_ > currentTuningAidMinFreqHz_) {
                    float span = static_cast<float>(currentTuningAidMaxFreqHz_ - currentTuningAidMinFreqHz_);
                    float ratio_center = (static_cast<float>(config.data.cwToneFrequencyHz) - static_cast<float>(currentTuningAidMinFreqHz_)) / span;
                    int line_x = static_cast<int>(std::round(ratio_center * (bounds.width - 1)));
                    line_x = constrain(line_x, 0, bounds.width - 1);
                    sprite_->drawFastVLine(line_x, 0, graphH, TUNING_AID_CW_TARGET_COLOR);

                    sprite_->setTextDatum(MC_DATUM);
                    sprite_->setTextColor(TUNING_AID_CW_TARGET_COLOR, TFT_BLACK);
                    sprite_->drawString(String(config.data.cwToneFrequencyHz) + "Hz", line_x, label_y);
                }

            } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
                uint16_t f_mark = config.data.rttyMarkFrequencyHz;
                uint16_t f_space = f_mark - config.data.rttyShiftFrequencyHz;

                // Space vonal (cyan) + címke
                if (f_space >= min_freq_displayed && f_space <= max_freq_displayed) {
                    float ratio_space = (static_cast<float>(f_space) - min_freq_displayed) / displayed_span_hz;
                    uint16_t line_x_space = static_cast<uint16_t>(std::round(ratio_space * (bounds.width - 1)));
                    line_x_space = constrain(line_x_space, 0, bounds.width - 1);
                    sprite_->drawFastVLine(line_x_space, 0, graphH, TUNING_AID_RTTY_SPACE_COLOR);

                    // Space címke
                    sprite_->setTextDatum(BR_DATUM);
                    sprite_->setTextColor(TUNING_AID_RTTY_SPACE_COLOR, TFT_BLACK);
                    sprite_->drawString(String(static_cast<uint16_t>(round(f_space))) + "Hz", line_x_space - 5, label_y);
                }

                // Mark vonal (yellow) + címke
                if (f_mark >= min_freq_displayed && f_mark <= max_freq_displayed) {
                    float ratio_mark = (static_cast<float>(f_mark) - min_freq_displayed) / displayed_span_hz;
                    uint16_t line_x_mark = static_cast<uint16_t>(std::round(ratio_mark * (bounds.width - 1)));
                    line_x_mark = constrain(line_x_mark, 0, bounds.width - 1);
                    sprite_->drawFastVLine(line_x_mark, 0, graphH, TUNING_AID_RTTY_MARK_COLOR);

                    // Mark címke
                    sprite_->setTextDatum(BL_DATUM);
                    sprite_->setTextColor(TUNING_AID_RTTY_MARK_COLOR, TFT_BLACK);
                    sprite_->drawString(String(static_cast<uint16_t>(round(f_mark))) + "Hz", line_x_mark + 2, label_y);
                }
            }
        }
    }

    // Sprite kirakása a képernyőre
    sprite_->pushSprite(bounds.x, bounds.y);

    // Frekvencia feliratok rajzolása, ha még nem történt meg
    renderFrequencyRangeLabels(min_freq_displayed, max_freq_displayed);
}

/**
 * @brief SNR Curve renderelése - frekvencia/SNR burkológörbe
 */
void UICompSpectrumVis::renderSnrCurve() {

    uint16_t graphH = getGraphHeight();
    if (!spriteCreated_ || bounds.width == 0 || graphH <= 0) {
        return;
    }

    // Core1 spektrum adatok lekérése
    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    float currentAutoGain = 1.0f;

    // Lekérjük az adatokat a Core1-ről
    bool dataAvailable = getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz, &currentAutoGain);
    // Ha nincs friss adat vagy nincs magnitude adat, ne rajzoljunk újra (megelőzzük a villogást)
    if (!dataAvailable || !magnitudeData || currentBinWidthHz == 0) {
        // Csak a sprite kirakása a korábbi tartalommal
        UISPECTRUM_DEBUG("UICompSpectrumVis::renderSnrCurve - Nincs elérhető adat a rajzoláshoz\n");
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // Sprite teljes törlése
    sprite_->fillSprite(TFT_BLACK);

    const float MIN_FREQ_HZ = currentTuningAidMinFreqHz_;
    const float MAX_FREQ_HZ = currentTuningAidMaxFreqHz_;

    // Biztonsági ellenőrzés: ha a frekvencia határok még nem inicializálódtak
    if (MIN_FREQ_HZ == 0 || MAX_FREQ_HZ == 0 || MIN_FREQ_HZ >= MAX_FREQ_HZ) {
        static unsigned long lastSnrErrorDebugTime = 0;
        if (Utils::timeHasPassed(lastSnrErrorDebugTime, 10000)) {
            UISPECTRUM_DEBUG("UICompSpectrumVis::renderSnrCurve - Érvénytelen frekvencia határok: MIN=%.2f, MAX=%.2f, automatikus javítás!\n", MIN_FREQ_HZ,
                             MAX_FREQ_HZ);
            lastSnrErrorDebugTime = millis();
        }

        UISPECTRUM_DEBUG("UICompSpectrumVis::renderSnrCurve - Érvénytelen frekvencia határok miatt alapértelmezett értékek beállítása\n");
        // Állítsuk be az alapértelmezett határokat
        currentTuningAidMinFreqHz_ = AnalyzerConstants::ANALYZER_MIN_FREQ_HZ;
        currentTuningAidMaxFreqHz_ = maxDisplayFrequencyHz_;
    }

    const uint16_t min_bin = std::max(2, static_cast<int>(std::round(MIN_FREQ_HZ / currentBinWidthHz)));
    const uint16_t max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(MAX_FREQ_HZ / currentBinWidthHz)));
    const uint16_t num_bins = std::max(1, max_bin - min_bin + 1);

    float maxMagnitude = 0.0f;

    // Előző pont koordinátái a görbe rajzolásához
    int16_t prevX = -1;
    int16_t prevY = -1;

    // Minden pixel oszlophoz kiszámítjuk az SNR értéket interpolációval
    for (uint16_t x = 0; x < bounds.width; x++) {

        // X koordináta frekvenciává konvertálása (lebegőpontos bin index)
        float ratio = (bounds.width <= 1) ? 0.0f : (static_cast<float>(x) / (bounds.width - 1));
        float exact_bin_index = min_bin + ratio * (num_bins - 1);

        // Interpolált magnitude érték (közös helper metódus használata)
        // A getInterpolatedMagnitude már normalizált float-ot ad vissza
        float rawMagnitude = getInterpolatedMagnitude(magnitudeData, exact_bin_index, min_bin, max_bin);

        // ELŐSZÖR zajszűrés a nyers adaton
        if (rawMagnitude < (this->radioMode_ == RadioMode::AM ? NOISE_THRESHOLD_AM : NOISE_THRESHOLD_FM)) {
            rawMagnitude = 0.0f;
        }

        // Magnitude-alapú adaptív skálázás a módtól függően
        float adaptiveScale = getMagnitudeAgcScale(SensitivityConstants::TUNING_AID_SNR_CURVE_SENSITIVITY_FACTOR);

        // UTÁNA erősítés a tiszta jelen
        float snrValue = rawMagnitude * adaptiveScale;
        maxMagnitude = std::max(maxMagnitude, snrValue);

        // ----------------------------------------
        // AGC vagy manuális erősítés alkalmazása a maxMagnitude kikeresése UTÁN!!
        // ----------------------------------------
        // AGC esetén az snrcurve túlvezérlődik, csökkentjük az erősítést
        if (isAutoGainMode()) {
            snrValue *= 0.015f;
        }

        // Y koordináta számítása (invertált, mert a képernyő teteje y=0)
        uint16_t y = graphH - static_cast<int>(constrain(snrValue, 0.0f, static_cast<float>(graphH)));
        y = constrain(y, 0, graphH - 1);

        // Görbe rajzolása - pont összekötése az előzővel
        if (prevX >= 0 && prevY >= 0) {
            //  Vonal rajzolása az előző ponttól a jelenleg számítottig
            sprite_->drawLine(prevX, prevY, x, y, TFT_CYAN);
        }

        // Pontot is rajzolunk a jobb láthatóságért
        sprite_->drawPixel(x, y, TFT_WHITE);

        // Koordináták mentése a következő iterációhoz
        prevX = x;
        prevY = y;
    }

    // Hangolási segédvonalak rajzolása - módtól függően
    uint16_t min_freq_displayed = MIN_FREQ_HZ;
    uint16_t max_freq_displayed = MAX_FREQ_HZ;
    uint16_t displayed_span_hz = max_freq_displayed - min_freq_displayed;

    constexpr int LABEL_Y_POS = 12; // Y pozíció a frekvencia címkéknek

    if (displayed_span_hz > 0) {

        char freqStr[16];
        sprite_->setTextSize(1);
        int labelHeight = sprite_->fontHeight();

        if (currentMode_ == DisplayMode::CwSnrCurve) {
            // CW módban a konfigurált CW tone frekvencia vonala (zöld)
            uint16_t cwFrequency = config.data.cwToneFrequencyHz;

            // CW frekvencia vonal pozíciójának számítása
            if (cwFrequency >= min_freq_displayed && cwFrequency <= max_freq_displayed) {
                float ratio_cw = (static_cast<float>(cwFrequency) - min_freq_displayed) / displayed_span_hz;
                int line_x_cw = static_cast<int>(std::round(ratio_cw * (bounds.width - 1)));
                line_x_cw = constrain(line_x_cw, 0, bounds.width - 1);

                // Először a teljes vonal kirajzolása
                sprite_->drawFastVLine(line_x_cw, 0, graphH, TUNING_AID_CW_TARGET_COLOR);

                // CW frekvencia kiírása a vonal közepére
                sprite_->setTextDatum(MC_DATUM); // Middle Center - szöveg közép középre igazítás

                if (cwFrequency >= 1000) {
                    snprintf(freqStr, sizeof(freqStr), "%.1fkHz", cwFrequency / 1000.0f);
                } else {
                    snprintf(freqStr, sizeof(freqStr), "%dHz", cwFrequency);
                }

                uint16_t w = sprite_->textWidth(freqStr);

                // Töröljük a hátteret 1px margóval, MC_DATUM-hoz igazítva
                sprite_->fillRect(line_x_cw - w / 2 - 1, LABEL_Y_POS - 1, w + 2, labelHeight + 2, TFT_BLACK);
                sprite_->setTextColor(TUNING_AID_CW_TARGET_COLOR, TFT_BLACK); // Szöveg színe és háttérszín
                sprite_->drawString(freqStr, line_x_cw, LABEL_Y_POS);

                // TextDatum visszaállítása alapértelmezett
                sprite_->setTextDatum(TL_DATUM);
            }

        } else if (currentMode_ == DisplayMode::RttySnrCurve) {
            // RTTY módban mark és space vonalak
            uint16_t f_mark = config.data.rttyMarkFrequencyHz;
            uint16_t f_space = f_mark - config.data.rttyShiftFrequencyHz;

            // Space vonal (cyan) + címke
            if (f_space >= min_freq_displayed && f_space <= max_freq_displayed) {

                float ratio_space = (static_cast<float>(f_space) - min_freq_displayed) / displayed_span_hz;
                uint16_t line_x_space = static_cast<uint16_t>(std::round(ratio_space * (bounds.width - 1)));
                line_x_space = constrain(line_x_space, 0, bounds.width - 1);

                // Először a teljes vonal kirajzolása
                sprite_->drawFastVLine(line_x_space, 0, graphH, TUNING_AID_RTTY_SPACE_COLOR);

                // Space frekvencia kiírása a vonal közepére
                snprintf(freqStr, sizeof(freqStr), "%dHz", f_space);

                sprite_->setTextDatum(BR_DATUM);
                int w = sprite_->textWidth(freqStr);

                // Töröljük a hátteret 1px margóval
                sprite_->fillRect(line_x_space - 5 - w - 1, LABEL_Y_POS - 1, w + 2, labelHeight + 2, TFT_BLACK);

                // Space címke
                sprite_->setTextColor(TUNING_AID_RTTY_SPACE_COLOR, TFT_BLACK);
                sprite_->drawString(freqStr, line_x_space - 5, LABEL_Y_POS);
            }

            // Mark vonal (yellow) + címke
            if (f_mark >= min_freq_displayed && f_mark <= max_freq_displayed) {
                float ratio_mark = (static_cast<float>(f_mark) - min_freq_displayed) / displayed_span_hz;
                uint16_t line_x_mark = static_cast<uint16_t>(std::round(ratio_mark * (bounds.width - 1)));
                line_x_mark = constrain(line_x_mark, 0, bounds.width - 1);

                // Először a teljes vonal kirajzolása
                sprite_->drawFastVLine(line_x_mark, 0, graphH, TUNING_AID_RTTY_MARK_COLOR);

                // Mark frekvencia kiírása a vonal közepére
                snprintf(freqStr, sizeof(freqStr), "%dHz", f_mark);

                sprite_->setTextDatum(BL_DATUM);
                int w = sprite_->textWidth(freqStr);

                // Töröljük a hátteret 1px margóval
                sprite_->fillRect(line_x_mark + 5 - 1, LABEL_Y_POS - 1, w + 2, labelHeight + 2, TFT_BLACK);

                // Mark címke
                sprite_->setTextColor(TUNING_AID_RTTY_MARK_COLOR, TFT_BLACK);
                sprite_->drawString(freqStr, line_x_mark + 5, LABEL_Y_POS);
            }
        }
    }

    // Frissítjük a magnitude-alapú AGC-t
    updateMagnitudeBasedGain(maxMagnitude);

    // Sprite kirakása a képernyőre
    sprite_->pushSprite(bounds.x, bounds.y);

    // Frekvencia feliratok rajzolása alsó részen
    renderFrequencyRangeLabels(static_cast<uint16_t>(MIN_FREQ_HZ), static_cast<uint16_t>(MAX_FREQ_HZ));
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
 * @brief Core1 spektrum adatok lekérése (FLOAT - Arduino FFT)
 */
bool UICompSpectrumVis::getCore1SpectrumData(const q15_t **outData, uint16_t *outSize, float *outBinWidth, float *outAutoGain) {

    if (::activeSharedDataIndex < 0 || ::activeSharedDataIndex > 1) {
        // Érvénytelen index a Core1-től - biztonságosan leállunk
        *outData = nullptr;
        *outSize = 0;
        if (outBinWidth) {
            *outBinWidth = 0.0f;
        }
        if (outAutoGain) {
            *outAutoGain = 0.0f;
        }
        UISPECTRUM_DEBUG("UICompSpectrumVis::getCore1SpectrumData - érvénytelen shared index: %d\n", activeSharedDataIndex);
        return false;
    }

    const SharedData &data = ::sharedData[::activeSharedDataIndex];

    *outData = data.fftSpectrumData; // Q15 pointer
    *outSize = data.fftSpectrumSize;

    // DEBUG: SharedData tartalom ellenőrzése
    static uint8_t sharedDebugCounter = 0;
    if (++sharedDebugCounter >= 100) {
        UISPECTRUM_DEBUG("[SHARED DATA] idx=%d, fftSpectrumSize=%d, binWidth=%.2f Hz\n", ::activeSharedDataIndex, data.fftSpectrumSize, data.fftBinWidthHz);
        sharedDebugCounter = 0;
    }

    if (outBinWidth) {
        *outBinWidth = data.fftBinWidthHz;
    }

    if (outAutoGain) {
        *outAutoGain = 1.0f; // TODO: implementálni, ha szükséges
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
    tft.setTextDatum(MC_DATUM); // Middle center
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
        tft.setTextDatum(BC_DATUM); // Bottom center
        tft.drawString(Utils::formatFrequencyString(minDisplayFrequencyHz), bounds.x + bounds.width / 2, indicatorY + indicatorH);

        // Max frekvencia a spektrum felett középen - csak a címke mögötti kis területet töröljük,
        // hogy ne töröljük a teljes felső keretet.
        String topLabel = Utils::formatFrequencyString(maxDisplayFrequencyHz);
        tft.setTextDatum(TC_DATUM); // Top center
        // Approximate character width (pixelekben) a használt betűmérettel
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

        // // Balra igazított min frekvencia
        // tft.setTextDatum(BL_DATUM);
        // tft.drawString(Utils::formatFrequencyString(minDisplayFrequencyHz), bounds.x, indicatorY + indicatorH);

        // // Jobbra igazított max frekvencia
        // tft.setTextDatum(BR_DATUM);
        // tft.drawString(Utils::formatFrequencyString(maxDisplayFrequencyHz), bounds.x + bounds.width, indicatorY + indicatorH);

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
 * @brief Interpolált magnitude érték lekérése két FFT bin között
 *
 * Ez a metódus lineáris interpolációt végez két szomszédos FFT bin magnitude értéke között,
 * hogy simább átmeneteket kapjunk a waterfall és spektrum megjelenítésekben.
 *
 * @param magnitudeData Az FFT magnitude adatok tömbje (int16_t típusú FFT eredmény)
 * @param exactBinIndex Pontos (lebegőpontos) bin index
 * @param minBin Minimum megengedett bin index
 * @param maxBin Maximum megengedett bin index
 * @return Interpolált magnitude érték
 */
float UICompSpectrumVis::getInterpolatedMagnitude(const q15_t *magnitudeData, float exactBinIndex, int minBin, int maxBin) const {
    // Interpoláció a szomszédos bin-ek között
    int bin_low = static_cast<int>(std::floor(exactBinIndex));
    int bin_high = static_cast<int>(std::ceil(exactBinIndex));

    // Határok ellenőrzése
    bin_low = constrain(bin_low, minBin, maxBin);
    bin_high = constrain(bin_high, minBin, maxBin);

    // Interpolációs súly (0.0 - 1.0)
    float frac = exactBinIndex - bin_low;

    // Q15 direkt float konverzió (hasonló tartomány mint a float FFT) és lineáris interpoláció
    float mag_low = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[bin_low]);
    float mag_high = FftDisplayConstants::q15ToDisplayMagnitude(magnitudeData[bin_high]);

    return mag_low * (1.0f - frac) + mag_high * frac;
}