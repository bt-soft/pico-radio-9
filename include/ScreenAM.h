/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAM.h                                                                                                    *
 * Created Date: 2025.11.09.                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:50:09                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "ScreenAmRadioBase.h"
#include "UICommonVerticalButtons.h"

class ScreenAM : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAM> {
  public:
    static bool audioDecoderRun;

    // ===================================================================
    // Konstruktor és destruktor
    // ===================================================================

    /**
     * @brief ScreenAM konstruktor - AM rádió képernyő inicializálás
     */
    ScreenAM();

    /**
     * @brief Virtuális destruktor - Automatikus cleanup
     */
    virtual ~ScreenAM();

    // ===================================================================
    // UIScreen interface megvalósítás
    // ===================================================================

    /**
     * @brief Statikus képernyő tartalom kirajzolása
     * @details Csak a statikus UI elemeket rajzolja:
     * - S-Meter skála (vonalak, számok) AM módban
     *
     * A dinamikus tartalom (pl. S-Meter érték) a loop()-ban frissül.
     */
    virtual void drawContent() override;

    /**
     * @brief Képernyő aktiválása - Event-driven gombállapot szinkronizálás
     * @details Ez az EGYETLEN hely, ahol gombállapotokat szinkronizálunk!
     */

    virtual void activate() override;

    /**
     * @brief Képernyő deaktiválása - Cleanup és állapotok visszaállítása
     *
     */
    virtual void deactivate() override;

    /**
     * @brief Dialógus bezárásának kezelése - Gombállapot szinkronizálás
     * @details Az utolsó dialógus bezárásakor frissíti a gombállapotokat
     *
     * Funkcionalitás:
     * - Alap UIScreen::onDialogClosed() hívása
     * - Ha ez volt az utolsó dialógus -> updateAllVerticalButtonStates() + updateHorizontalButtonStates()
     * - Biztosítja a konzisztens gombállapotokat dialógus bezárás után
     */
    virtual void onDialogClosed(UIDialogBase *closedDialog) override;

  protected:
    /**
     * @brief AM specifikus gombok hozzáadása a közös gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Felülírja az ős metódusát, hogy hozzáadja az AM specifikus gombokat
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

  private:
    // ===================================================================
    // UI komponensek layout és management
    // ===================================================================

    /**
     * @brief UI komponensek létrehozása és képernyőn való elhelyezése
     * @details Létrehozza és pozicionálja az összes UI elemet:
     * - Állapotsor (felül)
     * - Frekvencia kijelző (középen)
     * - S-Meter (jelerősség mérő)
     * - Függőleges gombsor (jobb oldal) - Közös FMScreen-nel
     * - Vízszintes gombsor (alul) - FM gombbal
     */
    void layoutComponents();

    // ===================================================================
    // AM specifikus gomb eseménykezelők
    // ===================================================================

    /**
     * @brief Step gomb eseménykezelő - Frequency Step
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleStepButton(const UIButton::ButtonEvent &event);

    /**
     * @brief Digit gomb eseménykezelő - Decoder választó
     * @param event Gomb esemény (Clicked)
     * @details Megnyitja a dekóder választó dialógust
     */
    void handleDecoderButton(const UIButton::ButtonEvent &event);
};
