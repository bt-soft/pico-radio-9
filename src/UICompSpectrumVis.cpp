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
#define __UISPECTRUM_DEBUG
#if defined(__DEBUG) && defined(__UISPECTRUM_DEBUG)
#define UISPECTRUM_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define UISPECTRUM_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// A grafikon megjelenítési kitöltése
// Itt egyszerre határozzuk meg a vizuális kitöltést és az AGC célját.
static constexpr float GRAPH_TARGET_HEIGHT_UTILIZATION = 0.85f; // grafikon kitöltés / AGC cél (85%)

//--- Baseline erősítés konstansok spektrum megjelenítéshez (dB) ---
constexpr float LOWRES_BASELINE_GAIN_DB = -70.0f;   // LowRes alaperősítés (-70dB = 0.0001x csillapítás)
constexpr float HIGHRES_BASELINE_GAIN_DB = -6.0f;   // HighRes alaperősítés (-6dB = 0.5x csillapítás)
constexpr float ENVELOPE_BASELINE_GAIN_DB = -65.0f; // Envelope alaperősítés (-65dB = 0.00018x csillapítás)
constexpr float WATERFALL_BASELINE_GAIN_DB = 0.0f;  // Waterfall alaperősítés (0dB = nincs változtatás)

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
    // tuningAidWaterfallDb, tuningAidSnrCurveDb (NOAMP = mód nem elérhető)
    {CW_AF_BANDWIDTH_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, 10.0f, 18.0f},   // 1.5kHz: CW mód (csak tuning aid)
    {RTTY_AF_BANDWIDTH_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, 3.0f, 8.0f},   // 3kHz: RTTY mód (csak tuning aid)
    {AM_AF_BANDWIDTH_HZ, -10.0f, -10.0f, -10.0f, 5.0f, 10.0f, NOAMP, NOAMP}, // 6kHz: AM mód (tuning aid nem elérhető)
    {WEFAX_SAMPLE_RATE_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP}, // 11025Hz: WEFAX mód
    {FM_AF_BANDWIDTH_HZ, 0.0f, 0.0f, -3.0f, 0.0f, 0.0f, NOAMP, NOAMP},       // 15kHz: FM mód (tuning aid nem elérhető)
};
constexpr size_t BANDWIDTH_GAIN_TABLE_SIZE = ARRAY_ITEM_COUNT(BANDWIDTH_GAIN_TABLE);

//--- AGC konstansok ---
constexpr float AGC_GENERIC_MIN_GAIN_VALUE = 0.0001f;
constexpr float AGC_GENERIC_MAX_GAIN_VALUE = 80.0f;

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

// ===== ÚJ EGYSÉGES MAGNITUDE KEZELÉS =====
// Az AudioProcessor Q15 FFT-t használ, a magnitude értékek ~200-500 körüliek
// Ezért nagy gain szükséges (50-500x) a látható megjelenítéshez

/**
 * Q15 magnitude -> float konverzió gain-nel
 * Biztonságos, nem csordulhat túl
 */
static inline float q15ToFloatWithGain(q15_t magQ15, float gain) {
    float magFloat = static_cast<float>(q15Abs(magQ15)) * gain;
    return constrain(magFloat, 0.0f, 500.0f); // Magasabb felső határ a jobb dinamikáért
}

/**
 * Q15 magnitude -> uint8 (0..255) konverzió gain-nel
 */
static inline uint8_t q15ToUint8Safe(q15_t magQ15, float gain) {
    float magFloat = q15ToFloatWithGain(magQ15, gain);
    return static_cast<uint8_t>(magFloat);
}

/**
 * Q15 magnitude -> pixel height konverzió gain-nel (LINEÁRIS)
 */
static inline uint16_t q15ToPixelHeightSafe(q15_t magQ15, float gain, uint16_t maxHeight) {
    float normalized = q15ToFloatWithGain(magQ15, gain) / 500.0f; // 0..1
    uint16_t height = static_cast<uint16_t>(normalized * maxHeight);
    return std::min(height, maxHeight);
}

/**
 * Q15 magnitude -> pixel height NÉGYZETGYÖK alapú skálázással
 * Ez "soft compression" - jobb mint lineáris, de nem túl agresszív mint a log
 * Kis jelek: nagyobb boost, Nagy jelek: kisebb boost
 * Példa: 0.25 -> 0.5 (2x), 0.50 -> 0.707 (1.4x), 1.0 -> 1.0 (1x)
 */
static inline uint16_t q15ToPixelHeightSqrt(q15_t magQ15, float gain, uint16_t maxHeight) {
    float valueNormalized = q15ToFloatWithGain(magQ15, gain) / 500.0f; // 0..1

    if (valueNormalized < 0.001f) {
        return 0;
    }

    // Négyzetgyök kompresszió: sqrt(x) - sima, természetes átmenet
    float compressed = sqrtf(valueNormalized);
    compressed = constrain(compressed, 0.0f, 1.0f);

    uint16_t height = static_cast<uint16_t>(compressed * maxHeight);
    return std::min(height, maxHeight);
}

/**
 * Q15 magnitude -> pixel height GAUSS-LIKE COMPRESSION (SNR curve-hez)
 * Valódi kerek csúcs alakú megjelenítés - javított gauss görbe
 * Exponenciális falloff a nagy értékeknél, természetes kerek csúcs forma
 */
static inline uint16_t q15ToPixelHeightSoftCompression(q15_t magQ15, float gain, uint16_t maxHeight) {
    float valueNormalized = q15ToFloatWithGain(magQ15, gain) / 500.0f; // 0..1+

    if (valueNormalized < 0.001f) {
        return 0;
    }

    // TELJES Újraírás: valódi kerek csúcsok, SEMMI kocka!
    // Egyszerű logaritmikus kompresszió + smooth kerekítés
    float gaussLike;

    if (valueNormalized < 0.001f) {
        gaussLike = 0.0f;
    } else if (valueNormalized > 1.0f) {
        // Nagy értékek: logaritmikus lecsökkenés (természetes kerekítés)
        gaussLike = 1.0f - 0.3f * logf(valueNormalized);
    } else {
        // Normál tartomány: négyzetgyök kompresszió (smooth emelkedés)
        gaussLike = powf(valueNormalized, 0.6f); // Enyhe kompresszió
    }

    // Normalizálás és határolás
    gaussLike = std::min(gaussLike, 1.0f);

    uint16_t height = static_cast<uint16_t>(gaussLike * maxHeight);
    return height;
}

/**
 * Q15 magnitude -> pixel height konverzió LOGARITMIKUS (dB) skálával - OPTIMALIZÁLT
 * Szélesebb dinamikus tartomány, jobb kis jelek láthatósága
 * dB tartomány: -40dB .. +6dB (46dB széles ablak, optimális a spektrum megjelenítéshez)
 */
static inline uint16_t q15ToPixelHeightLogarithmic(q15_t magQ15, float gain, uint16_t maxHeight) {
    float valueNormalized = q15ToFloatWithGain(magQ15, gain) / 500.0f; // 0..1

    if (valueNormalized < 0.00001f) { // Enyhébb küszöb - több kis jel látszik
        return 0;
    }

    // dB számítás: 20*log10(value)
    float dB = 20.0f * log10f(valueNormalized);

    // TOVÁBBI KIBŐVÍTETT dB tartomány a még jobb fokozatos megjelenítéshez:
    // -70dB: nagyon halk jelek (alig látható)
    // +5dB: erős jelek (90% kitérés)
    const float DB_MIN = -70.0f;
    const float DB_MAX = 5.0f;

    float normalized = (dB - DB_MIN) / (DB_MAX - DB_MIN);
    normalized = constrain(normalized, 0.0f, 1.0f);

    uint16_t height = static_cast<uint16_t>(normalized * maxHeight);
    return std::min(height, maxHeight);
}

// RÉGI függvények - backward compatibility (deprecated, ne használd új kódban!)
static inline uint8_t q15ToUint8(q15_t v, int32_t gain_scaled) {
    // FIGYELEM: Nagy gain_scaled esetén túlcsordulhat!
    // Használd helyette: q15ToUint8Safe()
    int32_t abs_val = q15Abs(v);
    if (abs_val == 0)
        return 0;
    int32_t result = (abs_val * gain_scaled) >> 15;
    return (uint8_t)constrain(result, 0, 255);
}

static inline uint16_t q15ToPixelHeight(q15_t v, int32_t gain_scaled, uint16_t max_height) {
    // FIGYELEM: Nagy gain_scaled esetén túlcsordulhat!
    // Használd helyette: q15ToPixelHeightSafe()
    int32_t abs_val = q15Abs(v);
    if (abs_val == 0)
        return 0;
    // gain_scaled = gain_lin * 255
    // result = (q15_val * gain_scaled * max_height) >> 15 / 255
    int32_t temp = (abs_val * gain_scaled) >> 15; // 0..255
    int32_t result = (temp * max_height) >> 8;    // skalalas max_height-ra
    return (uint16_t)constrain(result, 0, (int32_t)max_height);
}

// OPTIMALIZÁLT fixpontos interpoláció Q15 tömbből (Q15 eredmény)

/**
 * Közös gain számítás minden vizualizációs módhoz - HOSSZÚ TÁVÚ ÁTLAGOLÁSSAL
 * @param magnitudeData FFT magnitude adatok
 * @param minBin Minimum bin index
 * @param maxBin Maximum bin index
 * @param isAutoGain Automatikus gain mód
 * @param manualGainDb Manuális gain dB értéke
 * @return Gain érték (50-500x tartományban)
 */
