/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMWeFax.h                                                                                               *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.22, Saturday  07:22:44                                                                       *
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
#include "UICompTuningBar.h"

/**
 * @brief AM WeFax dekóder képernyő
 * @details WeFax dekóder megjelenítése és kezelése
 */
class ScreenAMWeFax : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMWeFax> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMWeFax();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMWeFax() override;

    /**
     * @brief Képernyő aktiválása
     */
    virtual void activate() override;

    /**
     * @brief Képernyő deaktiválása
     */
    virtual void deactivate() override;

    /**
     * @brief Képernyő tartalom rajzolása
     */
    virtual void drawContent() override;

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
     * @brief SSTV specifikus gombok hozzáadása a közös AM gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Hozzáadja a SSTV-specifikus gombokat a vízszintes gombsorhoz
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

  private:
    // WEFAX mód változás ellenőrzése
    uint8_t cachedMode;
    uint16_t cachedDisplayWidth;
    uint16_t displayWidth;
    uint16_t sourceWidth;
    uint16_t sourceHeight;
    float scale;
    uint16_t targetHeight;
#define WEFAX_MAX_DISPLAY_WIDTH 800
    uint16_t displayBuffer[WEFAX_MAX_DISPLAY_WIDTH];
    float accumulatedTargetLine;
    uint16_t lastDrawnTargetLine;
    // Reset gomb, ami törli a képterületet és reseteli a dekódert
    std::shared_ptr<UIButton> resetButton;
    // Tuning Bar - FFT spektrum sáv
    std::shared_ptr<UICompTuningBar> tuningBar;

    void clearPictureArea();
    void checkDecodedData();
    void drawWeFaxMode(const char *modeName);
};
