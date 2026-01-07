/*
 * Projekt: [pico-radio-9] Raspberry Pi Pico Si4735 rádió                                                              *
 * Fájl: UICompSpectrumVis.cpp                                                                                         *
 * Készítés dátuma: 2025.11.15.                                                                                        *
 *                                                                                                                     *
 * Szerző: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * Licenc: MIT                                                                                                         *
 * 	A fájl szabadon használható, módosítható és terjeszthető; beépíthető más projektekbe (akár zártkódúba is).
 * 	Feltétel: a szerző és a licenc feltüntetése a forráskódban.
 * -----
 * Utolsó módosítás: 2025.11.30. (Sunday) 07:48:15                                                                     *
 * Módosította: BT-Soft                                                                                                *
 * -----
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

// ===== Vizualizáció általános konstansok =====
/**
 * @brief Grafikon célkitöltés (AGC cél)
 * 0.85 = 85% - ennyi helyet céloz meg az AGC a grafikon magasságából
 */
static constexpr float GRAPH_TARGET_HEIGHT_UTILIZATION = 0.85f;

// ===== Baseline erősítés konstansok spektrum megjelenítéshez (dB) =====
/**
 * @brief Alap vizualizációs erősítések dB-ben (AGC korrekció előtt)
 * Újrakalibrálva a dBFS-alapú számításhoz (20*log10(mag/32767))
 * 0dB = nincs változtatás (semleges)
 */
constexpr float LOWRES_BASELINE_GAIN_DB = 0.0f;       // Low-res spektrum
constexpr float HIGHRES_BASELINE_GAIN_DB = 0.0f;      // High-res spektrum
constexpr float ENVELOPE_BASELINE_GAIN_DB = 0.0f;     // Burkológörbe
constexpr float WATERFALL_BASELINE_GAIN_DB = 0.0f;    // Vízesés
constexpr float OSCILLOSCOPE_BASELINE_GAIN_DB = 0.0f; // Oszcilloszkóp

/**
 * @brief CW/RTTY hangolási segéd baseline erősítések (dB)
 * Negatív érték = csillapítás a túl erős jelekhez
 */
constexpr float CW_WATERFALL_BASELINE_GAIN_DB = -20.0f;   // CW vízesés
constexpr float CW_SNRCURVE_BASELINE_GAIN_DB = -24.0f;    // CW SNR görbe
constexpr float RTTY_WATERFALL_BASELINE_GAIN_DB = -24.0f; // RTTY vízesés
constexpr float RTTY_SNRCURVE_BASELINE_GAIN_DB = -24.0f;  // RTTY SNR görbe

/**
 * @brief Sávszélesség-specifikus gain konfiguráció struktúra
 *
 * Egy táblázat a különböző dekóder sávszélességekhez rendelt erősítési értékekkel.
 * Keskenyebb sávszélesség → kevesebb FFT bin → kisebb összenergia → nagyobb erősítés szükséges
 * Szélesebb sávszélesség → több FFT bin → nagyobb összenergia → kisebb erősítés elegendő
 */
struct BandwidthScaleConfig {
    uint32_t bandwidthHz;       ///< Dekóder sávszélesség (Hz)
    float lowResBarGainDb;      ///< Erősítés low-res spektrum módhoz (dB)
    float highResBarGainDb;     ///< Erősítés high-res spektrum módhoz (dB)
    float oscilloscopeGainDb;   ///< Erősítés oszcilloszkóphoz (dB)
    float envelopeGainDb;       ///< Erősítés burkológörbéhez (dB)
    float waterfallGainDb;      ///< Erősítés vízeséshez (dB)
    float tuningAidWaterfallDb; ///< Erősítés hangolási segéd vízeséshez (dB)
    float tuningAidSnrCurveDb;  ///< Erősítés hangolási segéd SNR görbéhez (dB)
};

/**
 * @brief Nincs erősítés konstans (mód nem elérhető adott sávszélességnél)
 */
#define NOAMP 0.0f

/**
 * @brief Előre definiált gain táblázat sávszélesség szerint (növekvő sorrendben)
 *
 * MEGJEGYZÉS: A NOAMP jelzéssel ellátott értékek azt jelzik, hogy az adott mód
 * nem elérhető az adott sávszélességen (pl. CW módnál nincs normál spektrum).
 */
constexpr BandwidthScaleConfig BANDWIDTH_GAIN_TABLE[] = {
    // bandwidthHz,          lowRes, highRes, osc,    env,   water,  tuningW, tuningSNR
    {CW_AF_BANDWIDTH_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, -8.0f, -8.0f},   // 1.5kHz CW
    {RTTY_AF_BANDWIDTH_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, -8.0f, -8.0f}, // 3kHz RTTY
    {AM_AF_BANDWIDTH_HZ, -10.0f, -10.0f, -10.0f, 5.0f, 10.0f, NOAMP, NOAMP}, // 6kHz AM
    {WEFAX_SAMPLE_RATE_HZ, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP, NOAMP}, // 11025Hz WEFAX
    {FM_AF_BANDWIDTH_HZ, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, NOAMP, NOAMP},        // 15kHz FM
};
constexpr size_t BANDWIDTH_GAIN_TABLE_SIZE = ARRAY_ITEM_COUNT(BANDWIDTH_GAIN_TABLE);

// ===== AGC (Automatic Gain Control) konstansok =====
/**
 * @brief AGC minimum és maximum erősítés értékek
 * Ezek korlátozzák az automatikus erősítés szabályozást
 */
constexpr float AGC_MIN_GAIN = 0.0001f; ///< Minimális gain érték (végtelen csillapítás ellen)
constexpr float AGC_MAX_GAIN = 80.0f;   ///< Maximális gain érték (túlerősítés ellen)