static inline float calculateDisplayGain(const q15_t *magnitudeData, uint16_t minBin, uint16_t maxBin, bool isAutoGain, int8_t manualGainDb) {
    float gain;

    if (isAutoGain) {
        // RMS (négyzetes átlag) számítás: jobb mint a maximum!
        // Az RMS az átlagos jelerősséget méri, nem hagyja hogy egy csúcs mindent elnyomjon
        uint32_t sumOfSquares = 0;
        uint16_t count = 0;

        for (uint16_t i = minBin; i <= maxBin; i++) {
            int32_t absVal = q15Abs(magnitudeData[i]);
            sumOfSquares += (absVal * absVal);
            count++;
        }

        float rms = 0.0f;
        if (count > 0) {
            rms = sqrtf(static_cast<float>(sumOfSquares) / count);
        }

        // Auto-gain RMS alapján - MÉRSEKELT értékek HighRes-hez
        const float targetRmsValue = 120.0f; // Közepes érték
        if (rms > 8.0f) {
            gain = targetRmsValue / rms;
            gain = std::max(gain, 12.0f); // Mérsekelt minimum
        } else {
            gain = 30.0f; // Mérsekelt default
        }

        // HOSSZÚ TÁVÚ simítás: 98% régi + 2% új = ~50 frame időállandó
        // Ez azt jelenti, hogy ~2 másodperc alatt adaptálódik (50 frame @ 25fps)
        static float smoothedGainAuto = 30.0f; // Mérsekelt érték
        smoothedGainAuto = 0.98f * smoothedGainAuto + 0.02f * gain;
        gain = smoothedGainAuto;

    } else {
        // Manuális gain
        if (manualGainDb == 0) {
            gain = 300.0f; // Alapértelmezett
        } else {
            // dB -> lineáris: 6dB = 2x, 12dB = 4x, stb.
            gain = powf(10.0f, static_cast<float>(manualGainDb) / 20.0f) * 150.0f;
        }
    }

    return constrain(gain, 15.0f, 200.0f); // Mérsekelt határok HighRes-hez
}

// OPTIMALIZÁLT fixpontos interpoláció Q15 tömbből (Q15 eredmény) - folytatás
static inline q15_t q15Interpolate(const q15_t *data, float exactIndex, int minIdx, int maxIdx) {
    int idx_low = (int)exactIndex;
    int idx_high = idx_low + 1;
    idx_low = constrain(idx_low, minIdx, maxIdx);
    idx_high = constrain(idx_high, minIdx, maxIdx);

    if (idx_low == idx_high)
        return data[idx_low];

    // Fixpontos lineáris interpoláció (16 bit frakció)
    uint16_t frac16 = (uint16_t)((exactIndex - idx_low) * 65536.0f);
    int32_t low = data[idx_low];
    int32_t high = data[idx_high];
    return (q15_t)((low * (65536 - frac16) + high * frac16) >> 16);
}

