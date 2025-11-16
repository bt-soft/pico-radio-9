/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMRTTY.h                                                                                                *
 * Created Date: 2025.11.13.                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:50:27                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "ScreenAMRadioBase.h"
#include "UICommonVerticalButtons.h"
#include "UICompTextBox.h"

/**
 * @brief AM CW dekóder képernyő
 * @details CW (Morse) dekóder megjelenítése és kezelése
 */
class ScreenAMRTTY : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMRTTY> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMRTTY();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMRTTY() override;

    /**
     * @brief Képernyő aktiválása
     */
    virtual void activate() override;

    /**
     * @brief Képernyő deaktiválása
     */
    virtual void deactivate() override;

    /**
     * @brief Folyamatos loop hívás
     */
    virtual void handleOwnLoop() override;

  protected:
    /**
     * @brief UI komponensek létrehozása és képernyőn való elhelyezése
     */
    void layoutComponents();

    /**
     * @brief CW specifikus gombok hozzáadása a közös AM gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Hozzáadja a CW-specifikus gombokat (pl. WPM, Speed, stb.)
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

  private:
    std::shared_ptr<UICompTextBox> rttyTextBox; ///< RTTY dekódolt szöveg megjelenítése

    /**
     * @brief RTTY dekódolt szöveg ellenőrzése és frissítése
     */
    void checkDecodedData();

    uint16_t lastPublishedRttyMark;
    uint16_t lastPublishedRttySpace;
    float lastPublishedRttyBaud;
};
