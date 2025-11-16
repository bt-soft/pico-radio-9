/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompStereoIndicator.cpp                                                                                     *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                     *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:45:12                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "UICompStereoIndicator.h"

/**
 * @brief UICompStereoIndicator konstruktor
 */
UICompStereoIndicator::UICompStereoIndicator(const Rect &bounds, const ColorScheme &colors) : UIComponent(bounds, colors) {
    isStereo = false;
    needsUpdate = true;
}

/**
 * @brief Stereo állapot beállítása
 * @param stereo true = STEREO (piros), false = MONO (kék)
 */
void UICompStereoIndicator::setStereo(bool stereo) {
    if (isStereo != stereo) {
        isStereo = stereo;
        needsUpdate = true;
        markForRedraw();
    }
}

/**
 * @brief Komponens kirajzolása
 */
void UICompStereoIndicator::draw() {
    if (!needsUpdate && !needsRedraw) {
        return;
    }

    // Háttér színek
    uint16_t bgColor = isStereo ? TFT_RED : TFT_BLUE;
    uint16_t textColor = TFT_WHITE;

    // Háttér kitöltése
    tft.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, bgColor);

    // Keret rajzolása
    tft.drawRect(bounds.x, bounds.y, bounds.width, bounds.height, TFT_WHITE);

    // Szöveg beállítása
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(1);
    tft.setFreeFont();          // Alapértelmezett font
    tft.setTextDatum(MC_DATUM); // Middle Center

    // Szöveg kirajzolása
    const char *text = isStereo ? "STEREO" : "MONO";
    int16_t centerX = bounds.x + bounds.width / 2;
    int16_t centerY = bounds.y + bounds.height / 2;

    tft.drawString(text, centerX, centerY);

    needsUpdate = false;
    needsRedraw = false;
}