// BACKWARD COMPATIBILITY: float visszatérési értékekkel (megtartva a régi interfészt)
static inline float q15InterpolateFloat(const q15_t *data, float exactIndex, int minIdx, int maxIdx) {
    return (float)q15Interpolate(data, exactIndex, minIdx, maxIdx);
}

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
      modeIndicatorHideTime_(0),                       //
      lastTouchTime_(0),                               //
      lastFrameTime_(0),                               //
      envelopeLastSmoothedValue_(0.0f),                //
      barAgcGainFactor_(1.0f),                         //
      barAgcLastUpdateTime_(0),                        //
      barAgcRunningSum_(0.0f),                         //
      barAgcValidCount_(0),                            //
      magnitudeAgcGainFactor_(1.0f),                   //
      magnitudeAgcLastUpdateTime_(0),                  //
      magnitudeAgcRunningSum_(0.0f),                   //
      magnitudeAgcValidCount_(0),                      //
      sprite_(nullptr),                                //
      indicatorFontHeight_(0),                         //
      currentTuningAidType_(TuningAidType::CW_TUNING), //
      currentTuningAidMinFreqHz_(0.0f),                //
      currentTuningAidMaxFreqHz_(0.0f),                //
      wabufWriteCol_(0)                                //
{
    // Flag-ek inicializálása (bitfield)
    flags_.modeIndicatorVisible = false;
    flags_.modeIndicatorDrawn = false;
    flags_.frequencyLabelsDrawn = false;
    flags_.needBorderDrawn = true;
    flags_.spriteCreated = false;
    flags_.isMutedDrawn = false;
    flags_._unused = 0;

    // Inicializáljuk a cache-elt gain értékeket
    cachedGainDb_ = 0;
    cachedGainLinear_ = 1.0f;
    cachedGainScaled_ = 255; // 1.0 * 255

    maxDisplayFrequencyHz_ = radioMode_ == RadioMode::AM ? UICompSpectrumVis::MAX_DISPLAY_FREQUENCY_AM : UICompSpectrumVis::MAX_DISPLAY_FREQUENCY_FM;

    // Peak detection buffer inicializálása
    memset(Rpeak_, 0, sizeof(Rpeak_));
    memset(bar_height_, 0, sizeof(bar_height_));

    // AGC history bufferek inicializálása
    memset(barAgcHistory_, 0, sizeof(barAgcHistory_));
    memset(magnitudeAgcHistory_, 0, sizeof(magnitudeAgcHistory_));

    // Waterfall körkörös buffer inicializálása (1D vektor)
    if (bounds.height > 0 && bounds.width > 0) {
        wabuf_.resize(bounds.width * bounds.height, 0);
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
 * @brief Bar-alapú AGC frissítése (Spektrum módok: LowRes, HighRes) - OPTIMALIZÁLT O(1)
 * @param currentBarMaxValue Jelenlegi frame legnagyobb bar magassága
 */
void UICompSpectrumVis::updateBarBasedGain(float currentBarMaxValue) {

    // Ha némított állapotban vagyunk, az AGC ne működjön!
    if (rtv::muteStat) {
        return;
    }

    // Eltávolítjuk a legrégibb értéket a futó összegből
    float oldValue = barAgcHistory_[barAgcHistoryIndex_];
    if (oldValue > AGC_MIN_SIGNAL_THRESHOLD) {
        barAgcRunningSum_ -= oldValue;
        if (barAgcValidCount_ > 0)
            barAgcValidCount_--;
    }

    // Hozzáadjuk az új értéket
    barAgcHistory_[barAgcHistoryIndex_] = currentBarMaxValue;
    if (currentBarMaxValue > AGC_MIN_SIGNAL_THRESHOLD) {
        barAgcRunningSum_ += currentBarMaxValue;
        barAgcValidCount_++;
    }

    barAgcHistoryIndex_ = (barAgcHistoryIndex_ + 1) % AGC_HISTORY_SIZE;

    // Időalapú gain frissítés
    if (Utils::timeHasPassed(barAgcLastUpdateTime_, AGC_UPDATE_INTERVAL_MS)) {
        // Gyors átlag számítás a futó összegből (O(1) komplexitás!)
        float averageMax = (barAgcValidCount_ > 0) ? (barAgcRunningSum_ / barAgcValidCount_) : 0.0f;

        if (averageMax > AGC_MIN_SIGNAL_THRESHOLD) {
            float targetHeight = static_cast<float>(getGraphHeight()) * GRAPH_TARGET_HEIGHT_UTILIZATION;
            float idealGain = targetHeight / averageMax;
            float newGain = AGC_SMOOTH_FACTOR * idealGain + (1.0f - AGC_SMOOTH_FACTOR) * barAgcGainFactor_;
            barAgcGainFactor_ = constrain(newGain, AGC_GENERIC_MIN_GAIN_VALUE, AGC_GENERIC_MAX_GAIN_VALUE);
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
    if (configValue <= static_cast<uint8_t>(DisplayMode::SpectrumBarWithWaterfall)) {
        return static_cast<DisplayMode>(configValue);
    }
    return DisplayMode::Off;
}

/**
 * @brief Dialog eltűnésekor meghívódó metódus
 */
void UICompSpectrumVis::onDialogDismissed() {
    // Dialog épp eltűnt - újra kell rajzolni a keretet és a frekvencia feliratokat is
    flags_.needBorderDrawn = true;      // Rajzoljuk újra a keretet
    flags_.frequencyLabelsDrawn = true; // Frissítsük a frekvencia feliratokat is
    UIComponent::onDialogDismissed();   // Hívjuk meg az ősosztály implementációját (markForRedraw)
}

/**
 * @brief Handles the mute state, drawing a message if muted.
 * @return true if rendering should be halted due to mute, false otherwise.
 */
bool UICompSpectrumVis::handleMuteState() {
#if not defined TEST_DO_NOT_PROCESS_MUTED_STATE
    if (rtv::muteStat) {
        if (!flags_.isMutedDrawn) {
            drawFrame();
            drawMutedMessage();
            flags_.isMutedDrawn = true;
        }
        return true; // Halt further rendering
    }

    if (flags_.isMutedDrawn) {
        flags_.isMutedDrawn = false;
        flags_.needBorderDrawn = true; // Redraw frame after unmuting
    }
#endif
    return false; // Continue rendering
}

/**
 * @brief Handles the visibility and timing of the mode indicator display.
 */
void UICompSpectrumVis::handleModeIndicator() {
    // Show indicator if it's supposed to be visible and hasn't been drawn yet
    if (flags_.modeIndicatorVisible && !flags_.modeIndicatorDrawn) {
        renderModeIndicator();
        flags_.modeIndicatorDrawn = true;
    }

    // Hide indicator after timeout
    if (flags_.modeIndicatorVisible && millis() > modeIndicatorHideTime_) {
        flags_.modeIndicatorVisible = false;
        flags_.modeIndicatorDrawn = false;

        // Clear the area where the indicator was
        int indicatorY = bounds.y + bounds.height;
        tft.fillRect(bounds.x - 3, indicatorY, bounds.width + 6, 12, TFT_BLACK);

        // Allow frequency labels to be redrawn
        flags_.frequencyLabelsDrawn = true;
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

    if (flags_.needBorderDrawn) {
        drawFrame();
        flags_.needBorderDrawn = false;
    }

    // Biztonsági ellenőrzés: ha az aktuális mód nem elérhető, váltás elérhető módra
    if (!isModeAvailable(currentMode_)) {
        DecoderId activeDecoder = audioController.getActiveDecoder();

        // CW dekóder: váltás CW waterfall módra
        if (activeDecoder == ID_DECODER_CW) {
            setCurrentDisplayMode(DisplayMode::CWWaterfall);
        }
        // RTTY dekóder: váltás RTTY waterfall módra
        else if (activeDecoder == ID_DECODER_RTTY) {
            setCurrentDisplayMode(DisplayMode::RTTYWaterfall);
        }
        // AM/SSB/FM mód (nincs CW/RTTY dekóder): váltás spektrum LowRes módra
        else {
            setCurrentDisplayMode(DisplayMode::SpectrumLowRes);
        }
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
        case DisplayMode::SpectrumBarWithWaterfall:
            renderSpectrumBarWithWaterfall();
            break;
        case DisplayMode::CWWaterfall:
        case DisplayMode::RTTYWaterfall:
            renderCwOrRttyTuningAidWaterfall();
            break;

        case DisplayMode::CwSnrCurve:
        case DisplayMode::RttySnrCurve:
            renderCwOrRttyTuningAidSnrCurve();
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

    if (flags_.spriteCreated) { // Ha létezik sprite egy korábbi módból
        sprite_->deleteSprite();
        flags_.spriteCreated = false;
    }

    // Sprite használata MINDEN módhoz (kivéve Off)
    if (modeToPrepareForDisplayMode != DisplayMode::Off) {
        uint16_t graphH = getGraphHeight();
        if (bounds.width > 0 && graphH > 0) {
            sprite_->setColorDepth(16); // RGB565
            flags_.spriteCreated = (sprite_->createSprite(bounds.width, graphH) != nullptr);
            if (flags_.spriteCreated) {
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
        if (flags_.spriteCreated) {
            sprite_->fillSprite(TFT_BLACK);
        }

        // Envelope reset mód váltáskor
        if (modeToPrepareForDisplayMode == DisplayMode::Envelope) {
            envelopeLastSmoothedValue_ = 0.0f; // Simított érték nullázása
            // wabuf_ buffer teljes törlése hogy tiszta vonallal kezdődjön az envelope
            std::fill(wabuf_.begin(), wabuf_.end(), 0); // 1D buffer t�rl�s
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
uint8_t UICompSpectrumVis::getIndicatorHeight() const { return flags_.modeIndicatorVisible ? 10 : 0; }

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
        flags_.frequencyLabelsDrawn = true;
    } else if (sdToUse != nullptr) {
        // Egyéb módokban a SharedData alapján
        uint16_t minHz = sdToUse->displayMinFreqHz ? sdToUse->displayMinFreqHz : MIN_AUDIO_FREQUENCY_HZ;
        uint16_t maxHz = sdToUse->displayMaxFreqHz ? sdToUse->displayMaxFreqHz : static_cast<uint16_t>(maxDisplayFrequencyHz_);
        if (maxHz < minHz + 100) {
            maxHz = minHz + 100;
        }
        maxDisplayFrequencyHz_ = maxHz;
        flags_.frequencyLabelsDrawn = true;
    }
}

/**
 * @brief Módok közötti váltás
 */
void UICompSpectrumVis::cycleThroughModes() {

    uint8_t nextMode = static_cast<uint8_t>(currentMode_) + 1;

    // Körbe járunk maximum DisplayMode::SpectrumBarWithWaterfall-ig
    if (nextMode > static_cast<uint8_t>(DisplayMode::SpectrumBarWithWaterfall)) {
        nextMode = static_cast<uint8_t>(DisplayMode::Off);
    }

    // Keresünk egy elérhető módot (maximum 12 lépés hogy ne legyen végtelen ciklus)
    uint8_t attempts = 0;
    while (!isModeAvailable(static_cast<DisplayMode>(nextMode)) && attempts < 12) {
        nextMode++;
        if (nextMode > static_cast<uint8_t>(DisplayMode::SpectrumBarWithWaterfall)) {
            nextMode = static_cast<uint8_t>(DisplayMode::Off);
        }
        attempts++;
    }

    // Új mód beállítása
    setCurrentDisplayMode(static_cast<DisplayMode>(nextMode));

    // Config-ba is beállítjuk (mentésre) az aktuális módot
    if (RadioMode::FM == radioMode_) {
        config.data.audioModeFM = nextMode;
    } else {
        config.data.audioModeAM = nextMode;
    }

    // AGC reset mód váltáskor
    resetMagnitudeAgc();
    resetBarAgc();
}

/**
 * @brief Módok megjelenítésének indítása
 */
void UICompSpectrumVis::startShowModeIndicator() {
    // Mode indicator megjelenítése 20 másodpercig
    flags_.modeIndicatorVisible = true;
    flags_.modeIndicatorDrawn = false; // A flag visszaállítása, hogy az indikátor azonnal megjelenhessen
    flags_.needBorderDrawn = true;     // Kényszerítjük a keret újrarajzolását

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
    // FONTOS: A tuning aid feltételeket ELŐRE kell venni, mert forEnvelope és forWaterfall is true lehet tuning aid módokban!
    auto getDbFromTable = [forEnvelope, forWaterfall, forTuningAid, forSnrCurve, forLowResBar, forHighResBar,
                           forOscilloscope](const BandwidthScaleConfig &cfg) -> float {
        // Tuning aid módok ELŐSZÖR (ezek specifikusabbak)
        if (forTuningAid && forSnrCurve)
            return cfg.tuningAidSnrCurveDb;
        if (forTuningAid)
            return cfg.tuningAidWaterfallDb;
        // Általános módok UTÁNA
        if (forEnvelope)
            return cfg.envelopeGainDb;
        if (forWaterfall)
            return cfg.waterfallGainDb;
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

            // OPTIMALIZÁLÁS: Azonnal számoljuk ki a lineáris formát is (powf eliminálása a render loop-ból!)
            cachedGainLinear_ = powf(10.0f, cachedGainDb_ / 20.0f);

            // Egyszerusitett fixpont: gain * 255 (32-bit nativ muveletek!)
            // q15ToUint8: (q15_val * gain_scaled) >> 15 = 0..255
            cachedGainScaled_ = (int32_t)(cachedGainLinear_ * 255.0f);

            UISPECTRUM_DEBUG(
                "UICompSpectrumVis::computeCachedGain() - currentBandwidthHz_=%d, cachedGainDb_=%.2f, cachedGainLinear_=%.4f, cachedGainScaled_=%d\n",
                this->currentBandwidthHz_, cachedGainDb_, cachedGainLinear_, cachedGainScaled_);

            return;
        }
    }

    Utils::beepError(); // Hibára figyelmeztető csippanás
    UISPECTRUM_DEBUG(
        "UICompSpectrumVis::computeCachedGain() - Nincs pontos egyezés a sávszélesség táblázatban, interpoláció szükséges. currentBandwidthHz_=%d\n",
        this->currentBandwidthHz_);
}

/**
 * @brief Módkijelző láthatóságának beállítása
 */
void UICompSpectrumVis::setModeIndicatorVisible(bool visible) {
    flags_.modeIndicatorVisible = visible;
    flags_.modeIndicatorDrawn = false; // A rajzolási flag visszaállítása, ha megváltozik a láthatóság
    if (visible) {
        modeIndicatorHideTime_ = millis() + 20000; // 20 másodperc
    }
}

/**
 * @brief Ellenőrzi, hogy egy megjelenítési mód elérhető-e az aktuális dekóder alapján
 */
bool UICompSpectrumVis::isModeAvailable(DisplayMode mode) const {
    // Aktív dekóder lekérése
    DecoderId activeDecoder = audioController.getActiveDecoder();

    // CW dekóder: csak CW tuning aid módok
    if (activeDecoder == ID_DECODER_CW) {
        if (mode == DisplayMode::CWWaterfall || mode == DisplayMode::CwSnrCurve) {
            return true;
        }
        return false; // Minden más mód blokkolva
    }

    // RTTY dekóder: csak RTTY tuning aid módok
    if (activeDecoder == ID_DECODER_RTTY) {
        if (mode == DisplayMode::RTTYWaterfall || mode == DisplayMode::RttySnrCurve) {
            return true;
        }
        return false; // Minden más mód blokkolva
    }

    // AM/SSB/FM mód (nincs CW/RTTY dekóder): tuning aid módok nem elérhetők
    if (activeDecoder != ID_DECODER_CW && activeDecoder != ID_DECODER_RTTY) {
        if (mode == DisplayMode::CWWaterfall || mode == DisplayMode::RTTYWaterfall || mode == DisplayMode::CwSnrCurve || mode == DisplayMode::RttySnrCurve) {
            return false;
        }
    }

    // Minden más esetben elérhető
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
    int16_t textX = bounds.x + bounds.width / 2;
    int16_t textY = bounds.y + (bounds.height - 1) / 2; // Pontos középre igazítás, figyelembe véve az alsó border-t
    tft.drawString("OFF", textX, textY);
}

/**
 * @brief Waterfall szín meghatározása
 */
uint16_t UICompSpectrumVis::valueToWaterfallColor(float val, float min_val, float max_val, byte colorProfileIndex) {
    const uint16_t *colors = (colorProfileIndex == 0) ? FftDisplayConstants::waterFallColors_0 : FftDisplayConstants::waterFallColors_1;
    constexpr uint8_t color_size = 16;

    val = constrain(val, min_val, max_val);

    uint8_t index = (uint8_t)((val - min_val) * (color_size - 1) / (max_val - min_val));
    index = constrain(index, 0, color_size - 1);

    return colors[index];
}

/**
 * @brief Egyszerűsített waterfall szín konverzió 0-255 értékhez (uint8_t)
 * @param val Intenzitás érték (0-255)
 * @param colorProfileIndex Színprofil index (0 vagy 1)
 * @return RGB565 színkód
 */
uint16_t UICompSpectrumVis::valueToWaterfallColor(uint8_t val, byte colorProfileIndex) {
    const uint16_t *colors = (colorProfileIndex == 0) ? FftDisplayConstants::waterFallColors_0 : FftDisplayConstants::waterFallColors_1;
    constexpr uint8_t color_size = 16;

    // Egyszerű lineáris mapping: 0-255 → 0-15
    uint8_t index = (val * (color_size - 1)) / 255;
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
            // CW: Szélesebb span a vékonyabb megjelenítéshez
            uint16_t centerFreq = config.data.cwToneFrequencyHz;
            uint16_t hfBandwidthHz = CW_AF_BANDWIDTH_HZ; // Alapértelmezett CW sávszélesség

            float cwSpanHz = std::max(2000.0f, static_cast<float>(hfBandwidthHz * 2)); // Nagyobb tartomány! (1200->2000Hz minimum)

            currentTuningAidMinFreqHz_ = centerFreq - cwSpanHz / 2;
            currentTuningAidMaxFreqHz_ = centerFreq + cwSpanHz / 2;

        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            // RTTY: Adaptív tartomány - shift frekvenciához igazított + padding
            uint16_t f_mark = config.data.rttyMarkFrequencyHz;
            uint16_t f_space = f_mark - config.data.rttyShiftFrequencyHz;

            // Mindig a mark/space között legyen a központ
            float f_center = (f_mark + f_space) / 2.0f;

            // Adaptív span: shift + 1000Hz padding (500Hz mindkét oldalon)
            float span = config.data.rttyShiftFrequencyHz + 1000.0f;

            currentTuningAidMinFreqHz_ = f_center - span / 2.0f;
            currentTuningAidMaxFreqHz_ = f_center + span / 2.0f;

        } else {
            // OFF_DECODER: alapértelmezett tartomány
            currentTuningAidMinFreqHz_ = 0.0f;
            currentTuningAidMaxFreqHz_ = maxDisplayFrequencyHz_;
        }

        // Ha változott a frekvencia tartomány, invalidáljuk a buffert (csak waterfall módokhoz)
        if ((typeChanged || oldMinFreq != currentTuningAidMinFreqHz_ || oldMaxFreq != currentTuningAidMaxFreqHz_) &&
            (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall)) {
            std::fill(wabuf_.begin(), wabuf_.end(), 0); // 1D buffer t�rl�s
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
    uint16_t relative_bin_index = fft_bin_index - min_bin_low_res;

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
    if (flags_.isMutedDrawn) {
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

    flags_.isMutedDrawn = true;
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
        case DisplayMode::SpectrumBarWithWaterfall:
            modeText = "Bar + Waterfall";
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
    if (!flags_.modeIndicatorVisible)
        return;

    uint8_t indicatorH = getIndicatorHeight();
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
    int16_t indicatorY = bounds.y + bounds.height; // Közvetlenül a keret alatt kezdődik
    tft.fillRect(bounds.x - 4, indicatorY, bounds.width + 8, 10, TFT_BLACK);

    // Szöveg kirajzolása a komponens alján + indikátor terület közepén
    // Az Y koordináta a szöveg alapvonalát jelenti (az indikátor terület alja)
    tft.drawString(modeText, bounds.x + bounds.width / 2, indicatorY + indicatorH);
}

/**
 * @brief A látható frekvencia tartomány címkéinek kirajzolása
 * a mode indikátor helyére amikor az eltűnik
 * @param minDisplayFrequencyHz Az aktuálisan megjelenített minimum frekvencia Hz-ben
 * @param maxDisplayFrequencyHz Az aktuálisan megjelenített maximum frekvencia Hz-ben
 */
void UICompSpectrumVis::renderFrequencyRangeLabels(uint16_t minDisplayFrequencyHz, uint16_t maxDisplayFrequencyHz) {

    if (!flags_.frequencyLabelsDrawn) {
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
        tft.setTextDatum(TC_DATUM);            // Felső-középre igazítás
        constexpr uint8_t approxCharWidth = 6; // jó közelítés a setTextSize(1) esetén
        int16_t textWidth = topLabel.length() * approxCharWidth;

        // Kis margó a szöveg körül
        constexpr uint8_t bgMargin = 4;
        int16_t centerX = bounds.x + bounds.width / 2;
        int16_t rectX = centerX - (textWidth / 2) - bgMargin;
        int16_t rectW = textWidth + bgMargin * 2;

        // Clamp a téglalapot a komponens területére
        if (rectX < bounds.x) {
            rectX = bounds.x;
        }
        if (rectX + rectW > bounds.x + bounds.width) {
            rectW = (bounds.x + bounds.width) - rectX;
        }

        // Emeljük a címkét 2 pixellel magasabbra a kérés szerint.
        int16_t rectY = bounds.y - 16; // háttér téglalap kezdete kicsit fentebb

        // Clamp rectY hogy ne lépjen ki túl messze a komponens fölé
        if (rectY < bounds.y - 20) {
            rectY = bounds.y - 20;
        }
        tft.fillRect(rectX, rectY, rectW, 14, TFT_BLACK);

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

    flags_.frequencyLabelsDrawn = false;
}

/**
 * @brief Közös helper függvény: CW/RTTY frekvencia címkék rajzolása fekete háttérrel
 *
 * Optimalizáció: Ez a függvény kivonja a közös label rajzolási logikát,
 * amelyet a renderCwOrRttyTuningAidWaterfall() és renderSnrCurve() egyaránt használ.
 *
 * @param min_freq Minimális frekvencia (Hz) a grafikon tartományában
 * @param max_freq Maximális frekvencia (Hz) a grafikon tartományában
 * @param graphH Grafikon magassága pixelben
 */
void UICompSpectrumVis::renderTuningAidFrequencyLabels(float min_freq, float max_freq, uint16_t graphH) {
    float freq_range = max_freq - min_freq;
    if (freq_range <= 0) {
        return; // Érvénytelen frekvencia tartomány
    }

    // Sprite text beállítások (egyszer, nem minden label-re külön)
    sprite_->setTextDatum(TC_DATUM);             // Felül középre igazítva a méretszámításhoz
    sprite_->setTextColor(TFT_WHITE, TFT_BLACK); // Fehér szöveg, fekete háttér
    sprite_->setFreeFont();
    sprite_->setTextSize(1);

    constexpr int8_t PADDING = 3; // 3px padding minden irányban (felül, alul, bal, jobb)

    // Lambda helper: egy frekvencia címke kirajzolása fekete háttérrel
    auto drawLabelWithBackground = [&](uint16_t freq, const char *prefix = nullptr) {
        if (freq < min_freq || freq > max_freq) {
            return; // Címke kívül esik a tartományon
        }

        // X pozíció számítása a frekvencia alapján
        int16_t x_pos = round(((freq - min_freq) / freq_range) * (bounds.width - 1));

        // Formázott szöveg készítése (prefix opcionális: "M:" vagy "S:")
        char buf[16];
        if (prefix) {
            snprintf(buf, sizeof(buf), "%s%u", prefix, freq);
        } else {
            snprintf(buf, sizeof(buf), "%u", freq);
        }

        // Szöveg méret kiszámítása (sprite API)
        int16_t text_w = sprite_->textWidth(buf);
        int16_t text_h = sprite_->fontHeight();

        // Fekete háttér téglalap koordinátái (szöveg körül padding-gel)
        int16_t box_x = x_pos - text_w / 2 - PADDING;
        int16_t box_y = graphH - text_h - PADDING * 2;
        int16_t box_w = text_w + PADDING * 2;
        int16_t box_h = text_h + PADDING * 2;

        // Rajzolás: 1) fekete háttér, 2) fehér szöveg
        sprite_->fillRect(box_x, box_y, box_w, box_h, TFT_BLACK);
        sprite_->drawString(buf, x_pos, box_y + PADDING);
    };

    // CW mód: egyetlen frekvencia címke (pl. "800")
    if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
        uint16_t cw_freq = config.data.cwToneFrequencyHz;
        drawLabelWithBackground(cw_freq);
    } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
        // RTTY mód: két frekvencia címke (mark="M:2125", space="S:1955")

        uint16_t mark_freq = config.data.rttyMarkFrequencyHz;
        uint16_t space_freq = mark_freq - config.data.rttyShiftFrequencyHz;

        drawLabelWithBackground(mark_freq, "M:");  // Mark frekvencia "M:" prefix-szel
        drawLabelWithBackground(space_freq, "S:"); // Space frekvencia "S:" prefix-szel
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
 * @brief PROFESSZIONÁLIS spektrum renderelés (Low és High resolution)
 *
 * Teljesen újraírva a Q15 FFT környezethez:
 * - Logaritmikus (dB) skálázás
 * - Peak hold funkció
 * - Smooth bar release
 * - AGC/Manuális gain támogatás
 *
 * @param isLowRes true = LowRes (16 sávos bar), false = HighRes (pixel-pontosságú)
 */
void UICompSpectrumVis::renderSpectrumBar(bool isLowRes) {

    // ===== VALIDÁCIÓ =====
    uint8_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // ===== FFT BIN TARTOMÁNY =====
    const uint16_t minBin = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t maxBin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const uint16_t numBins = std::max(1, maxBin - minBin + 1);

    // ===== GAIN SZÁMÍTÁS (AGC vagy manuális) + BASELINE =====
    // Jelenleg: automatikus gain keresés az aktuális FFT adatokból
    // Később: config.data.audioFftGainConfigAm/Fm használata manuális módban
    int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float displayGain = calculateDisplayGain(magnitudeData, minBin, maxBin, isAutoGainMode(), gainCfg);

    // Baseline erősítés alkalmazása dB formátumban (LowRes vs HighRes)
    float baselineGainDb;
    if (isLowRes) {
        baselineGainDb = LOWRES_BASELINE_GAIN_DB; // LowRes baseline
    } else {
        baselineGainDb = HIGHRES_BASELINE_GAIN_DB; // HighRes baseline
    }

    // dB -> lineáris konverzió és alkalmazás
    float baselineMultiplier = powf(10.0f, baselineGainDb / 20.0f);
    displayGain *= baselineMultiplier;

    // ===== TIMING KONSTANSOK =====
    const uint8_t BAR_FALL_SPEED = 2;    // Bar esési sebesség (pixel/frame)
    const uint8_t PEAK_HOLD_FRAMES = 30; // Peak tartás ideje (frame-ekben, ~500ms @ 60fps)
    const uint8_t PEAK_FALL_SPEED = 1;   // Peak esési sebesség (pixel/frame)

    if (isLowRes) {
        // ╔═══════════════════════════════════════════════════════════════════╗
        // ║  LOW RESOLUTION MODE (16 BAR)                                     ║
        // ╚═══════════════════════════════════════════════════════════════════╝

        // Layout számítás
        const uint8_t numBars = LOW_RES_BANDS;
        uint8_t barWidth = (bounds.width - (numBars - 1) * BAR_GAP_PIXELS) / numBars;
        barWidth = std::max((uint8_t)1, barWidth);
        uint8_t barSpacing = barWidth + BAR_GAP_PIXELS;
        int16_t xOffset = (bounds.width - (numBars * barWidth + (numBars - 1) * BAR_GAP_PIXELS)) / 2;

        // 1. FFT bin -> band mapping (maximum érték keresése minden bandben)
        q15_t bandMaxValues[LOW_RES_BANDS] = {0};
        for (uint16_t bin = minBin; bin <= maxBin; bin++) {
            uint8_t bandIdx = getBandVal(bin, minBin, numBins, numBars);
            if (bandIdx < numBars) {
                q15_t absVal = q15Abs(magnitudeData[bin]);
                if (absVal > bandMaxValues[bandIdx]) {
                    bandMaxValues[bandIdx] = absVal;
                }
            }
        }

        // 2. LOGARITMIKUS (dB) konverzió -> pixel magasság
        // Ez ÖNMAGÁBAN kezeli a dinamikus tartományt, nincs szükség arányosításra!
        uint16_t targetHeights[LOW_RES_BANDS] = {0};
        for (uint8_t i = 0; i < numBars; i++) {
            targetHeights[i] = q15ToPixelHeightLogarithmic(bandMaxValues[i], displayGain, graphH);
        }

        // 3. Smooth bar release (gyors felfutás, lassú esés)
        static uint8_t barFallTimer = 0;
        bool shouldFall = (++barFallTimer % 2 == 0); // Lelassítás: minden 2. frame-ben

        for (uint8_t i = 0; i < numBars; i++) {
            if (targetHeights[i] > bar_height_[i]) {
                // Felfutás: azonnal
                bar_height_[i] = targetHeights[i];
            } else if (shouldFall && bar_height_[i] > 0) {
                // Esés: lassított
                uint16_t diff = bar_height_[i] - targetHeights[i];
                uint8_t fallSpeed = (diff > 20) ? BAR_FALL_SPEED * 2 : BAR_FALL_SPEED;
                bar_height_[i] = (bar_height_[i] > fallSpeed) ? (bar_height_[i] - fallSpeed) : 0;
            }
        }

        // 4. Peak hold (csúcsérték tartás)
        static uint8_t peakHoldCounters[LOW_RES_BANDS] = {0};
        static uint8_t peakFallTimer = 0;
        bool shouldPeakFall = (++peakFallTimer % 4 == 0); // Lassabb peak esés

        for (uint8_t i = 0; i < numBars; i++) {
            if (bar_height_[i] >= Rpeak_[i]) {
                // Új peak érték
                Rpeak_[i] = bar_height_[i];
                peakHoldCounters[i] = PEAK_HOLD_FRAMES; // Reset hold timer
            } else {
                // Peak tartás vagy esés
                if (peakHoldCounters[i] > 0) {
                    peakHoldCounters[i]--; // Hold phase
                } else if (shouldPeakFall && Rpeak_[i] > 0) {
                    Rpeak_[i] = (Rpeak_[i] > PEAK_FALL_SPEED) ? (Rpeak_[i] - PEAK_FALL_SPEED) : 0;
                }
            }
        }

        // 5. Rajzolás
        sprite_->fillRect(0, 0, bounds.width, graphH, TFT_BLACK); // Teljes törlés

        for (uint8_t i = 0; i < numBars; i++) {
            uint16_t xPos = xOffset + i * barSpacing;

            // Bar rajzolása (sima zöld szín)
            if (bar_height_[i] > 0) {
                int16_t yStart = graphH - bar_height_[i];
                sprite_->fillRect(xPos, yStart, barWidth, bar_height_[i], TFT_GREEN);
            }

            // Peak marker (cián vonal)
            if (Rpeak_[i] > 2) {
                int16_t yPeak = graphH - Rpeak_[i];
                sprite_->fillRect(xPos, yPeak, barWidth, 2, TFT_CYAN);
            }
        }

    } else {
        // ╔═══════════════════════════════════════════════════════════════════╗
        // ║  HIGH RESOLUTION MODE (pixel-by-pixel)                            ║
        // ╚═══════════════════════════════════════════════════════════════════╝

        // Smooth buffer inicializálás
        if (highresSmoothedCols.size() != bounds.width) {
            highresSmoothedCols.assign(bounds.width, 0.0f);
        }

        // Peak hold bufferek
        static std::vector<uint16_t> hiResPeaks(bounds.width, 0);
        static std::vector<uint8_t> hiResPeakHoldCounters(bounds.width, 0);
        if (hiResPeaks.size() != bounds.width) {
            hiResPeaks.assign(bounds.width, 0);
            hiResPeakHoldCounters.assign(bounds.width, 0);
        }

        // 1. FFT bin -> pixel mapping és LOGARITMIKUS (dB) konverzió
        std::vector<uint16_t> targetHeights(bounds.width, 0);
        for (uint8_t x = 0; x < bounds.width; x++) {
            float ratio = (bounds.width > 1) ? static_cast<float>(x) / (bounds.width - 1) : 0.0f;
            uint16_t binIdx = minBin + static_cast<uint16_t>(ratio * (numBins - 1));
            binIdx = constrain(binIdx, minBin, maxBin);

            targetHeights[x] = q15ToPixelHeightLogarithmic(magnitudeData[binIdx], displayGain, graphH);
        }

        // 2. Temporal smoothing (IIR szűrő)
        const float SMOOTH_ALPHA = 0.7f; // 0=gyors, 1=lassú
        for (uint8_t x = 0; x < bounds.width; x++) {
            highresSmoothedCols[x] = SMOOTH_ALPHA * highresSmoothedCols[x] + (1.0f - SMOOTH_ALPHA) * targetHeights[x];
        }

        // 3. Peak hold logika
        static uint8_t hiResPeakFallTimer = 0;
        bool shouldPeakFall = (++hiResPeakFallTimer % 4 == 0);

        for (uint8_t x = 0; x < bounds.width; x++) {
            uint16_t currentHeight = static_cast<uint16_t>(highresSmoothedCols[x] + 0.5f);

            if (currentHeight >= hiResPeaks[x]) {
                hiResPeaks[x] = currentHeight;
                hiResPeakHoldCounters[x] = PEAK_HOLD_FRAMES;
            } else {
                if (hiResPeakHoldCounters[x] > 0) {
                    hiResPeakHoldCounters[x]--;
                } else if (shouldPeakFall && hiResPeaks[x] > 0) {
                    hiResPeaks[x] = (hiResPeaks[x] > PEAK_FALL_SPEED) ? (hiResPeaks[x] - PEAK_FALL_SPEED) : 0;
                }
            }
        }

        // 4. Rajzolás
        sprite_->fillRect(0, 0, bounds.width, graphH, TFT_BLACK);

        for (uint8_t x = 0; x < bounds.width; x++) {
            uint16_t height = static_cast<uint16_t>(highresSmoothedCols[x] + 0.5f);
            height = constrain(height, 0, graphH);

            // Bar oszlop (skyblue gradient)
            if (height > 0) {
                int16_t yStart = graphH - height;
                sprite_->drawFastVLine(x, yStart, height, TFT_SKYBLUE);
            }

            // Peak pixel (zöld)
            if (hiResPeaks[x] > 1) {
                int16_t yPeak = graphH - hiResPeaks[x];
                sprite_->drawPixel(x, yPeak, TFT_GREEN);
            }
        }
    }

    // ===== VÉGSŐ RENDER =====
    sprite_->pushSprite(bounds.x, bounds.y);
    renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
}

/**
 * @brief Oszcilloszkóp renderelése
 */
void UICompSpectrumVis::renderOscilloscope() {
    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const int16_t *osciRawData = nullptr;
    uint16_t sampleCount = 0;
    if (!getCore1OscilloscopeData(&osciRawData, &sampleCount) || !osciRawData || sampleCount <= 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // AM módban túl sok minta esetén limitáljuk a megjelenítést
    if (this->radioMode_ == RadioMode::AM && sampleCount > 512) {
        sampleCount = 128;
    }

    sprite_->fillSprite(TFT_BLACK);
    sprite_->drawFastHLine(0, graphH / 2, bounds.width, TFT_DARKGREY);

    int32_t max_abs = 1;
    double sum_sq = 0.0;
    for (uint16_t j = 0; j < sampleCount; ++j) {
        int16_t v_abs = abs(osciRawData[j]);
        if (v_abs > max_abs)
            max_abs = v_abs;
        sum_sq += static_cast<double>(osciRawData[j]) * osciRawData[j];
    }

    double rms = (sampleCount > 0) ? std::sqrt(sum_sq / sampleCount) : 0.0;
    oscRmsSmoothed_ = 0.08f * oscRmsSmoothed_ + (1.0f - 0.08f) * rms;

    float rms_ratio = (30.0f <= 0.0f) ? 1.0f : (oscRmsSmoothed_ / 30.0f);
    rms_ratio = constrain(rms_ratio, 0.0f, 1.0f);
    float softGainFactor = (rms_ratio < 1.0f) ? (0.12f + powf(rms_ratio, 2.0f) * (1.0f - 0.12f)) : 1.0f;

    const int32_t half_h = graphH / 2 - 1;

    // --- Gain Calculation ---
    float final_gain_lin = cachedGainLinear_; // Cache-elt line�ris gain (powf elimin�lva!)
    if (isAutoGainMode()) {
        uint16_t maxPixelHeight = q15ToPixelHeight(max_abs, (int32_t)(final_gain_lin * 255.0f), graphH / 4);
        if (maxPixelHeight == 0 && max_abs > 0)
            maxPixelHeight = 1;
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
        float pxf = isAutoGainMode() ? (static_cast<float>(osciRawData[i]) / 32767.0f) * half_h * final_gain_lin
                                     : (static_cast<float>(osciRawData[i]) / max_abs) * half_h * final_gain_lin;

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
    uint16_t count = 0;
    for (uint16_t i = startBin; i <= endBin; ++i) {
        double v = static_cast<double>(data[i]);
        sum_sq += v * v;
        ++count;
    }
    if (count == 0) {
        return 0.0f;
    }
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
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0 || wabuf_.empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // === ÚJ ENVELOPE MEGOLDÁS ===
    // Tiszta, egyszerű implementáció automatikus és manuális gain támogatással

    // 1. Frekvenciatartomány meghatározása
    const uint16_t min_bin_for_env = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t max_bin_for_env =
        std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ * 0.5f / currentBinWidthHz)));
    const uint16_t num_bins_in_env_range = std::max(1, max_bin_for_env - min_bin_for_env + 1);

    // 2. Gain számítás (automatikus vagy manuális)
    // Először MINDIG keressük meg a maximum magnitude-ot (debug célra is kell!)
    q15_t maxMagQ15 = 0;
    for (uint16_t i = min_bin_for_env; i <= max_bin_for_env; i++) {
        q15_t absVal = q15Abs(magnitudeData[i]);
        if (absVal > maxMagQ15) {
            maxMagQ15 = absVal;
        }
    }

    float envelopeGain;

    if (isAutoGainMode()) {
        // Auto-gain számítás: sokkal agresszívebb erősítés
        // A magnitude értékek ~200-300 körüliek, ezért 50-100x gain kell
        const float targetMaxValue = 200.0f; // 0..255 tartományból
        if (maxMagQ15 > 10) {                // Ha van érdemi jel
            envelopeGain = targetMaxValue / static_cast<float>(maxMagQ15);
            envelopeGain = std::max(envelopeGain, 50.0f); // Minimum 50x gain
        } else {
            envelopeGain = 100.0f; // Alapértelmezett 100x gain csöndes jelhez
        }

        // Simítás: lassú gain változás
        static float smoothedGain = 100.0f;
        smoothedGain = 0.9f * smoothedGain + 0.1f * envelopeGain;
        envelopeGain = smoothedGain;

    } else {
        // Manuális gain: config alapján, de minimum 50x
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;

        if (gainCfg == 0) {
            envelopeGain = 300.0f; // Alapértelmezett 300x erősítés
        } else {
            // dB -> lineáris konverzió, majd 150x szorzó
            envelopeGain = powf(10.0f, static_cast<float>(gainCfg) / 20.0f) * 150.0f;
        }
    }

    // Gain korlátozása
    envelopeGain = constrain(envelopeGain, 50.0f, 500.0f);

    // Envelope baseline erősítés alkalmazása (dB formátumban)
    float envelopeBaselineMultiplier = powf(10.0f, ENVELOPE_BASELINE_GAIN_DB / 20.0f);
    envelopeGain *= envelopeBaselineMultiplier;

    // 3. Buffer scroll (minden oszlop 1-gyel balra)
    for (uint8_t r = 0; r < bounds.height; ++r) {
        for (uint8_t c = 0; c < bounds.width - 1; ++c) {
            wabuf_[r * bounds.width + c] = wabuf_[r * bounds.width + c + 1];
        }
    }

    // 4. Magnitude adatok írása az utolsó oszlopba
    for (uint8_t r = 0; r < bounds.height; ++r) {
        // Bin index számítás
        uint16_t fft_bin_index =
            min_bin_for_env + static_cast<int>(std::round(static_cast<float>(r) * (num_bins_in_env_range - 1) / std::max(1, (bounds.height - 1))));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_env, max_bin_for_env);

        // Q15 magnitude -> uint8 konverzió biztonságosan
        q15_t magQ15 = magnitudeData[fft_bin_index];
        uint8_t magU8 = q15ToUint8Safe(magQ15, envelopeGain);

        wabuf_[r * bounds.width + (bounds.width - 1)] = magU8;
    }

    // 5. Sprite scroll (vizuális görgetés a képen)
    sprite_->scroll(-1, 0);

    // 6. Envelope görbe rajzolása (buffer átlagolás minden oszlopra)
    uint8_t yCenter = graphH / 2;
    sprite_->drawFastHLine(0, yCenter, bounds.width, TFT_DARKGREY); // Középvonal

    for (uint8_t c = 0; c < bounds.width; ++c) {
        // Oszlop átlag számítás (az összes frekvencia bin átlaga)
        int32_t sum = 0;
        for (uint8_t r = 0; r < bounds.height; ++r) {
            sum += wabuf_[r * bounds.width + c];
        }
        float avgMagnitude = static_cast<float>(sum) / bounds.height;

        // Simítás az előző oszloppal (térbeli folytonosság)
        static float prevAvg = 0.0f;
        avgMagnitude = 0.8f * avgMagnitude + 0.2f * prevAvg;
        prevAvg = avgMagnitude;

        // Pixel magasság számítás (0..255 -> 0..graphH) - AGRESSZÍV skálázás
        float normalizedValue = avgMagnitude / 255.0f;                                 // 0..1
        uint16_t pixelHeight = static_cast<uint16_t>(normalizedValue * graphH * 4.0f); // 400% kihasználtság!
        pixelHeight = std::min(pixelHeight, static_cast<uint16_t>(graphH - 2));

        // Görbe rajzolás (középpontból szimmetrikusan felfelé/lefelé)
        if (pixelHeight > 0) {
            uint16_t yUpper = yCenter - (pixelHeight / 2);
            uint16_t yLower = yCenter + (pixelHeight / 2);

            // Korlátozás a graph területére
            yUpper = constrain(yUpper, 2, graphH - 2);
            yLower = constrain(yLower, 2, graphH - 2);

            // Függőleges vonal rajzolása
            if (yUpper <= yLower) {
                sprite_->drawFastVLine(c, yUpper, yLower - yUpper + 1, TFT_CYAN);
            }
        }
    }

    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief Spectrum Bar + Waterfall kombinált renderelés
 * A felső részben vízszintes high-res bar, alatta pedig lefelé haladó waterfall
 * A két megjelenítés frekvenciában illeszkedik egymáshoz
 */
void UICompSpectrumVis::renderSpectrumBarWithWaterfall() {

    // Peak hold engedélyezése
    // #define BARWATERFALL_ENABLE_PEAK_HOLD

    // Helper lambda függvény a bar területének kirajzolásához (DRY principle)
    auto drawBarArea = [this](uint16_t barHeight, const std::vector<uint16_t> &hiResPeaks) {
        sprite_->fillRect(0, 0, bounds.width, barHeight, TFT_BLACK);
        for (uint16_t x = 0; x < bounds.width; x++) {
            uint16_t height = static_cast<uint16_t>(highresSmoothedCols[x] + 0.5f);
            height = constrain(height, 0, barHeight);

            // Bar oszlop (zöld)
            if (height > 0) {
                uint16_t yStart = barHeight - height;
                sprite_->drawFastVLine(x, yStart, height, TFT_GREEN);
            }

#ifdef BARWATERFALL_ENABLE_PEAK_HOLD
            // Peak pixel (világosabb zöld) - csak ha engedélyezve
            if (hiResPeaks[x] > 1) {
                uint16_t yPeak = barHeight - hiResPeaks[x];
                sprite_->drawPixel(x, yPeak, TFT_GREENYELLOW);
            }
#else
            (void)hiResPeaks; // Figyelmeztetés elkerülése ha nem használjuk
#endif
        }
    };

    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0 || wabuf_.empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // FFT bin tartomány meghatározása
    const uint16_t minBin = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t maxBin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const uint16_t numBins = std::max(1, maxBin - minBin + 1);

    // Layout: felső 1/4 a bar, alsó 3/4 a waterfall
    const uint16_t barHeight = graphH / 4;      // Bar magassága (1/4 része a teljes magasságnak)
    const uint16_t waterfallStartY = barHeight; // Waterfall kezdési pozíciója (y koordináta)

    // ===== GAIN SZÁMÍTÁS =====
    int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float displayGain = calculateDisplayGain(magnitudeData, minBin, maxBin, isAutoGainMode(), gainCfg);

    // Bar baseline erősítés - jelentősen csökkentve hogy ne ugorjanak max magasságra
    // A displayGain már tartalmaz automatikus erősítést, így csak enyhe korrekcióra van szükség
    const float barGain = displayGain * 0.0008f; // Kb -62dB, sokkal lágyabb mint a HIGHRES_BASELINE_GAIN_DB

    // Waterfall gain számítás - TELJESEN FÜGGETLEN a bar gain-től!
    // Cél: magQ15 értékek (tipikusan 100-600) → 0-255 tartomány
    // Ha átlagos maximum ~600, akkor 255/600 ≈ 0.42 kéne
    // De legyen dinamikus tartomány is, ezért kissebb érték: ~0.3-0.5
    constexpr float waterfallGain = 0.4f; // Egyszerű konstans érték, jó dinamikai tartomány

    // ===== HIGH-RES BAR RAJZOLÁSA (FELSŐ RÉSZ) - TEMPORAL SMOOTHING =====
    // Ugyanazokat a buffereket használjuk mint a highres mode (memória takarékosság)
    if (highresSmoothedCols.size() != bounds.width) {
        highresSmoothedCols.assign(bounds.width, 0.0f);
    }

#ifdef BARWATERFALL_ENABLE_PEAK_HOLD
    // Peak hold bufferek (közös a highres mode-dal) - csak ha engedélyezve
    static std::vector<uint16_t> hiResPeaks;
    static std::vector<uint8_t> hiResPeakHoldCounters;
    if (hiResPeaks.size() != bounds.width) {
        hiResPeaks.assign(bounds.width, 0);
        hiResPeakHoldCounters.assign(bounds.width, 0);
    }
#else
    // Üres peak vector ha le van tiltva (csak hogy a drawBarArea ne hibázzon)
    static std::vector<uint16_t> hiResPeaks;
#endif

    // 1. Target magasságok kiszámítása (nyers FFT adatokból)
    std::vector<uint16_t> targetHeights(bounds.width, 0);
    for (uint16_t x = 0; x < bounds.width; x++) {
        float binFloat = minBin + (static_cast<float>(x) * (numBins - 1)) / std::max(1, (bounds.width - 1));
        uint16_t binIdx = static_cast<uint16_t>(std::round(binFloat));
        binIdx = constrain(binIdx, minBin, maxBin);

        targetHeights[x] = q15ToPixelHeightLogarithmic(magnitudeData[binIdx], barGain, barHeight);
    }

    // 2. Temporal smoothing (IIR szűrő) - csökkenti a villogást
    const float SMOOTH_ALPHA = 0.7f; // 0=gyors, 1=lassú (0.7 = 70% régi, 30% új)
    for (uint16_t x = 0; x < bounds.width; x++) {
        highresSmoothedCols[x] = SMOOTH_ALPHA * highresSmoothedCols[x] + (1.0f - SMOOTH_ALPHA) * targetHeights[x];
    }

#ifdef BARWATERFALL_ENABLE_PEAK_HOLD
    // 3. Peak hold logika (közös a highres mode-dal) - csak ha engedélyezve
    const uint8_t PEAK_HOLD_FRAMES = 30; // Peak tartás ideje
    const uint8_t PEAK_FALL_SPEED = 1;   // Peak esési sebesség
    static uint8_t hiResPeakFallTimer = 0;
    bool shouldPeakFall = (++hiResPeakFallTimer % 4 == 0);

    for (uint16_t x = 0; x < bounds.width; x++) {
        uint16_t currentHeight = static_cast<uint16_t>(highresSmoothedCols[x] + 0.5f);

        if (currentHeight >= hiResPeaks[x]) {
            hiResPeaks[x] = currentHeight;
            hiResPeakHoldCounters[x] = PEAK_HOLD_FRAMES;
        } else {
            if (hiResPeakHoldCounters[x] > 0) {
                hiResPeakHoldCounters[x]--;
            } else if (shouldPeakFall && hiResPeaks[x] > 0) {
                hiResPeaks[x] = (hiResPeaks[x] > PEAK_FALL_SPEED) ? (hiResPeaks[x] - PEAK_FALL_SPEED) : 0;
            }
        }
    }
#endif

    // 4. Bar rajzolása simított értékekkel
    drawBarArea(barHeight, hiResPeaks);

    // ===== WATERFALL RAJZOLÁSA (ALSÓ RÉSZ) =====
    // Scroll művelet: az alsó rész (waterfall) 1 pixellel lefelé mozog
    // Ezt a sprite scroll funkciójával valósítjuk meg, csak a waterfall területen
    // FONTOS: Ez sprite-on belüli művelet, nem kell külön buffer

    // Scroll-t teljes sprite-ra alkalmazzuk, de csak lefelé
    sprite_->scroll(0, 1);

    // Az első sort (bar területet) újra kirajzoljuk a simított értékekkel, mert a scroll elmozdította
    drawBarArea(barHeight, hiResPeaks);

    // Új waterfall sor rajzolása a bar alá (waterfallStartY pozícióba)
    uint8_t maxWaterfallVal = 0;
    for (uint16_t x = 0; x < bounds.width; x++) {
        // Frekvencia bin index számítás (ugyanaz mint a bar-nál)
        float binFloat = minBin + (static_cast<float>(x) * (numBins - 1)) / std::max(1, (bounds.width - 1));
        uint16_t binIdx = static_cast<uint16_t>(std::round(binFloat));
        binIdx = constrain(binIdx, minBin, maxBin);

        // Q15 -> uint8 konverzió a waterfall számára
        q15_t magQ15 = magnitudeData[binIdx];
        float magFloat = static_cast<float>(q15Abs(magQ15)) * waterfallGain;
        uint8_t val = static_cast<uint8_t>(constrain(static_cast<int>(magFloat), 0, 255));

        if (val > maxWaterfallVal) {
            maxWaterfallVal = val;
        }

        // Pixel színének kiszámítása és kirajzolása (egyszerűsített: közvetlenül 0-255 érték)
        uint16_t color = valueToWaterfallColor(val, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(x, waterfallStartY, color);
    }

    // ===== AGC FRISSÍTÉS (ha engedélyezve van) =====
    if (isAutoGainMode()) {
        // Magnitude-alapú AGC frissítés
        q15_t maxMagQ15 = 0;
        for (uint16_t i = minBin; i <= maxBin; i++) {
            q15_t absVal = q15Abs(magnitudeData[i]);
            if (absVal > maxMagQ15) {
                maxMagQ15 = absVal;
            }
        }
        float estimatedPeak = (static_cast<float>(maxMagQ15) / 32768.0f) * (graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);
        updateMagnitudeBasedGain(estimatedPeak);
    }

    // ===== SPRITE MEGJELENÍTÉSE =====
    sprite_->pushSprite(bounds.x, bounds.y);

    // Frekvencia feliratok megjelenítése (ha a mode indicator nem látható)
    if (!flags_.modeIndicatorVisible) {
        renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
    }
}

/**
 * @brief Waterfall renderelése
 */
void UICompSpectrumVis::renderWaterfall() {
    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0 || wabuf_.empty()) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // Waterfall scroll - optimaliz optimalizalva korkoros bufferrel (nincs szukseg move-ra)

    const uint16_t min_bin_for_wf = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t max_bin_for_wf = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const uint16_t num_bins_in_wf_range = std::max(1, max_bin_for_wf - min_bin_for_wf + 1);

    // --- Gain Calculation (újraírva az envelope logika alapján) ---
    float waterfallGain;
    if (isAutoGainMode()) {
        // Automatikus gain: maximum magnitude keresése
        q15_t maxMagQ15 = 0;
        for (uint16_t i = min_bin_for_wf; i <= max_bin_for_wf; i++) {
            q15_t absVal = q15Abs(magnitudeData[i]);
            if (absVal > maxMagQ15) {
                maxMagQ15 = absVal;
            }
        }

        const float targetMaxValue = 200.0f;
        if (maxMagQ15 > 10) {
            waterfallGain = targetMaxValue / static_cast<float>(maxMagQ15);
            waterfallGain = std::max(waterfallGain, 50.0f);
        } else {
            waterfallGain = 100.0f;
        }

        static float smoothedGainWF = 100.0f;
        smoothedGainWF = 0.9f * smoothedGainWF + 0.1f * waterfallGain;
        waterfallGain = smoothedGainWF;
    } else {
        int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        if (gainCfg == 0) {
            waterfallGain = 300.0f; // Alapértelmezett 300x
        } else {
            waterfallGain = powf(10.0f, static_cast<float>(gainCfg) / 20.0f) * 150.0f;
        }
    }
    waterfallGain = constrain(waterfallGain, 50.0f, 500.0f);

    // Waterfall baseline erősítés alkalmazása (dB formátumban)
    float waterfallBaselineMultiplier = powf(10.0f, WATERFALL_BASELINE_GAIN_DB / 20.0f);
    waterfallGain *= waterfallBaselineMultiplier;

    // --- End of Gain Calculation ---

    uint8_t maxwabuf_Val = 0;
    for (uint16_t r = 0; r < bounds.height; ++r) {
        uint16_t fft_bin_index =
            min_bin_for_wf + static_cast<int>(std::round(static_cast<float>(r) * (num_bins_in_wf_range - 1) / std::max(1, (bounds.height - 1))));
        fft_bin_index = constrain(fft_bin_index, min_bin_for_wf, max_bin_for_wf);

        // Q15 -> uint8 konverzió (túlcsordulás nélkül)
        q15_t magQ15 = magnitudeData[fft_bin_index];
        float magFloat = static_cast<float>(q15Abs(magQ15)) * waterfallGain;
        uint8_t val = static_cast<uint8_t>(constrain(static_cast<int>(magFloat), 0, 255));

        wabuf_[r * bounds.width + (bounds.width - 1)] = val;
        if (val > maxwabuf_Val)
            maxwabuf_Val = val;
    }

    sprite_->scroll(-1, 0);
    for (uint16_t y = 0; y < graphH; ++y) {
        float r_wabuf_float = (static_cast<float>(graphH - 1 - y) * (bounds.height - 1)) / std::max(1, graphH - 1);
        uint16_t r_lower = floor(r_wabuf_float);
        uint16_t r_upper = ceil(r_wabuf_float);
        r_lower = constrain(r_lower, 0, bounds.height - 1);
        r_upper = constrain(r_upper, 0, bounds.height - 1);

        float frac = r_wabuf_float - r_lower;
        float val_interp = wabuf_[r_lower * bounds.width + (bounds.width - 1)] * (1.0f - frac) + wabuf_[r_upper * bounds.width + (bounds.width - 1)] * frac;

        uint16_t color = valueToWaterfallColor(100 * val_interp, 0.0f, 255.0f * 100, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(bounds.width - 1, y, color);
    }

    if (isAutoGainMode()) {
        float estimatedPeak = (static_cast<float>(maxwabuf_Val) / 255.0f) * (graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);
        updateMagnitudeBasedGain(estimatedPeak);
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    if (!flags_.modeIndicatorVisible) {
        renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
    }
}

/**
 * @brief Hangolási segéd renderelése (CW/RTTY waterfall)
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidWaterfall() {

    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0 || wabuf_.empty()) {
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

    const uint16_t min_bin = std::max(2, static_cast<int>(std::round(currentTuningAidMinFreqHz_ / currentBinWidthHz)));
    const uint16_t max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(currentTuningAidMaxFreqHz_ / currentBinWidthHz)));
    const uint16_t num_bins = std::max(1, max_bin - min_bin + 1);

    // --- ÚJ EGYSÉGES GAIN SZÁMÍTÁS ---
    int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float tuningGain = calculateDisplayGain(magnitudeData, min_bin, max_bin, isAutoGainMode(), gainCfg);
    // --- End of Gain Calculation ---

    uint8_t maxwabuf_Val = 0;

    for (uint16_t c = 0; c < bounds.width; ++c) {
        float ratio = (bounds.width <= 1) ? 0.0f : static_cast<float>(c) / (bounds.width - 1);
        float exact_bin = min_bin + ratio * (num_bins - 1);

        q15_t mag_q15 = static_cast<q15_t>(std::round(q15InterpolateFloat(magnitudeData, exact_bin, min_bin, max_bin)));

        // ÚJ BIZTONSÁGOS KONVERZIÓ
        uint8_t val = q15ToUint8Safe(mag_q15, tuningGain);

        wabuf_[0 * bounds.width + c] = val;
        if (val > maxwabuf_Val)
            maxwabuf_Val = val;

        uint16_t color = valueToWaterfallColor(100 * val, 0.0f, 255.0f * 100, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(c, 0, color);
    }

    // Draw tuning aid lines (CW zöld vonal, RTTY mark=zöld, space=sárga)
    float freq_range = currentTuningAidMaxFreqHz_ - currentTuningAidMinFreqHz_;
    if (freq_range > 0) {
        if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
            uint16_t cw_freq = config.data.cwToneFrequencyHz;
            if (cw_freq >= currentTuningAidMinFreqHz_ && cw_freq <= currentTuningAidMaxFreqHz_) {
                int16_t x_pos = round(((cw_freq - currentTuningAidMinFreqHz_) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_GREEN);
            }
        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            uint16_t mark_freq = config.data.rttyMarkFrequencyHz;
            uint16_t space_freq = mark_freq - config.data.rttyShiftFrequencyHz;

            if (mark_freq >= currentTuningAidMinFreqHz_ && mark_freq <= currentTuningAidMaxFreqHz_) {
                int16_t x_pos = round(((mark_freq - currentTuningAidMinFreqHz_) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_GREEN);
            }
            if (space_freq >= currentTuningAidMinFreqHz_ && space_freq <= currentTuningAidMaxFreqHz_) {
                int16_t x_pos = round(((space_freq - currentTuningAidMinFreqHz_) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_YELLOW);
            }
        }
    }

    // Frekvencia címkék rajzolása (közös helper függvény)
    renderTuningAidFrequencyLabels(currentTuningAidMinFreqHz_, currentTuningAidMaxFreqHz_, graphH);

    // Sprite megjelenítése a képernyőn!
    sprite_->pushSprite(bounds.x, bounds.y);

    renderFrequencyRangeLabels(currentTuningAidMinFreqHz_, currentTuningAidMaxFreqHz_);
}

/**
 * @brief SNR Curve renderelése - frekvencia/SNR burkológörbe
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidSnrCurve() {
    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
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

    // --- ÚJ EGYSÉGES GAIN SZÁMÍTÁS ---
    int8_t gainCfg = this->radioMode_ == RadioMode::AM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float snrGain = calculateDisplayGain(magnitudeData, min_bin, max_bin, isAutoGainMode(), gainCfg);
    // --- End of Gain Calculation ---

    const uint16_t targetHeight = static_cast<uint16_t>(graphH * GRAPH_TARGET_HEIGHT_UTILIZATION);

    // Pixel magasságok számítása minden x pozícióhoz
    std::vector<uint16_t> pixelHeights(bounds.width, 0);
    uint16_t maxPixelHeight = 0;

    for (uint16_t x = 0; x < bounds.width; x++) {
        float ratio = (bounds.width <= 1) ? 0.0f : static_cast<float>(x) / (bounds.width - 1);
        float exact_bin = min_bin + ratio * (num_bins - 1);
        q15_t mag_q15 = static_cast<q15_t>(std::round(q15InterpolateFloat(magnitudeData, exact_bin, min_bin, max_bin)));

        // ÚJ SOFT COMPRESSION PIXEL MAGASSÁG SZÁMÍTÁS (görbített tetejű)
        uint16_t height = q15ToPixelHeightSoftCompression(mag_q15, snrGain, targetHeight);
        pixelHeights[x] = height;

        if (height > maxPixelHeight) {
            maxPixelHeight = height;
        }
    }

    // AGC frissítés
    if (isAutoGainMode()) {
        updateMagnitudeBasedGain(static_cast<float>(maxPixelHeight));
    }

    // Görbe rajzolása (soft compression - természetes csúcsok)
    int16_t prevX = -1, prevY = -1;
    for (uint16_t x = 0; x < bounds.width; x++) {
        uint16_t height = pixelHeights[x]; // Nincs hard clipping!
        // Csak a képernyőn kívüli értékeket korlátozzuk
        if (height >= graphH)
            height = graphH - 1;

        int16_t y = graphH - 1 - height; // y koordináta: graphH-1 az alja, 0 a teteje

        if (prevX != -1) {
            sprite_->drawLine(prevX, prevY, x, y, TFT_CYAN);
        }
        sprite_->drawPixel(x, y, TFT_WHITE);
        prevX = x;
        prevY = y;
    }

    // Draw tuning aid lines
    float freq_range = max_freq - min_freq;
    if (freq_range > 0) {
        if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
            uint16_t cw_freq = config.data.cwToneFrequencyHz;
            if (cw_freq >= min_freq && cw_freq <= max_freq) {
                int16_t x_pos = round(((cw_freq - min_freq) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_GREEN);
            }
        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            uint16_t mark_freq = config.data.rttyMarkFrequencyHz;
            uint16_t space_freq = mark_freq - config.data.rttyShiftFrequencyHz;

            if (mark_freq >= min_freq && mark_freq <= max_freq) {
                int16_t x_pos = round(((mark_freq - min_freq) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_GREEN);
            }
            if (space_freq >= min_freq && space_freq <= max_freq) {
                int16_t x_pos = round(((space_freq - min_freq) / freq_range) * (bounds.width - 1));
                sprite_->drawFastVLine(x_pos, 0, graphH, TFT_YELLOW);
            }
        }
    }

    // Frekvencia címkék a hangolási vonalakon (sprite-ra, vonal aljára, padding minden irányban)
    if (freq_range > 0) {
        sprite_->setTextDatum(TC_DATUM);             // Felül középre igazítva a méretszámításhoz
        sprite_->setTextColor(TFT_WHITE, TFT_BLACK); // Fehér szöveg, fekete háttér
        sprite_->setFreeFont();
        sprite_->setTextSize(1);

        constexpr int8_t PADDING = 3; // 3px padding minden irányban

        if (currentTuningAidType_ == TuningAidType::CW_TUNING) {
            uint16_t cw_freq = config.data.cwToneFrequencyHz;
            if (cw_freq >= min_freq && cw_freq <= max_freq) {
                int16_t x_pos = round(((cw_freq - min_freq) / freq_range) * (bounds.width - 1));
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", cw_freq);

                // Szöveg méret kiszámítása
                int16_t text_w = sprite_->textWidth(buf);
                int16_t text_h = sprite_->fontHeight();
                int16_t box_x = x_pos - text_w / 2 - PADDING;
                int16_t box_y = graphH - text_h - PADDING * 2;
                int16_t box_w = text_w + PADDING * 2;
                int16_t box_h = text_h + PADDING * 2;

                sprite_->fillRect(box_x, box_y, box_w, box_h, TFT_BLACK); // Fekete háttér
                sprite_->drawString(buf, x_pos, box_y + PADDING);         // Szöveg a téglalapba
            }
        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            uint16_t mark_freq = config.data.rttyMarkFrequencyHz;
            uint16_t space_freq = mark_freq - config.data.rttyShiftFrequencyHz;

            if (mark_freq >= min_freq && mark_freq <= max_freq) {
                int16_t x_pos = round(((mark_freq - min_freq) / freq_range) * (bounds.width - 1));
                char buf[16];
                snprintf(buf, sizeof(buf), "M:%u", mark_freq);

                int16_t text_w = sprite_->textWidth(buf);
                int16_t text_h = sprite_->fontHeight();
                int16_t box_x = x_pos - text_w / 2 - PADDING;
                int16_t box_y = graphH - text_h - PADDING * 2;
                int16_t box_w = text_w + PADDING * 2;
                int16_t box_h = text_h + PADDING * 2;

                sprite_->fillRect(box_x, box_y, box_w, box_h, TFT_BLACK);
                sprite_->drawString(buf, x_pos, box_y + PADDING);
            }

            if (space_freq >= min_freq && space_freq <= max_freq) {
                int16_t x_pos = round(((space_freq - min_freq) / freq_range) * (bounds.width - 1));
                char buf[16];
                snprintf(buf, sizeof(buf), "S:%u", space_freq);

                int16_t text_w = sprite_->textWidth(buf);
                int16_t text_h = sprite_->fontHeight();
                int16_t box_x = x_pos - text_w / 2 - PADDING;
                int16_t box_y = graphH - text_h - PADDING * 2;
                int16_t box_w = text_w + PADDING * 2;
                int16_t box_h = text_h + PADDING * 2;

                sprite_->fillRect(box_x, box_y, box_w, box_h, TFT_BLACK);
                sprite_->drawString(buf, x_pos, box_y + PADDING);
            }
        }
    }

    // AGC frissítés (ha auto gain mód)
    if (isAutoGainMode()) {
        updateMagnitudeBasedGain(static_cast<float>(maxPixelHeight));
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    renderFrequencyRangeLabels(static_cast<uint16_t>(min_freq), static_cast<uint16_t>(max_freq));
}
