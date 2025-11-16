/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenSetupSystem.h                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:51:54                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "ScreenSetupBase.h"

/**
 * @brief Rendszer beállítások képernyő.
 *
 * Ez a képernyő a rendszer és felhasználói felület beállításait kezeli:
 * - TFT háttérvilágítás fényességének beállítása
 * - Képernyővédő időtúllépésének beállítása
 * - Inaktív számjegyek világítása
 * - Hangjelzések engedélyezése
 * - Rotary encoder gyorsítás beállítása
 */
class ScreenSetupSystem : public ScreenSetupBase {
  private:
    /**
     * @brief Rendszer specifikus menüpont akciók
     */
    enum class SystemItemAction {
        NONE = 0,
        BRIGHTNESS = 300,
        SAVER_TIMEOUT,
        INACTIVE_DIGIT_LIGHT,
        BEEPER_ENABLED,
        ROTARY_ACCELERATION, // Rotary gyorsítás beállítása
    };

    // Rendszer specifikus dialógus kezelő függvények
    void handleBrightnessDialog(int index);
    void handleSaverTimeoutDialog(int index);
    void handleToggleItem(int index, bool &configValue);

  protected:
    // SetupScreenBase virtuális metódusok implementációja
    virtual void populateMenuItems() override;
    virtual void handleItemAction(int index, int action) override;
    virtual const char *getScreenTitle() const override;

  public:
    /**
     * @brief Konstruktor.
     * @param tft TFT_eSPI referencia.
     */
    ScreenSetupSystem();
    virtual ~ScreenSetupSystem() = default;
};
