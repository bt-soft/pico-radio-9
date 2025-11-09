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
    // Event-driven gombállapot szinkronizálás
    // ===================================================================

    /**
     * @brief AM specifikus vízszintes gombsor állapotainak szinkronizálása
     * @details CSAK aktiváláskor hívódik meg! Event-driven architektúra.
     *
     * Szinkronizált állapotok:
     * - AM specifikus gombok alapértelmezett állapotai
     */
    void updateHorizontalButtonStates();

    // ===================================================================
    // AM specifikus gomb eseménykezelők
    // ===================================================================

    /**
     * @brief Step gomb eseménykezelő - Frequency Step
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleStepButton(const UIButton::ButtonEvent &event);
};
