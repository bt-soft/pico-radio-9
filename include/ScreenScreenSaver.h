/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenScreenSaver.h                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:51:31                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "ScreenFrequDisplayBase.h"
#include "UICompSevenSegmentFreq.h"

/**
 * @brief Képernyővédő konstansok namespace
 * @details Animáció és UI elem pozicionálási konstansok
 */
namespace ScreenSaverConstants {
// Animáció alapvető paraméterei
constexpr int SAVER_ANIMATION_STEPS = 500;         // Animációs lépések száma
constexpr int SAVER_ANIMATION_LINE_LENGTH = 63;    // Animációs vonal hossza
constexpr int SAVER_LINE_CENTER = 31;              // Vonal középpontja
constexpr int SAVER_NEW_POS_INTERVAL_MSEC = 15000; // Új pozíció intervalluma (ms)
constexpr int SAVER_COLOR_FACTOR = 64;             // Szín változtatási faktor
constexpr int SAVER_ANIMATION_STEP_JUMP = 3;       // Animációs lépés ugrás

// Animált keret mérete és UI elemek relatív pozíciói a keret bal felső sarkához képest
// Különböző szélességek a rádió módok szerint (SevenSegmentFreq-hez igazítva + margók + akkumulátor)
constexpr int ANIMATION_BORDER_WIDTH_DEFAULT = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH;         // Alapértelmezett szélesség
constexpr int ANIMATION_BORDER_WIDTH_FM = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 90;         // FM mód
constexpr int ANIMATION_BORDER_WIDTH_AM_LW = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 100;     // AM LW mód
constexpr int ANIMATION_BORDER_WIDTH_AM_MW = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 100;     // AM MW mód
constexpr int ANIMATION_BORDER_WIDTH_AM_SW = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 70;      // AM SW mód
constexpr int ANIMATION_BORDER_WIDTH_SSB_CW = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 70;     // SSB/CW módok (LSB/USB + CW)
constexpr int ANIMATION_BORDER_WIDTH_SSB_CW_BFO = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 25; // SSB/CW + BFO
constexpr int ANIMATION_BORDER_HEIGHT = 45;                                                              // Animált keret magassága (kompaktabb)

// UI elemek pozicionálása a keret belsejében
constexpr int INTERNAL_MARGIN = 2;                                        // Belső margó a keret széleitől
constexpr int SEVEN_SEGMENT_FREQ_Y_OFFSET_FROM_TOP = INTERNAL_MARGIN + 2; // SevenSegmentFreq Y pozíció a keret tetejétől
constexpr int SEVEN_SEGMENT_FREQ_Y_OFFSET = SEVEN_SEGMENT_FREQ_Y_OFFSET_FROM_TOP;

// Akkumulátor szimbólum pozíció - SevenSegmentFreq jobb oldalától 2px távolságra (keret belsejében)
constexpr int ELEMENT_GAP = 2;                                 // Gap az elemek között
constexpr int BATTERY_Y_OFFSET_FROM_TOP = INTERNAL_MARGIN + 5; // Akkumulátor Y pozíció a keret tetejétől
constexpr int BATTERY_BASE_Y_OFFSET = BATTERY_Y_OFFSET_FROM_TOP;
constexpr uint8_t BATTERY_RECT_W = 38;                                  // Akkumulátor téglalap szélessége
constexpr uint8_t BATTERY_RECT_H = 18;                                  // Akkumulátor téglalap magassága
constexpr uint8_t BATTERY_NUB_W = 2;                                    // Akkumulátor "Dudor" (+ érintkező) szélessége
constexpr uint8_t BATTERY_NUB_H = 10;                                   // Akkumulátor "Dudor" (+ érintkező) magassága
constexpr uint8_t BATTERY_RECT_FULL_W = BATTERY_RECT_W + BATTERY_NUB_W; // Teljes akkumulátor téglalap szélessége (dudorral együtt)
} // namespace ScreenSaverConstants

/**
 * @file ScreenScreenSaver.h
 * @brief Képernyővédő osztály implementációja
 * @details Animált kerettel és frekvencia kijelzéssel rendelkező képernyővédő
 */
class ScreenScreenSaver : public ScreenFrequDisplayBase {
  private:
    // Időzítés változók
    uint32_t activationTime;          // Képernyővédő aktiválásának időpontja
    uint32_t lastAnimationUpdateTime; // Utolsó animáció frissítés időpontja

    virtual void activate() override; // Animáció állapot változók
    uint16_t animationBorderX;        // Animált keret bal felső sarkának X koordinátája
    uint16_t animationBorderY;        // Animált keret bal felső sarkának Y koordinátája
    uint16_t currentFrequencyValue;   // Aktuális frekvencia érték

    uint16_t posSaver;                                                          // Animáció pozíció számláló
    uint8_t saverLineColors[ScreenSaverConstants::SAVER_ANIMATION_LINE_LENGTH]; // Animációs vonal színei

    uint32_t lastFullUpdateSaverTime; // Utolsó teljes frissítés időpontja

    uint16_t currentBorderWidth = 0; // Aktuális keret szélesség a rádió mód szerint
    uint16_t currentAccuXOffset = 0; // Akkumulátor X pozíció a keret bal szélétől

    /**
     * @brief Képernyővédő aktiválása
     * @details Privát metódus, konstruktorból hívva
     */

    /**
     * @brief Animált keret rajzolása
     * @details Frekvencia kijelző körül mozgó téglalap keret rajzolása
     */
    void drawAnimatedBorder();

    /**
     * @brief Akkumulátor információ rajzolása
     * @details Akkumulátor szimbólum és töltöttségi szöveg kirajzolása
     * Az animált keret pozíciójához relatívan pozícionálva
     */
    void drawBatteryInfo();

    /**
     * @brief Frekvencia és akkumulátor kijelző frissítése
     * @details 15 másodpercenként frissíti a pozíciót és a kijelzett információkat
     */
    void updateFrequencyAndBatteryDisplay();

    /**
     * @brief Aktuális rádió módhoz tartozó keret szélesség meghatározása
     * @return A keret szélessége pixelben
     * @details FM: 210px, AM: 180px, SSB: 150px, CW: 120px
     */
    uint16_t getCurrentBorderWidth() const;

  public:
    /**
     * @brief Konstruktor
     */
    ScreenScreenSaver();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenScreenSaver() = default;

    /**
     * @brief Képernyővédő deaktiválása
     */
    virtual void deactivate() override;

    /**
     * @brief Tartalom rajzolása
     * @details Minden animációs frame-ben meghívódik
     */
    virtual void drawContent() override;

    /**
     * @brief Saját loop kezelése
     * @details Animáció és időzítés logika
     */
    virtual void handleOwnLoop() override;

    /**
     * @brief Érintés esemény kezelése
     * @param event Érintés esemény
     * @return true ha kezelte az eseményt, false egyébként
     */
    virtual bool handleTouch(const TouchEvent &event) override;

    /**
     * @brief Forgó encoder esemény kezelése
     * @param event Forgó encoder esemény
     * @return true ha kezelte az eseményt, false egyébként
     */
    virtual bool handleRotary(const RotaryEvent &event) override;
};