// ===== Színprofilok =====
namespace FftDisplayConstants {

/**
 * @brief Waterfall színpaletta RGB565 formátumban (alapértelmezett: sötétkék-zöld-sárga-piros)
 */
const uint16_t waterFallColors_0[16] = {0x000C, // sötétkék háttér (gyenge jel)
                                        0x001F, // közepes kék
                                        0x021F, // világoskék
                                        0x07FF, // cián
                                        0x07E0, // zöld
                                        0x5FE0, // világoszöld
                                        0xFFE0, // sárga
                                        0xFD20, // narancs
                                        0xF800, // piros
                                        0xF81F, // pink
                                        0xFFFF, // fehér (erős jel)
                                        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

/**
 * @brief Waterfall színpaletta RGB565 formátumban (alternatív: piros árnyalatok)
 */
const uint16_t waterFallColors_1[16] = {0x0000, 0x1000, 0x2000, 0x4000, 0x8000, 0xC000, 0xF800, 0xF8A0,
                                        0xF9C0, 0xFD20, 0xFFE0, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

/** @brief Használt színpaletta index (0 vagy 1) */
#define WATERFALL_COLOR_INDEX 0

/** @brief Mód indikátor láthatósági időtúllépés (ms) */
constexpr uint16_t MODE_INDICATOR_VISIBLE_TIMEOUT_MS = 10 * 1000;

/** @brief Spektrum FPS korlátozás (frame per second) */
constexpr uint8_t SPECTRUM_FPS = 25;

}; // namespace FftDisplayConstants

// ===== dBFS (Decibels relative to Full Scale) számítási konstansok =====
/**
 * @brief Q15 teljes skála referencia érték
 * Q15 formátumban (signed 16-bit) a maximális érték 32767
 */
constexpr float Q15_FULL_SCALE = 32767.0f;

/**
 * @brief Csend küszöb dBFS-ben
 * Ennél alacsonyabb értékeket csendnek tekintünk (log10 végtelen értékének elkerülése)
 */
constexpr float DB_SILENCE_THRESHOLD = -100.0f;

/**
 * @brief Vizualizációs dinamika tartományok különböző módokhoz
 */
namespace DbRanges {
// Spektrum és oszcilloszkóp vizualizációhoz
constexpr float SPECTRUM_DB_MIN = -72.0f; ///< Leggyengébb látható jel
constexpr float SPECTRUM_DB_MAX = 6.0f;   ///< Legerősebb jel (kis tartalékkal 0 felett)

// Vízesés és burkológörbe vizualizációhoz (szélesebb tartomány)
constexpr float WATERFALL_DB_MIN = -80.0f; ///< Érzékenyebb a gyenge jelekhez
constexpr float WATERFALL_DB_MAX = 0.0f;   ///< Teljes skála felső határa

// SNR hangolási segéd (tömörített tartomány a tiszta csúcsokhoz)
constexpr float SNR_CURVE_DB_MIN = -60.0f; ///< SNR görbe alsó határa
constexpr float SNR_CURVE_DB_MAX = 10.0f;  ///< SNR görbe felső határa

// SNR görbe simítási exponens (hatványfüggvény a vizuális kellemességhez)
constexpr float SNR_CURVE_SMOOTHING_EXPONENT = 0.6f;
} // namespace DbRanges

// ===== dBFS segédfüggvények =====

/**
 * @brief Q15 magnitúdó abszolút értéke (gyors inline)
 */
static inline int32_t q15Abs(q15_t v) { return (v < 0) ? -(int32_t)v : (int32_t)v; }

/**
 * @brief Q15 érték konvertálása dBFS-re (Decibels relative to Full Scale)
 *
 * @param magQ15 Q15 formátumú magnitúdó érték
 * @return dBFS érték, vagy DB_SILENCE_THRESHOLD ha túl kicsi a jel
 *
 * @note Formula: dBFS = 20 * log10(magnitude / Q15_FULL_SCALE)
 *       Példa: 327 magnitúdó → 20*log10(327/32767) ≈ -40 dBFS
 */
static inline float q15ToDbFs(q15_t magQ15) {
    float magnitude = static_cast<float>(q15Abs(magQ15));
    if (magnitude < 1.0f) {
        return DB_SILENCE_THRESHOLD;
    }
    return 20.0f * log10f(magnitude / Q15_FULL_SCALE);
}

/**
 * @brief dBFS normalizálás megadott tartományra (0.0 - 1.0)
 *
 * @param dbfs dBFS érték
 * @param totalGainDb Összes alkalmazott erősítés (dB)
 * @param dbMin Tartomány alsó határa (dB)
 * @param dbMax Tartomány felső határa (dB)
 * @return Normalizált érték 0.0 és 1.0 között
 *
 * @note A tartományon kívüli értékek 0.0-ra vagy 1.0-ra korlátozódnak
 */
static inline float normalizeDbRange(float dbfs, float totalGainDb, float dbMin, float dbMax) {
    float dbTotal = dbfs + totalGainDb;
    float normalized = (dbTotal - dbMin) / (dbMax - dbMin);
    return constrain(normalized, 0.0f, 1.0f);
}

/**
 * @brief Q15 magnitúdó → normalizált érték (0.0-1.0) logaritmikus dBFS skálán
 *
 * Általános célú konverziós függvény különböző vizualizációkhoz.
 *
 * @param magQ15 Bemeneti Q15 magnitúdó
 * @param totalGainDb Teljes erősítés (AGC, baseline, stb.) decibelben
 * @param dbRangeMin Vizuális tartomány minimális dB értéke (→ 0.0f)
 * @param dbRangeMax Vizuális tartomány maximális dB értéke (→ 1.0f)
 * @return Normalizált lebegőpontos érték 0.0f és 1.0f között
 */
static inline float q15ToDbFsNormalized(q15_t magQ15, float totalGainDb, float dbRangeMin, float dbRangeMax) {
    float dbfs = q15ToDbFs(magQ15);

    if (dbfs <= DB_SILENCE_THRESHOLD) {
        return 0.0f;
    }

    return normalizeDbRange(dbfs, totalGainDb, dbRangeMin, dbRangeMax);
}

/**
 * @brief Q15 magnitúdó → uint8_t (0-255) logaritmikus dBFS skálán
 *
 * Ideális vízesés és burkológörbe megjelenítéshez (szélesebb dinamika).
 *
 * @param magQ15 Q15 magnitúdó
 * @param totalGainDb Teljes erősítés (dB)
 * @return uint8_t érték 0 és 255 között
 */
static inline uint8_t q15ToUint8Log(q15_t magQ15, float totalGainDb) {
    float normalized = q15ToDbFsNormalized(magQ15, totalGainDb, DbRanges::WATERFALL_DB_MIN, DbRanges::WATERFALL_DB_MAX);
    return static_cast<uint8_t>(normalized * 255.0f);
}

/**
 * @brief Q15 magnitúdó → pixelmagasság 'soft-compression' görbével
 *
 * Ideális SNR hangolási segédekhez. A normalizált eredményre hatványfüggvényt
 * alkalmazunk a csúcsok vizuálisan kellemesebb megjelenítéséhez.
 *
 * @param magQ15 Q15 magnitúdó
 * @param totalGainDb Teljes erősítés (dB)
 * @param maxHeight Maximális pixelmagasság
 * @return Pixelmagasság (0 - maxHeight)
 */
static inline uint16_t q15ToPixelHeightSnrCurve(q15_t magQ15, float totalGainDb, uint16_t maxHeight) {
    float normalized = q15ToDbFsNormalized(magQ15, totalGainDb, DbRanges::SNR_CURVE_DB_MIN, DbRanges::SNR_CURVE_DB_MAX);

    if (normalized < 0.001f) {
        return 0;
    }

    // Soft-compression: hatványfüggvény a kerekebb, kellemesebb csúcsokért
    float curved = powf(normalized, DbRanges::SNR_CURVE_SMOOTHING_EXPONENT);

    uint16_t height = static_cast<uint16_t>(curved * maxHeight);
    return std::min(height, maxHeight);
}

/**
 * @brief Q15 magnitude → pixel magasság konverzió LOGARITMIKUS (dBFS) skálával
 *
 * Ez a központi függvény a spektrum-alapú megjelenítésekhez (bar chart, stb.).
 * A számítás alapja a dBFS (Decibels relative to Full Scale), ahol 0 dBFS
 * a maximális lehetséges jelszintet jelöli (Q15 típusnál ez 32767).
 *
 * Előnyök a dBFS használatának:
 * - Egyértelmű fizikai referencia (nincs "mágikus szám")
 * - Emberi hallás logaritmikus érzékelésének megfelelő
 * - Könnyű kalibrálhatóság és hibakeresés
 *
 * @param magQ15 Bemeneti Q15 magnitúdó érték
 * @param gainDb Teljes összegzett erősítés (AGC + baseline + sávszélesség) DECIBELBEN
 * @param maxHeight Grafikon maximális magassága pixelben
 * @return Kiszámított pixelmagasság (0 - maxHeight)
 *
 * @example
 *   magQ15 = 327 → dBFS ≈ -40 dB
 *   Ha gainDb = +40 dB → dbTotal = 0 dB → kb. középső magasság
 */
static inline uint16_t q15ToPixelHeightLogarithmic(q15_t magQ15, float gainDb, uint16_t maxHeight) {
    float dbfs = q15ToDbFs(magQ15);

    if (dbfs <= DB_SILENCE_THRESHOLD) {
        return 0;
    }

    // Erősítés alkalmazása dB tartományban (egyszerű összeadás)
    float dbTotal = dbfs + gainDb;

    // Normalizálás és konverzió pixel magasságra
    float normalized = normalizeDbRange(dbTotal, 0.0f, DbRanges::SPECTRUM_DB_MIN, DbRanges::SPECTRUM_DB_MAX);

    uint16_t height = static_cast<uint16_t>(normalized * maxHeight);
    return std::min(height, maxHeight);
}

// ===== BACKWARD COMPATIBILITY függvények (DEPRECATED) =====
/**
 * @deprecated Régi lineáris konverziós függvények
 * @warning Ezek túlcsordulásra hajlamosak nagy gain értékeknél!
 * @note Új kódban NE használd - helyette a dBFS alapú függvények ajánlottak
 */

/**
 * @brief Q15 → uint8_t konverzió (RÉGI, lineáris módszer)
 * @deprecated Használd helyette: q15ToUint8Log()
 */
static inline uint8_t q15ToUint8(q15_t v, int32_t gain_scaled) {
    int32_t abs_val = q15Abs(v);
    if (abs_val == 0)
        return 0;

    int32_t result = (abs_val * gain_scaled) >> 15;
    return static_cast<uint8_t>(constrain(result, 0, 255));
}

/**
 * @brief Q15 → pixel magasság konverzió (RÉGI, lineáris módszer)
 * @deprecated Használd helyette: q15ToPixelHeightLogarithmic()
 */
static inline uint16_t q15ToPixelHeight(q15_t v, int32_t gain_scaled, uint16_t max_height) {
    int32_t abs_val = q15Abs(v);
    if (abs_val == 0)
        return 0;

    int32_t temp = (abs_val * gain_scaled) >> 15; // 0..255 tartomány
    int32_t result = (temp * max_height) >> 8;    // skálázás max_height-ra
    return static_cast<uint16_t>(constrain(result, 0, static_cast<int32_t>(max_height)));
}

// ===== Interpolációs segédfüggvények =====

/**
 * @brief Fixpontos lineáris interpoláció Q15 tömbből (Q15 eredménnyel)
 *
 * Optimalizált verzió: 16 bites frakcionális részű fixpontos számítás.
 *
 * @param data Q15 adattömb
 * @param exactIndex Pontos indexelési pozíció (lehet tört érték)
 * @param minIdx Minimum index korlát
 * @param maxIdx Maximum index korlát
 * @return Interpolált Q15 érték
 */
static inline q15_t q15Interpolate(const q15_t *data, float exactIndex, int minIdx, int maxIdx) {
    int idx_low = static_cast<int>(exactIndex);
    int idx_high = idx_low + 1;

    idx_low = constrain(idx_low, minIdx, maxIdx);
    idx_high = constrain(idx_high, minIdx, maxIdx);

    if (idx_low == idx_high) {
        return data[idx_low];
    }

    // Fixpontos lineáris interpoláció (16 bit frakció)
    uint16_t frac16 = static_cast<uint16_t>((exactIndex - idx_low) * 65536.0f);
    int32_t low = data[idx_low];
    int32_t high = data[idx_high];

    return static_cast<q15_t>((low * (65536 - frac16) + high * frac16) >> 16);
}

/**
 * @brief Interpoláció float visszatérési értékkel (backward compatibility)
 * @deprecated Használd helyette: q15Interpolate()
 */
static inline float q15InterpolateFloat(const q15_t *data, float exactIndex, int minIdx, int maxIdx) {
    return static_cast<float>(q15Interpolate(data, exactIndex, minIdx, maxIdx));
}

// ===== Gain számítási segédfüggvények =====

/**
 * @brief RMS alapú gain számítás konstansok
 */
namespace GainCalculation {
constexpr float TARGET_RMS_VALUE = 120.0f;   ///< Célzott RMS érték auto-gain módban
constexpr float MIN_RMS_THRESHOLD = 8.0f;    ///< Minimum RMS küszöb
constexpr float MIN_GAIN_LINEAR = 12.0f;     ///< Minimum lineáris gain érték
constexpr float DEFAULT_GAIN_LINEAR = 30.0f; ///< Alapértelmezett gain érték
constexpr float SMOOTHING_FACTOR = 0.98f;    ///< Simítási faktor (98% régi + 2% új)
constexpr float MIN_GAIN_LIMIT = 15.0f;      ///< Lineáris gain alsó korlátja
constexpr float MAX_GAIN_LIMIT = 200.0f;     ///< Lineáris gain felső korlátja
} // namespace GainCalculation

/**
 * @brief Közös gain számítás minden vizualizációs módhoz
 *
 * RMS (Root Mean Square) alapú automatikus erősítés számítás hosszú távú átlagolással.
 * Az RMS méréséből számolt gain stabilabb, mint a maximum alapú számítás, mert
 * egy-egy csúcs nem tudja teljesen elnyomni a többi jelet.
 *
 * @param magnitudeData FFT magnitude adatok (Q15 tömb)
 * @param minBin Minimum bin index (tartomány kezdete)
 * @param maxBin Maximum bin index (tartomány vége)
 * @param isAutoGain Automatikus gain mód aktív-e
 * @param manualGainDb Manuális gain érték dB-ben (ha nem auto)
 * @return Gain érték DECIBELBEN
 *
 * @note Az auto-gain ~50 frame időállandóval simít (98% régi + 2% új)
 */
static inline float calculateDisplayGainDb(const q15_t *magnitudeData, uint16_t minBin, uint16_t maxBin, bool isAutoGain, int8_t manualGainDb) {
    if (!isAutoGain) {
        // Manuális gain: beállított érték közvetlen használata
        return static_cast<float>(manualGainDb);
    }

    // Auto-gain: RMS alapú számítás
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

    // Gain számítás RMS alapján
    float gainLinear;
    if (rms > GainCalculation::MIN_RMS_THRESHOLD) {
        gainLinear = GainCalculation::TARGET_RMS_VALUE / rms;
        gainLinear = std::max(gainLinear, GainCalculation::MIN_GAIN_LINEAR);
    } else {
        gainLinear = GainCalculation::DEFAULT_GAIN_LINEAR;
    }

    // Hosszú távú exponenciális simítás (~50 frame időállandó)
    static float smoothedGainAuto = GainCalculation::DEFAULT_GAIN_LINEAR;
    smoothedGainAuto = GainCalculation::SMOOTHING_FACTOR * smoothedGainAuto + (1.0f - GainCalculation::SMOOTHING_FACTOR) * gainLinear;

    // Korlátok alkalmazása és dB konverzió
    gainLinear = constrain(smoothedGainAuto, GainCalculation::MIN_GAIN_LIMIT, GainCalculation::MAX_GAIN_LIMIT);

    return 20.0f * log10f(gainLinear);
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
            barAgcGainFactor_ = constrain(newGain, AGC_MIN_GAIN, AGC_MAX_GAIN);
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

/**
 * @brief Általános AGC gain számítás (privát, közös mag)
 *
 * Mindkét AGC típushoz (bar-alapú és magnitude-alapú) használt általános logika.
 * History buffer alapján számítja ki az új gain értéket.
 *
 * @param history History buffer (bar magasságok vagy magnitude értékek)
 * @param historySize History buffer mérete
 * @param currentGainFactor Aktuális gain faktor
 * @param targetValue Célérték (pixel magasság vagy magnitude)
 * @return Új gain faktor (korlátozva AGC_MIN_GAIN és AGC_MAX_GAIN között)
 */
float UICompSpectrumVis::calculateAgcGainGeneric(const float *history, uint8_t historySize, float currentGainFactor, float targetValue) const {
    // History átlag számítása (csak érvényes értékekből)
    float sum = 0.0f;
    uint8_t validCount = 0;

    for (uint8_t i = 0; i < historySize; ++i) {
        if (history[i] > AGC_MIN_SIGNAL_THRESHOLD) {
            sum += history[i];
            validCount++;
        }
    }

    if (validCount == 0) {
        return currentGainFactor; // Nincs elég jel → megtartjuk a jelenlegi gain-t
    }

    // Átlag alapú ideális gain számítás
    float averageMax = sum / validCount;
    float idealGain = targetValue / averageMax;

    // Simított átmenet (exponenciális simítás)
    float newGainSuggested = AGC_SMOOTH_FACTOR * idealGain + (1.0f - AGC_SMOOTH_FACTOR) * currentGainFactor;

    // Biztonsági korlátok alkalmazása
    float newGainLimited = constrain(newGainSuggested, AGC_MIN_GAIN, AGC_MAX_GAIN);

#ifdef __DEBUG
    if (this->isAutoGainMode()) {
        static long lastAgcGenericLogTime = 0;
        if (Utils::timeHasPassed(lastAgcGenericLogTime, 2000)) {
            UISPECTRUM_DEBUG("[AGC] átlag=%.2f ideális=%.2f javasolt=%.2f (min=%.4f max=%.2f) → limitált=%.2f\n", averageMax, idealGain, newGainSuggested,
                             AGC_MIN_GAIN, AGC_MAX_GAIN, newGainLimited);
            lastAgcGenericLogTime = millis();
        }
    }
#endif

    return newGainLimited;
}

/**
 * @brief Bar-alapú AGC scale lekérése
 *
 * @param baseConstant Alap érzékenységi konstans
 * @return Skálázási faktor (baseConstant * AGC gain vagy manual gain)
 */
float UICompSpectrumVis::getBarAgcScale(float baseConstant) {
    if (isAutoGainMode()) {
        return baseConstant * barAgcGainFactor_;
    }

    // Manual gain: dB → lineáris konverzió
    int8_t gainDb = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;

    float gainLinear = powf(10.0f, gainDb / 20.0f);
    return baseConstant * gainLinear;
}

/**
 * @brief Magnitude-alapú AGC scale lekérése
 *
 * @param baseConstant Alap érzékenységi konstans
 * @return Skálázási faktor (baseConstant * AGC gain vagy manual gain)
 */
float UICompSpectrumVis::getMagnitudeAgcScale(float baseConstant) {
    if (isAutoGainMode()) {
        return baseConstant * magnitudeAgcGainFactor_;
    }

    // Manual gain: dB → lineáris konverzió
    int8_t gainDb = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;

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
        return true; // További renderelés leállítása
    }

    if (flags_.isMutedDrawn) {
        flags_.isMutedDrawn = false;
        flags_.needBorderDrawn = true; // Keret újrarajzolása némítás feloldása után
    }
#endif
    return false; // Renderelés folytatása
}

/**
 * @brief Handles the visibility and timing of the mode indicator display.
 */
void UICompSpectrumVis::handleModeIndicator() {
    // Indikátor megjelenítése, ha láthatónak kell lennie és még nem lett kirajzolva
    if (flags_.modeIndicatorVisible && !flags_.modeIndicatorDrawn) {
        renderModeIndicator();
        flags_.modeIndicatorDrawn = true;
    }

    // Indikátor elrejtése időtúllépés után
    if (flags_.modeIndicatorVisible && millis() > modeIndicatorHideTime_) {
        flags_.modeIndicatorVisible = false;
        flags_.modeIndicatorDrawn = false;

        // Annak a területnek a törlése, ahol az indikátor volt
        int indicatorY = bounds.y + bounds.height;
        tft.fillRect(bounds.x - 3, indicatorY, bounds.width + 6, 12, TFT_BLACK);

        // Frekvencia feliratok újrarajzolásának engedélyezése
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
        return; // Renderelés leállítása némítás esetén
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

    // Teljes terület törlése módváltáskor az előző grafikon eltávolításához
    if (modeToPrepareForDisplayMode != lastRenderedMode_) {

        // Csak a belső területet töröljük, de az alsó bordert meghagyjuk
        tft.fillRect(bounds.x, bounds.y, bounds.width, bounds.height - 1, TFT_BLACK);

        // Frekvencia feliratok területének törlése - CSAK a component szélességében
        tft.fillRect(bounds.x, bounds.y + bounds.height + 1, bounds.width, 10, TFT_BLACK);

        // Sprite is törlése ha létezett
        if (flags_.spriteCreated) {
            sprite_->fillSprite(TFT_BLACK);
        }

        // Envelope reset módváltáskor
        if (modeToPrepareForDisplayMode == DisplayMode::Envelope) {

            // wabuf_ buffer teljes törlése hogy tiszta vonallal kezdődjön az envelope
            std::fill(wabuf_.begin(), wabuf_.end(), 0); // 1D buffer trls
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

    // AGC reset módváltáskor
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

            // Egyszerusített fixpont: gain * 255 (32-bit nativ muveletek!)
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
            // CW: CW frekvencia középen, +-600Hz padding
            uint16_t centerFreq = config.data.cwToneFrequencyHz;
            constexpr uint16_t CW_PADDING_HZ = 600; // +-600Hz padding a CW frekvencia körül

            currentTuningAidMinFreqHz_ = centerFreq - CW_PADDING_HZ;
            currentTuningAidMaxFreqHz_ = centerFreq + CW_PADDING_HZ;

        } else if (currentTuningAidType_ == TuningAidType::RTTY_TUNING) {
            // RTTY: Fix szélességű tartomány, mark és space közti középfrekvencia középen
            uint16_t f_mark = config.data.rttyMarkFrequencyHz;
            uint16_t f_space = f_mark - config.data.rttyShiftFrequencyHz;

            // Középfrekvencia: mark és space között
            float f_center = (f_mark + f_space) / 2.0f;

            // FIX tartomány szélessége: ±800Hz a központól (1600Hz összesen)
            // Ez elegendő minden RTTY shift típushoz (85-850Hz), és mindig középen van a center
            constexpr float RTTY_HALF_SPAN_HZ = 800.0f;

            currentTuningAidMinFreqHz_ = f_center - RTTY_HALF_SPAN_HZ;
            currentTuningAidMaxFreqHz_ = f_center + RTTY_HALF_SPAN_HZ;

        } else {
            // OFF_DECODER: alapértelmezett tartomány
            currentTuningAidMinFreqHz_ = 0.0f;
            currentTuningAidMaxFreqHz_ = maxDisplayFrequencyHz_;
        }

        // Ha változott a frekvencia tartomány, invalidáljuk a buffert (csak waterfall módokhoz)
        if ((typeChanged || oldMinFreq != currentTuningAidMinFreqHz_ || oldMaxFreq != currentTuningAidMaxFreqHz_) &&
            (currentMode_ == DisplayMode::CWWaterfall || currentMode_ == DisplayMode::RTTYWaterfall)) {
            std::fill(wabuf_.begin(), wabuf_.end(), 0); // 1D buffer trls
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
            modeText = "Bar+Waterfall";
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
    float displayGainDb = calculateDisplayGainDb(magnitudeData, minBin, maxBin, isAutoGainMode(), gainCfg);

    // Baseline erősítés és bandwidth gain összeadása dB formátumban
    float baselineGainDb = isLowRes ? LOWRES_BASELINE_GAIN_DB : HIGHRES_BASELINE_GAIN_DB;
    float totalGainDb = displayGainDb + baselineGainDb + cachedGainDb_;

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
        // A totalGainDb-t (a teljes, decibelben számolt erősítést) adjuk át.
        uint16_t targetHeights[LOW_RES_BANDS] = {0};
        for (uint8_t i = 0; i < numBars; i++) {
            targetHeights[i] = q15ToPixelHeightLogarithmic(bandMaxValues[i], totalGainDb, graphH);
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
            targetHeights[x] = q15ToPixelHeightLogarithmic(magnitudeData[binIdx], totalGainDb, graphH);
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
    float baseline_osc_lin_factor = powf(10.0f, OSCILLOSCOPE_BASELINE_GAIN_DB / 20.0f);
    float final_gain_lin = cachedGainLinear_ * baseline_osc_lin_factor;

    if (isAutoGainMode()) {
        uint16_t maxPixelHeight = q15ToPixelHeight(max_abs, (int32_t)(final_gain_lin * 255.0f), graphH / 4);
        if (maxPixelHeight == 0 && max_abs > 0)
            maxPixelHeight = 1;
        updateMagnitudeBasedGain(static_cast<float>(maxPixelHeight));
        final_gain_lin *= magnitudeAgcGainFactor_;
    } else {
        int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        final_gain_lin *= powf(10.0f, static_cast<float>(gainCfg) / 20.0f) * softGainFactor;
    }
    // --- End of Gain Calculation ---

    // Gain korltozsa oszcilloszkphoz
    final_gain_lin = constrain(final_gain_lin, 0.1f, 10.0f);

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
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData = nullptr;
    uint16_t actualFftSize = 0;
    float currentBinWidthHz = 0.0f;
    uint16_t current_height = 0;
    int32_t max_mag = 0;
    float totalGainDb = 0;

    if (getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) && magnitudeData && currentBinWidthHz > 0) {
        const uint16_t min_bin = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
        const uint16_t max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));

        // --- Erősítés számítása (egységesen dB-ben) ---
        int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
        totalGainDb = calculateDisplayGainDb(magnitudeData, min_bin, max_bin, isAutoGainMode(), gainCfg);

        // Alap és sávszélesség-specifikus erősítések hozzáadása, plusz kompenzáció a baseline gain miatt
        totalGainDb += ENVELOPE_BASELINE_GAIN_DB + cachedGainDb_ + 12.0f;

        // A spektrum CSÚCS magnitúdójának megkeresése, nem az átlag. Így reszponszívabb.
        for (uint16_t i = min_bin; i <= max_bin; i++) {
            int32_t val = q15Abs(magnitudeData[i]);
            if (val > max_mag) {
                max_mag = val;
            }
        }

        if (max_mag > 0) {
            // JAVÍTÁS: LINEÁRIS magasság számítás a jobb dinamika érdekében
            float total_gain_lin = powf(10.0f, totalGainDb / 20.0f);
            int32_t total_gain_scaled = (int32_t)(total_gain_lin * 255.0f);
            current_height = q15ToPixelHeight((q15_t)max_mag, total_gain_scaled, graphH);
        }
    }

    // Időbeli simítás eltávolítva
    uint16_t pixelHeight = current_height;

    // // Diagnosztikai naplózás
    // static long lastEnvelopeLogTime = 0;
    // if (Utils::timeHasPassed(lastEnvelopeLogTime, 500)) { // 500ms-enként naplóz
    //     UISPECTRUM_DEBUG("Envelope: totalGain=%.1fdB, max_mag=%d, height=%d\n",
    //         totalGainDb, max_mag, pixelHeight);
    //     lastEnvelopeLogTime = millis();
    // }

    // --- Rajzolási logika ---
    sprite_->scroll(-1, 0); // Görgetés balra

    uint16_t lastColX = bounds.width - 1;
    sprite_->drawFastVLine(lastColX, 0, graphH, TFT_BLACK); // Utolsó oszlop törlése

    uint8_t yCenter = graphH / 2;

    if (pixelHeight > 1) {
        uint16_t halfHeight = pixelHeight / 2;
        // Megakadályozza a komponens függőleges határainak túllépését
        if (halfHeight >= yCenter) {
            halfHeight = yCenter - 1;
        }

        uint16_t yUpper = yCenter - halfHeight;
        uint16_t yLower = yCenter + halfHeight;

        sprite_->drawFastVLine(lastColX, yUpper, yLower - yUpper + 1, TFT_CYAN);
    } else {
        // Egyetlen pixeles középvonal rajzolása az új oszlophoz, ha nincs jelentős jel
        sprite_->drawPixel(lastColX, yCenter, TFT_DARKGREY);
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    // Frekvencia feliratok nem kellenek envelope módban, mert az X-tengely az idő.
    // renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
}

/**
 * @brief Spektrum bar és waterfall renderelése egyszerre
 */
void UICompSpectrumVis::renderSpectrumBarWithWaterfall() {
    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData;
    uint16_t actualFftSize;
    float currentBinWidthHz;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    const uint16_t minBin = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const uint16_t maxBin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const uint16_t numBins = std::max(1, maxBin - minBin + 1);

    // Layout:
    const uint16_t barHeight = graphH / 3;
    const uint16_t waterfallStartY = barHeight;

    // --- Gain Calculation (Unified dB) ---
    int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float displayGainDb = calculateDisplayGainDb(magnitudeData, minBin, maxBin, isAutoGainMode(), gainCfg);

    // Use HIGHRES baseline for the bar part, and WATERFALL baseline for the waterfall
    float barTotalGainDb = displayGainDb + HIGHRES_BASELINE_GAIN_DB + cachedGainDb_;
    float waterfallTotalGainDb = displayGainDb + WATERFALL_BASELINE_GAIN_DB + cachedGainDb_;

    // --- High-Res Bar Part ---
    if (highresSmoothedCols.size() != bounds.width) {
        highresSmoothedCols.assign(bounds.width, 0.0f);
    }

    std::vector<uint16_t> targetHeights(bounds.width, 0);
    for (uint16_t x = 0; x < bounds.width; x++) {
        float ratio = (bounds.width > 1) ? static_cast<float>(x) / (bounds.width - 1) : 0.0f;
        uint16_t binIdx = minBin + static_cast<uint16_t>(ratio * (numBins - 1));
        binIdx = constrain(binIdx, minBin, maxBin);
        targetHeights[x] = q15ToPixelHeightLogarithmic(magnitudeData[binIdx], barTotalGainDb, barHeight);
    }

    const float SMOOTH_ALPHA = 0.7f;
    for (uint16_t x = 0; x < bounds.width; x++) {
        highresSmoothedCols[x] = SMOOTH_ALPHA * highresSmoothedCols[x] + (1.0f - SMOOTH_ALPHA) * targetHeights[x];
    }

    // --- Waterfall Part ---
    sprite_->scroll(0, 1); // Scroll the whole sprite down

    // Redraw the bar area (top part)
    sprite_->fillRect(0, 0, bounds.width, barHeight, TFT_BLACK);
    for (uint16_t x = 0; x < bounds.width; x++) {
        uint16_t height = static_cast<uint16_t>(highresSmoothedCols[x] + 0.5f);
        height = constrain(height, 0, barHeight);
        if (height > 0) {
            sprite_->drawFastVLine(x, barHeight - height, height, TFT_GREEN);
        }
    }

    // Draw the new line for the waterfall at its starting position
    for (uint16_t x = 0; x < bounds.width; x++) {
        float ratio = (bounds.width > 1) ? static_cast<float>(x) / (bounds.width - 1) : 0.0f;
        uint16_t binIdx = minBin + static_cast<uint16_t>(ratio * (numBins - 1));
        binIdx = constrain(binIdx, minBin, maxBin);

        uint8_t val = q15ToUint8Log(magnitudeData[binIdx], waterfallTotalGainDb);
        uint16_t color = valueToWaterfallColor(val, WATERFALL_COLOR_INDEX);
        sprite_->drawPixel(x, waterfallStartY, color);
    }

    // --- Final Render ---
    sprite_->pushSprite(bounds.x, bounds.y);
    renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
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

    // Görgetjük a sprite-ot egy sorral lejjebb, mielőtt az új sort rajzolnánk
    sprite_->scroll(0, 1);

    const int min_bin = std::max(2, static_cast<int>(std::round(MIN_AUDIO_FREQUENCY_HZ / currentBinWidthHz)));
    const int max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(maxDisplayFrequencyHz_ / currentBinWidthHz)));
    const int num_bins = std::max(1, max_bin - min_bin + 1);

    // GAIN SZÁMÍTÁS (dB-ben)
    int8_t gainCfg = (radioMode_ == RadioMode::AM) ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    float displayGainDb = calculateDisplayGainDb(magnitudeData, min_bin, max_bin, isAutoGainMode(), gainCfg);
    float totalGainDb = displayGainDb + WATERFALL_BASELINE_GAIN_DB + cachedGainDb_;

    for (int i = 0; i < bounds.width; ++i) {
        float ratio = (float)i / (bounds.width - 1);
        int bin_idx = min_bin + (int)(ratio * (num_bins - 1));
        bin_idx = constrain(bin_idx, min_bin, max_bin);
        q15_t mag_q15 = magnitudeData[bin_idx];

        uint8_t val = q15ToUint8Log(mag_q15, totalGainDb);
        uint16_t color = valueToWaterfallColor(val, WATERFALL_COLOR_INDEX);

        // Az új sort a sprite tetejére rajzoljuk (y=0)
        sprite_->drawPixel(i, 0, color);
    }

    sprite_->pushSprite(bounds.x, bounds.y);
    renderFrequencyRangeLabels(MIN_AUDIO_FREQUENCY_HZ, maxDisplayFrequencyHz_);
}

/**
 * @brief CW/RTTY hangolási segéd (vízesés) renderelése
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidWaterfall() {
    uint16_t graphH = getGraphHeight();
    if (!flags_.spriteCreated || bounds.width == 0 || graphH <= 0) {
        return;
    }

    const q15_t *magnitudeData;
    uint16_t actualFftSize;
    float currentBinWidthHz;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    // Görgetés
    sprite_->scroll(0, 1);

    // 1. Frekvenciatartomány és gain meghatározása
    const bool isCw = (currentTuningAidType_ == TuningAidType::CW_TUNING);
    const float min_freq = currentTuningAidMinFreqHz_;
    const float max_freq = currentTuningAidMaxFreqHz_;
    const int min_bin = std::max(0, static_cast<int>(std::round(min_freq / currentBinWidthHz)));
    const int max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(max_freq / currentBinWidthHz)));

    // 2. Teljes erősítés kiszámítása dB-ben
    int8_t gainCfg = config.data.audioFftGainConfigAm; // CW/RTTY is in AM mode
    float displayGainDb = calculateDisplayGainDb(magnitudeData, min_bin, max_bin, (gainCfg == SPECTRUM_GAIN_MODE_AUTO), gainCfg);
    float baselineGainDb = isCw ? CW_WATERFALL_BASELINE_GAIN_DB : RTTY_WATERFALL_BASELINE_GAIN_DB;
    float totalGainDb = displayGainDb + baselineGainDb + cachedGainDb_;

    // 3. Pixel mapping és rajzolás
    for (uint16_t x = 0; x < bounds.width; x++) {
        float current_freq = min_freq + (max_freq - min_freq) * x / (bounds.width - 1);
        float exact_bin = (current_freq / currentBinWidthHz);
        q15_t mag_q15 = q15Interpolate(magnitudeData, exact_bin, min_bin, max_bin);

        uint8_t val = q15ToUint8Log(mag_q15, totalGainDb);
        uint16_t color = valueToWaterfallColor(val, WATERFALL_COLOR_INDEX);

        sprite_->drawPixel(x, 0, color);
    }

    renderTuningAidFrequencyLabels(min_freq, max_freq, graphH);
    sprite_->pushSprite(bounds.x, bounds.y);
}

/**
 * @brief CW/RTTY hangolási segéd (SNR görbe) renderelése
 */
void UICompSpectrumVis::renderCwOrRttyTuningAidSnrCurve() {

    uint16_t graphH = getGraphHeight();
    uint16_t targetHeight = graphH - 15;
    if (!flags_.spriteCreated || bounds.width == 0 || targetHeight <= 0) {
        return;
    }

    const q15_t *magnitudeData;
    uint16_t actualFftSize;
    float currentBinWidthHz;
    if (!getCore1SpectrumData(&magnitudeData, &actualFftSize, &currentBinWidthHz) || !magnitudeData || currentBinWidthHz == 0) {
        sprite_->pushSprite(bounds.x, bounds.y);
        return;
    }

    sprite_->fillSprite(TFT_BLACK);

    // 1. Frekvenciatartomány és gain meghatározása
    const bool isCw = (currentTuningAidType_ == TuningAidType::CW_TUNING);
    const float min_freq = currentTuningAidMinFreqHz_;
    const float max_freq = currentTuningAidMaxFreqHz_;
    const int min_bin = std::max(0, static_cast<int>(std::round(min_freq / currentBinWidthHz)));
    const int max_bin = std::min(static_cast<int>(actualFftSize - 1), static_cast<int>(std::round(max_freq / currentBinWidthHz)));

    // 2. Teljes erősítés kiszámítása dB-ben
    int8_t gainCfg = config.data.audioFftGainConfigAm; // CW/RTTY is in AM mode
    float displayGainDb = calculateDisplayGainDb(magnitudeData, min_bin, max_bin, (gainCfg == SPECTRUM_GAIN_MODE_AUTO), gainCfg);
    float baselineGainDb = isCw ? CW_SNRCURVE_BASELINE_GAIN_DB : RTTY_SNRCURVE_BASELINE_GAIN_DB;
    float totalGainDb = displayGainDb + baselineGainDb + cachedGainDb_;

    // 4. Pixel mapping és rajzolás
    uint16_t prev_x = 0;
    uint16_t prev_y = 0;
    for (uint16_t x = 0; x < bounds.width; x++) {
        float current_freq = min_freq + (max_freq - min_freq) * x / (bounds.width - 1);
        float exact_bin = (current_freq / currentBinWidthHz);
        q15_t mag_q15 = q15Interpolate(magnitudeData, exact_bin, min_bin, max_bin);

        // Konverzió pixel magasságra (SNR GÖRBE)
        uint16_t height = q15ToPixelHeightSnrCurve(mag_q15, totalGainDb, targetHeight);
        uint16_t y = graphH - height;

        if (x > 0) {
            sprite_->drawLine(prev_x, prev_y, x, y, TFT_CYAN);
        }
        prev_x = x;
        prev_y = y;
    }

    renderTuningAidFrequencyLabels(min_freq, max_freq, graphH);
    sprite_->pushSprite(bounds.x, bounds.y);
}