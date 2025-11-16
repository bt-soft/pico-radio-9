/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompStereoIndicator.h                                                                                       *
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
 * Last Modified: 2025.11.16, Sunday  09:54:09                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "UIComponent.h"

/**
 * @brief STEREO/MONO jelző komponens FM rádióhoz
 * @details Egyszerű komponens, ami STEREO (piros háttér) vagy MONO (kék háttér) feliratot jelenít meg
 */
class UICompStereoIndicator : public UIComponent {
  private:
    bool isStereo = false;
    bool needsUpdate = true;

  public:
    /**
     * @brief Konstruktor
     * @param tft TFT display referencia
     * @param bounds Komponens pozíciója és mérete
     * @param colors Színséma (opcionális)
     */
    UICompStereoIndicator(const Rect &bounds, const ColorScheme &colors = ColorScheme::defaultScheme());

    /**
     * @brief Stereo állapot beállítása
     * @param stereo true = STEREO (piros), false = MONO (kék)
     */
    void setStereo(bool stereo);

    /**
     * @brief Jelenlegi stereo állapot lekérdezése
     * @return true ha STEREO, false ha MONO
     */
    bool getStereo() const { return isStereo; } // UIComponent interface implementáció
    virtual void draw() override;
    virtual bool handleTouch(const TouchEvent &event) override { return false; }
    virtual bool handleRotary(const RotaryEvent &event) override { return false; }
};
