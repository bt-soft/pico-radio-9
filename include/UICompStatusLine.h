/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompStatusLine.h                                                                                            *
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
 * Last Modified: 2025.11.16, Sunday  09:53:58                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "UIComponent.h"

/**
 * @file UICompStatusLine.h
 * @brief UICompStatusLine komponens - állapotsor megjelenítése a képernyő tetején
 * @details Ez a komponens 9 kis négyzetet jelenít meg vízszintesen, mindegyikben különböző állapotinformációkkal.
 * A komponens nem reagál touch és rotary eseményekre, csak információ megjelenítésére szolgál.
 */
class UICompStatusLine : public UIComponent {
  private:
    // Négyzetek pozíciói és méretei
    struct StatusBox {
        uint16_t x;
        uint16_t width;
        uint16_t color;
    };
    bool stationInMemory; // Memória állomás jelző

#define STATUS_LINE_BOXES 10
    StatusBox statusBoxes[STATUS_LINE_BOXES];

    // Privát rajzoló metódusok
    void initializeBoxes();
    void drawBoxFrames();
    void clearBoxContent(uint8_t boxIndex);
    void drawTextInBox(uint8_t boxIndex, const char *text, uint16_t textColor);

    /**
     * @brief Reseteli a betűtípust és a szöveg méretét
     * @details Ez a metódus visszaállítja a TFT betűtípust az alapértelmezett értékre és a szöveg méretét 1-re.
     */
    void resetFont() {
        tft.setFreeFont();
        tft.setTextSize(1);
    }

  public:
    /**
     * @brief UICompStatusLine konstruktor
     * @param x A komponens X koordinátája
     * @param y A komponens Y koordinátája
     * @param colors Színséma (opcionális, alapértelmezett színsémát használ ha nincs megadva)
     */
    UICompStatusLine(int16_t x, int16_t y, const ColorScheme &colors = ColorScheme::defaultScheme());

    /**
     * @brief Virtuális destruktor
     */
    virtual ~UICompStatusLine() = default;

    /**
     * @brief Touch esemény kezelése
     * @param event Touch esemény
     * @return false - ez a komponens nem kezeli a touch eseményeket
     */
    virtual bool handleTouch(const TouchEvent &event) override { return false; };

    /**
     * @brief Rotary encoder esemény kezelése
     * @param event Rotary esemény
     * @return false - ez a komponens nem kezeli a rotary eseményeket
     */
    virtual bool handleRotary(const RotaryEvent &event) override { return false; };

    /**
     * @brief A komponens kirajzolása
     * @details Megrajzolja az állapotsor kereteit és inicializálja a négyzeteket
     */
    virtual void draw() override;

    // Értékfrissítő metódusok - mindegyik a saját négyzetét frissíti
    void updateBfo();
    void updateAgc();
    void updateMode();
    void updateBandwidth();
    void updateBand();
    void updateStep();
    void updateAntCap();
    void updateTemperature();
    void updateVoltage();
    void updateStationInMemory(bool isInMemo);
};
