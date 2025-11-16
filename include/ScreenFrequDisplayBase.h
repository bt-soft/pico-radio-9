/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenFrequDisplayBase.h                                                                                      *
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
 * Last Modified: 2025.11.16, Sunday  09:51:00                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "UICompSevenSegmentFreq.h"
#include "UIScreen.h"

/**
 * @class ScreenFrequDisplayBase
 * @brief Alap UIScreen osztály frekvencia kijelző komponenssel
 * @details Ez az absztrakciós réteg az UIScreen és a konkrét frekvencia kijelzőt használó képernyők között.
 *
 * **Fő funkciók:**
 * - UICompSevenSegmentFreq komponens integrációja
 *
 * **Örökölhető osztályok:**
 * - ScreenRadioBase - Rádió vezérlő képernyők alaposztálya
 */
class ScreenFrequDisplayBase : public UIScreen {
  protected:
    // Frekvencia kijelző komponens
    std::shared_ptr<UICompSevenSegmentFreq> sevenSegmentFreq;

    /**
     * @brief Létrehozza a frekvencia kijelző komponenst
     * @param freqBounds A frekvencia kijelző határai (Rect)
     */
    inline void createSevenSegmentFreq(Rect freqBounds) {
        sevenSegmentFreq = std::make_shared<UICompSevenSegmentFreq>(freqBounds);
        addChild(sevenSegmentFreq);
    }

  public:
    ScreenFrequDisplayBase(const char *name) : UIScreen(name) {}

    /**
     * @brief Frekvencia kijelző komponens lekérése
     */
    inline std::shared_ptr<UICompSevenSegmentFreq> getSevenSegmentFreq() const { return sevenSegmentFreq; }
};